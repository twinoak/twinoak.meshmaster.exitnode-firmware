#include "AgentLoop.h"

#include <Arduino.h>
#include <esp_system.h>

#include <cinttypes>
#include <cstdio>
#include <cstring>

#include "Backoff.h"
#include "Health.h"
#include "Iso8601.h"
#include "Log.h"
#include "version.h"

namespace mm {

namespace {

// A batch of twenty, each item about 290 bytes at the longest name and a three-hop path.
constexpr size_t kBatchJsonCap = 8192;
constexpr int kMaxHopsReported = 8;   // kMaxObservationPathBytes at the one-byte hash
constexpr int kHopHexCap = 7;         // three bytes as hex, and a NUL

// The scratch a batch is written from. Static, not automatic: the Arduino loop task has 8 KB of
// stack and this is more than that on its own.
char g_batchJson[kBatchJsonCap];
ObservationItem g_items[kObservationBatchSize];
char g_keys[kObservationBatchSize][kPublicKeyLen * 2 + 1];
char g_hops[kObservationBatchSize][kMaxHopsReported][kHopHexCap];
const char* g_hopPtrs[kObservationBatchSize][kMaxHopsReported];

const char kHex[] = "0123456789abcdef";

void toHex(const uint8_t* bytes, size_t len, char* out) {
  for (size_t i = 0; i < len; i++) {
    out[i * 2] = kHex[bytes[i] >> 4];
    out[i * 2 + 1] = kHex[bytes[i] & 0x0F];
  }
  out[len * 2] = '\0';
}

bool elapsed(uint32_t now, uint32_t deadline) { return static_cast<int32_t>(now - deadline) >= 0; }

}  // namespace

void AgentLoop::begin(SettingsStore& settings, AgentClient& server, AgentClock& clock, WifiLink& wifi,
                      AgentMesh& mesh, JobRunner& jobs) {
  settings_ = &settings;
  server_ = &server;
  clock_ = &clock;
  wifi_ = &wifi;
  mesh_ = &mesh;
  jobs_ = &jobs;
  mesh_->setRing(&ring_);
  budget_.maxTransactionsPerMinute = kDefaultMaxTransactionsPerMinute;
  budget_.minGapSeconds = kDefaultMinGapSeconds;
  jobs_->setBudget(budget_);
}

void AgentLoop::setIdentity(const char* publicKeyHex) {
  publicKey_ = publicKeyHex;
  jobs_->setIdentity(publicKeyHex);
}

void AgentLoop::onWifiUp() {
  // The first link is not a reconnection; every one after it is.
  counters_.wifiReconnects = wifi_->connects() > 0 ? static_cast<uint32_t>(wifi_->connects() - 1) : 0;
  configDue_ = true;
  configAt_ = millis() + 1500;  // a moment for DNS and SNTP to get going
  heartbeatScheduled_ = false;
  holdUntil_ = millis();
  backoffMs_ = 0;
}

void AgentLoop::reconfigure() {
  if (halted_) LOG_I("server: the settings changed; the loop starts again");
  halted_ = false;
  backoffMs_ = 0;
  holdUntil_ = millis();
  configDue_ = wifi_ != nullptr && wifi_->connected();
  configAt_ = millis();
  heartbeatScheduled_ = false;
}

void AgentLoop::loop() {
  if (server_ == nullptr || halted_ || !server_->configured() || !wifi_->connected()) return;
  const uint32_t now = millis();
  if (!elapsed(now, holdUntil_)) return;

  if (configDue_) {
    if (!elapsed(now, configAt_)) return;
    doConfig();
    return;
  }
  if (!heartbeatScheduled_) return;
  if (elapsed(now, nextHeartbeatAt_)) {
    doHeartbeat();
    return;
  }
  // The offline buffer before anything new: what waited is posted first, in order, with the
  // timestamps and the client ids it was made with (EXIT-NODES §5).
  if (jobs_->queueDepth() > 0) {
    doPendingResults();
    return;
  }
  // A claim before the batch: the server said work was waiting, and a job on the air is what
  // this exit node is for. One claim per heartbeat that said so.
  if (claimDue_) {
    doJobs();
    return;
  }
  // The oldest pending entry carries the moment it was heard, so how long a batch has waited is
  // a fact about the ring and needs no timer of its own.
  const uint32_t waited = ring_.empty() ? 0 : now - ring_.at(0).heardAtMillis;
  if (observationBatchDue(ring_.size(), waited)) doObservations();
}

void AgentLoop::doConfig() {
  configDue_ = false;
  if (server_->fetchConfig()) {
    budget_ = server_->config().budget;
    pollIntervalS_ = server_->config().pollIntervalS > 0 ? server_->config().pollIntervalS : 30;
  } else {
    // The config call is a startup nicety - the heartbeat answers the budget, the poll interval
    // and the clock every time - so a failure here is noted and the loop starts anyway, at
    // whatever pace the answer earned. A 401 is the exception: there is no loop to start.
    if (noteFailure(server_->lastCall()) == ServerAction::Halt) return;
    LOG_W("server: the startup config call did not answer; the heartbeat asks the same things every %d s",
          pollIntervalS_);
  }
  heartbeatScheduled_ = true;
  nextHeartbeatAt_ = millis();  // the first heartbeat goes as soon as the hold, if any, is over
}

void AgentLoop::buildHeartbeat(Heartbeat& hb) const {
  hb.agentVersion = meshmaster_agent_version();
  hb.transport = "onboard";
  hb.at = clock_->now();
  hb.companionPublicKey = publicKey_;              // nullptr while the radio has made no identity
  hb.companionFirmware = publicKey_ == nullptr ? nullptr : meshcoreTag();
  hb.hasRadio = publicKey_ != nullptr;
  if (hb.hasRadio) {
    hb.radio.freqMHz = settings_->numberDouble(*findKey("radio.freq"));
    hb.radio.bwKHz = settings_->numberDouble(*findKey("radio.bw"));
    hb.radio.sf = static_cast<int>(settings_->number(*findKey("radio.sf")));
    hb.radio.cr = static_cast<int>(settings_->number(*findKey("radio.cr")));
  }
  // The thirteen this firmware has a handler for - every type of API §3 since task 05: routing
  // requires the type advertised, so this list is what the server may send and nothing else
  // (task p3-53).
  hb.hasJobTypes = true;
  hb.jobTypes = kJobTypes;
  hb.jobTypeCount = kJobTypeCount;
  hb.hasBudget = true;
  hb.budget = budget_;
  // The radio is the companion here: "connected" is "the SX1262 came up", and a board whose
  // radio did not is an exit node that hears nothing, which is what the alert of §10 is for.
  hb.companionConnected = publicKey_ != nullptr;
  hb.queueDepth = jobs_->queueDepth();
  hb.board = boardName();
}

ServerAction AgentLoop::noteFailure(const AgentClient::Call& call) {
  const ServerAction action = actionFor(call.reachable, call.httpStatus);
  switch (action) {
    case ServerAction::Halt:
      halted_ = true;
      LOG_E("server: the token was refused (http 401): revoked, or wrong. Nothing more is sent until a person "
            "fixes it - `set meshmaster.token <token>` on this console.");
      break;
    case ServerAction::WaitRetryAfter: {
      const uint32_t wait = retryAfterMs(call.retryAfterS);
      holdUntil_ = millis() + wait;
      LOG_W("server: http 429; the wait is Retry-After's %lu s", static_cast<unsigned long>(wait / 1000));
      break;
    }
    case ServerAction::Discard:
      counters_.httpFailures++;
      LOG_W("server: http %d; this request will not get better by being sent again", call.httpStatus);
      break;
    case ServerAction::BackOff:
    default: {
      counters_.httpFailures++;
      counters_.consecutiveFailures++;
      backoffMs_ = nextBackoffMs(backoffMs_);
      // The sequence is 30 s, 60 s, 120 s ... ten minutes; what is waited is that plus or minus
      // a quarter, so two boards that lost the server together drift apart (src/Backoff.h).
      const uint32_t wait = withJitter(backoffMs_, esp_random());
      holdUntil_ = millis() + wait;
      LOG_I("server: next attempt in %lu s (the %lu s step, jittered)", static_cast<unsigned long>(wait / 1000),
            static_cast<unsigned long>(backoffMs_ / 1000));
      // A link that is up and a server that has not answered for an hour and a half is a stack
      // that is wedged, not a server that is busy: the board restarts itself, having said why.
      if (counters_.consecutiveFailures >= kRebootAfterFailures && wifi_ != nullptr && wifi_->connected()) {
        char why[kMaxRebootWhyLen + 1];
        std::snprintf(why, sizeof why, "%lu server calls in a row got no answer, with wifi up",
                      static_cast<unsigned long>(counters_.consecutiveFailures));
        healthRebootNow(why);
      }
      break;
    }
  }
  return action;
}

void AgentLoop::succeeded() {
  backoffMs_ = 0;
  counters_.consecutiveFailures = 0;
  holdUntil_ = millis();
}

void AgentLoop::heartbeatIfOverdue() {
  if (halted_ || inHeartbeat_ || server_ == nullptr || !server_->configured() || !heartbeatScheduled_) return;
  if (wifi_ == nullptr || !wifi_->connected()) return;
  const uint32_t now = millis();
  if (!elapsed(now, holdUntil_) || !elapsed(now, nextHeartbeatAt_)) return;
  LOG_D("server: the heartbeat is due inside a job; sending it from the job's own wait");
  doHeartbeat();
}

void AgentLoop::doHeartbeat() {
  inHeartbeat_ = true;
  Heartbeat hb;
  buildHeartbeat(hb);
  const ErrorCounter errors[] = {
      {"overheard", mesh_->overheard()},
      {"wifiReconnects", counters_.wifiReconnects},
      {"httpFailures", counters_.httpFailures},
      {"observationsDropped", ring_.dropped()},
      // Task 06's two: what the RAM buffers had to throw away while the server was gone, and
      // how many times this board restarted itself to clear a wedged stack (src/Health.h). The
      // reboot count is in RTC memory, so it survives the restart it counts.
      {"resultsDropped", jobs_->resultsDropped()},
      {"reboots", selfReboots()},
  };
  hb.errors = errors;
  hb.errorCount = static_cast<int>(sizeof errors / sizeof errors[0]);

  HeartbeatReply reply;
  const bool ok = server_->postHeartbeat(hb, reply);
  inHeartbeat_ = false;
  // Whatever happened, the next one is due an interval from now: a failure backs off on top.
  nextHeartbeatAt_ = millis() + static_cast<uint32_t>(pollIntervalS_) * 1000u;
  if (!ok) {
    noteFailure(server_->lastCall());
    return;
  }

  counters_.heartbeats++;
  lastHeartbeatAtMillis_ = millis();
  everHeartbeated_ = true;
  int32_t difference = 0;
  const bool moved = clock_->takeServerTime(reply.serverTimeUnix, difference);
  lastHeartbeatAtEpoch_ = clock_->now();
  if (moved) LOG_W("clock: corrected by %ld s from the server's time", static_cast<long>(difference));

  if (reply.budget.maxTransactionsPerMinute != budget_.maxTransactionsPerMinute ||
      reply.budget.minGapSeconds != budget_.minGapSeconds) {
    budget_ = reply.budget;
    jobs_->setBudget(budget_);
    LOG_I("server: budget in force: %d/min, gap %d s", budget_.maxTransactionsPerMinute, budget_.minGapSeconds);
  }
  if (reply.pollIntervalS > 0 && reply.pollIntervalS != pollIntervalS_) {
    pollIntervalS_ = reply.pollIntervalS;
    LOG_I("server: poll interval %d s", pollIntervalS_);
  }
  nextHeartbeatAt_ = millis() + static_cast<uint32_t>(pollIntervalS_) * 1000u;
  lastJobsWaiting_ = reply.jobsWaiting;
  if (reply.jobsWaiting) claimDue_ = true;
  LOG_D("server: heartbeat reply: serverTime %s, budget %d/min gap %d s, poll %d s, jobs waiting %s",
        reply.serverTime, reply.budget.maxTransactionsPerMinute, reply.budget.minGapSeconds, reply.pollIntervalS,
        reply.jobsWaiting ? "yes" : "no");
  succeeded();
}

void AgentLoop::doPendingResults() {
  const int before = jobs_->queueDepth();
  jobs_->flushResults();
  const int after = jobs_->queueDepth();
  if (after < before) {
    counters_.resultsPosted += static_cast<uint32_t>(before - after);
    LOG_I("jobs: %d result(s) that waited went through; %d still waiting", before - after, after);
  }
  if (after == 0) {
    succeeded();
    return;
  }
  // Still there: the same backoff the heartbeat uses, so the buffer is not hammered.
  noteFailure(server_->lastCall());
}

uint32_t AgentLoop::epochOf(uint32_t heardAtMillis, uint32_t nowMs) const {
  const uint32_t now = clock_->now();
  const uint32_t agoS = (nowMs - heardAtMillis + 500u) / 1000u;
  return agoS < now ? now - agoS : now;
}

size_t AgentLoop::buildBatch(uint32_t nowMs, char* out, size_t cap) {
  // A batch that is being retried is the same batch: the same items and the same client id, or
  // the server's de-duplication would take the id, store nothing, and the extra items would be
  // consumed unsent.
  size_t count = batchCount_ > 0 ? batchCount_ : ring_.size();
  if (count > kObservationBatchSize) count = kObservationBatchSize;
  if (count > ring_.size()) count = ring_.size();
  for (size_t i = 0; i < count; i++) {
    const HeardAdvert& a = ring_.at(i);
    ObservationItem& item = g_items[i];
    item = ObservationItem();
    item.at = epochOf(a.heardAtMillis, nowMs);
    item.kind = "advert";
    toHex(a.publicKey, kPublicKeyLen, g_keys[i]);
    item.publicKey = g_keys[i];

    const int size = a.pathHashSize == 0 ? 1 : a.pathHashSize;
    int reported = a.pathByteLen / size;
    if (reported > kMaxHopsReported) reported = kMaxHopsReported;
    for (int h = 0; h < reported; h++) {
      toHex(&a.pathBytes[h * size], static_cast<size_t>(size), g_hops[i][h]);
      g_hopPtrs[i][h] = g_hops[i][h];
    }
    // The path goes only when all of it is there; the hop count is what the packet said either
    // way, so a longer path than this ring keeps is still reported as its length.
    item.hasPath = reported == a.hops;
    item.path = g_hopPtrs[i];
    item.pathCount = reported;
    item.hasHops = true;
    item.hops = a.hops;

    if (a.snrQ4 != kNoSnrQ4) {
      item.hasSnr = true;
      item.snr = static_cast<double>(a.snrQ4) / 4.0;
    }
    if (a.rssi != kNoRssi) {
      item.hasRssi = true;
      item.rssi = a.rssi;
    }

    const char* role = roleName(a.advType);
    if (a.name[0] != '\0' || a.hasPosition || role != nullptr) {
      item.hasFields = true;
      item.fields.name = a.name[0] != '\0' ? a.name : nullptr;
      item.fields.hasPosition = a.hasPosition;
      item.fields.latE6 = a.latE6;
      item.fields.lonE6 = a.lonE6;
      item.fields.role = role;
    }
  }
  batchCount_ = count;
  if (count == 0) return 0;
  if (batchId_[0] == '\0') {
    // "obs-" and 32 hex, the shape the .NET observer's Guid gives its batches.
    std::snprintf(batchId_, sizeof batchId_, "obs-%08" PRIx32 "%08" PRIx32 "%08" PRIx32 "%08" PRIx32, esp_random(),
                  esp_random(), esp_random(), esp_random());
  }
  return writeObservationBatch(batchId_, g_items, static_cast<int>(count), out, cap);
}

void AgentLoop::doObservations() {
  const uint32_t now = millis();
  const size_t len = buildBatch(now, g_batchJson, sizeof g_batchJson);
  if (len == 0) {
    if (batchCount_ > 0) {
      LOG_E("observations: %u of them did not fit in %u bytes and were dropped",
            static_cast<unsigned>(batchCount_), static_cast<unsigned>(sizeof g_batchJson));
      ring_.consume(batchCount_);
    }
    batchCount_ = 0;
    batchId_[0] = '\0';
    return;
  }

  int accepted = 0;
  bool duplicate = false;
  const bool ok = server_->postObservations(g_batchJson, len, accepted, duplicate);
  if (ok) {
    counters_.batches++;
    counters_.observationsPosted += static_cast<uint32_t>(batchCount_);
    lastBatchAtEpoch_ = clock_->now();
    lastBatchCount_ = static_cast<int>(batchCount_);
    LOG_I("observations: %u posted, %d accepted%s", static_cast<unsigned>(batchCount_), accepted,
          duplicate ? " (the server had this batch already)" : "");
    ring_.consume(batchCount_);
    batchCount_ = 0;
    batchId_[0] = '\0';
    succeeded();
    return;
  }

  if (noteFailure(server_->lastCall()) == ServerAction::Discard) {
    counters_.observationsRefused++;
    LOG_W("observations: the server refused the batch for good; %u dropped", static_cast<unsigned>(batchCount_));
    ring_.consume(batchCount_);
    batchCount_ = 0;
    batchId_[0] = '\0';
    succeeded();
    return;
  }
  // Anything else: the batch is kept with its client id for the next attempt, and the server
  // de-duplicates on that id - so a batch it took whose answer was lost costs nothing twice.
}

void AgentLoop::doJobs() {
  // One claim per heartbeat that said work was waiting. Whatever it hands over is run here and
  // now, one transaction at a time at the budget's pace, and every result is posted as its job
  // ends - which is why this call can take minutes at a budget of two a minute, and why the
  // console and the OLED are pumped through it (JobRunner::idle).
  claimDue_ = false;
  const int ran = jobs_->claimAndRun();
  if (ran < 0) {
    counters_.claimFailures++;
    noteFailure(server_->lastCall());
    return;
  }
  succeeded();
}

void AgentLoop::report(Stream& out) const {
  const uint32_t now = millis();
  char when[24];

  if (halted_) {
    out.println("state     halted: the server refused the token (http 401). Set meshmaster.token again.");
  } else if (!server_->configured()) {
    out.println("state     not configured: meshmaster.token is needed");
  } else if (!wifi_->connected()) {
    out.println("state     waiting for wifi");
  } else if (!elapsed(now, holdUntil_)) {
    out.printf("state     holding off for %lu s\r\n", static_cast<unsigned long>((holdUntil_ - now) / 1000));
  } else {
    out.println("state     running");
  }

  if (everHeartbeated_) {
    formatClock(lastHeartbeatAtEpoch_, when, sizeof when);
    out.printf("heartbeat last %s (%lu s ago), %lu answered; jobs waiting %s\r\n", when,
               static_cast<unsigned long>((now - lastHeartbeatAtMillis_) / 1000),
               static_cast<unsigned long>(counters_.heartbeats), lastJobsWaiting_ ? "yes" : "no");
  } else {
    out.println("heartbeat none answered yet");
  }
  if (heartbeatScheduled_) {
    const int32_t due = static_cast<int32_t>(nextHeartbeatAt_ - now);
    out.printf("next      in %ld s (poll %d s, budget %d/min gap %d s)\r\n",
               static_cast<long>(due > 0 ? due / 1000 : 0), pollIntervalS_, budget_.maxTransactionsPerMinute,
               budget_.minGapSeconds);
  } else {
    out.println("next      not scheduled");
  }

  if (counters_.batches > 0) {
    formatClock(lastBatchAtEpoch_, when, sizeof when);
    out.printf("batch     last %s, %d item(s); %lu batch(es), %lu observation(s) posted\r\n", when, lastBatchCount_,
               static_cast<unsigned long>(counters_.batches), static_cast<unsigned long>(counters_.observationsPosted));
  } else {
    out.println("batch     none posted yet");
  }
  out.printf("pending   %u of %u in the ring, %lu dropped, %lu heard in all\r\n",
             static_cast<unsigned>(ring_.size()), static_cast<unsigned>(ObservationRing::kCapacity),
             static_cast<unsigned long>(ring_.dropped()), static_cast<unsigned long>(ring_.heard()));
  out.printf("jobs      %lu claim(s), %lu job(s) run, %lu claim(s) unanswered; `jobs` prints the last ten\r\n",
             static_cast<unsigned long>(jobs_->claims()), static_cast<unsigned long>(jobs_->ran()),
             static_cast<unsigned long>(counters_.claimFailures));
  out.printf("errors    overheard %lu, wifiReconnects %lu, httpFailures %lu, observationsDropped %lu, "
             "resultsDropped %lu, reboots %lu; `errors` has the rest\r\n",
             static_cast<unsigned long>(mesh_->overheard()), static_cast<unsigned long>(counters_.wifiReconnects),
             static_cast<unsigned long>(counters_.httpFailures), static_cast<unsigned long>(ring_.dropped()),
             static_cast<unsigned long>(jobs_->resultsDropped()), static_cast<unsigned long>(selfReboots()));
  out.printf("requests  %u in the last minute of the 120 the policy allows, %lu held back; %lu batch(es) refused\r\n",
             static_cast<unsigned>(server_->limiter().inWindow(now)), static_cast<unsigned long>(server_->limiter().held()),
             static_cast<unsigned long>(counters_.observationsRefused));
}

void AgentLoop::reportErrors(Stream& out) const {
  // The six the heartbeat carries, first and by their wire names, so that a figure on the exit
  // node's page can be found here without a translation.
  out.println("heartbeat the six counters the heartbeat reports, by the names the server shows");
  out.printf("  overheard            %lu  (mesh messages counted and dropped unread - a busy channel, not a fault)\r\n",
             static_cast<unsigned long>(mesh_->overheard()));
  out.printf("  wifiReconnects       %lu  (%u link(s) in all, %u drop(s))\r\n",
             static_cast<unsigned long>(counters_.wifiReconnects), static_cast<unsigned>(wifi_->connects()),
             static_cast<unsigned>(wifi_->drops()));
  out.printf("  httpFailures         %lu  (%lu in a row now; %lu reboot(s) away from restarting)\r\n",
             static_cast<unsigned long>(counters_.httpFailures),
             static_cast<unsigned long>(counters_.consecutiveFailures),
             static_cast<unsigned long>(counters_.consecutiveFailures >= kRebootAfterFailures
                                            ? 0
                                            : kRebootAfterFailures - counters_.consecutiveFailures));
  out.printf("  observationsDropped  %lu  (adverts the ring overwrote; %u of %u waiting, %lu heard in all)\r\n",
             static_cast<unsigned long>(ring_.dropped()), static_cast<unsigned>(ring_.size()),
             static_cast<unsigned>(ObservationRing::kCapacity), static_cast<unsigned long>(ring_.heard()));
  out.printf("  resultsDropped       %lu  (job results the buffer overwrote; %d waiting, %lu posted late)\r\n",
             static_cast<unsigned long>(jobs_->resultsDropped()), jobs_->queueDepth(),
             static_cast<unsigned long>(counters_.resultsPosted));
  out.printf("  reboots              %lu  (restarts this firmware asked for; `reboots` says why)\r\n",
             static_cast<unsigned long>(selfReboots()));
  out.println("beside    counters the server does not get, for a person at the port");
  out.printf("  claims unanswered    %lu\r\n", static_cast<unsigned long>(counters_.claimFailures));
  out.printf("  results not taken    %lu  (at the first attempt)\r\n",
             static_cast<unsigned long>(jobs_->postFailures()));
  out.printf("  batches refused      %lu  (the server would not have them, ever)\r\n",
             static_cast<unsigned long>(counters_.observationsRefused));
  out.printf("  observations posted  %lu in %lu batch(es)\r\n",
             static_cast<unsigned long>(counters_.observationsPosted), static_cast<unsigned long>(counters_.batches));
  out.printf("  heartbeats answered  %lu\r\n", static_cast<unsigned long>(counters_.heartbeats));
  out.printf("  undecodable adverts  %lu\r\n", static_cast<unsigned long>(mesh_->counters().advertsDropped));
}

void AgentLoop::describe(char* out, size_t cap) const {
  if (halted_) {
    std::snprintf(out, cap, "halted: http 401, the token was refused");
    return;
  }
  if (!everHeartbeated_) {
    std::snprintf(out, cap, "no heartbeat answered yet; %u advert(s) waiting", static_cast<unsigned>(ring_.size()));
    return;
  }
  char when[24];
  formatClock(lastHeartbeatAtEpoch_, when, sizeof when);
  std::snprintf(out, cap, "last at %s, %lu answered, poll %d s; %lu observation(s) posted, %u waiting, %lu dropped",
                when, static_cast<unsigned long>(counters_.heartbeats), pollIntervalS_,
                static_cast<unsigned long>(counters_.observationsPosted), static_cast<unsigned>(ring_.size()),
                static_cast<unsigned long>(ring_.dropped()));
}

void AgentLoop::oledLine(char* out, size_t cap) const {
  // Drawn by Oled::render, which has no reason to know whether begin() has run here yet: before
  // it has, server_ and wifi_ are null and there is nothing to say but that.
  if (server_ == nullptr) {
    std::snprintf(out, cap, "starting");
    return;
  }
  if (halted_) {
    std::snprintf(out, cap, "token revoked/wrong");
    return;
  }
  // Which of the two is missing, in one word each, because that is the question somebody
  // standing at the window is asking (task 06). Line one already has the WiFi's own detail.
  if (wifi_ != nullptr && !wifi_->connected() && server_->configured()) {
    std::snprintf(out, cap, "no wifi: no server");
    return;
  }
  if (counters_.consecutiveFailures > 0 && server_->configured()) {
    const int32_t left = static_cast<int32_t>(holdUntil_ - millis());
    std::snprintf(out, cap, "no server: retry %lus", static_cast<unsigned long>(left > 0 ? left / 1000 : 0));
    return;
  }
  if (!everHeartbeated_) {
    server_->oledLine(out, cap);
    return;
  }
  char when[24];
  formatClock(lastHeartbeatAtEpoch_, when, sizeof when);
  // "\xfa" is the middle dot of code page 437, which is the font the OLED is drawn in
  // (MeshCore's SSD1306Display::startFrame calls Adafruit_GFX::cp437(true)).
  std::snprintf(out, cap, "hb %.5s \xfa %lu adverts", when, static_cast<unsigned long>(mesh_->counters().adverts));
}

}  // namespace mm
