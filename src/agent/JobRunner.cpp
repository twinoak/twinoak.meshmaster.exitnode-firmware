#include "JobRunner.h"

#include <Arduino.h>
#include <esp_system.h>

#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "Iso8601.h"
#include "JobResults.h"
#include "Log.h"

namespace mm {

namespace {

// A result's free text, bounded here rather than at the envelope: 4 000 characters is what API §3
// allows and a kilobyte is more than any of these four jobs has to say. When it fills, the oldest
// half goes - what a job did last is what a reader of its page wants.
class JobLog {
 public:
  void clear() {
    len_ = 0;
    buf_[0] = '\0';
  }

  void add(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    if (len_ + 2 >= sizeof buf_) drop();
    if (len_ > 0) {
      buf_[len_++] = ';';
      buf_[len_++] = ' ';
      buf_[len_] = '\0';
    }
    va_list args;
    va_start(args, fmt);
    const int n = std::vsnprintf(&buf_[len_], sizeof buf_ - len_, fmt, args);
    va_end(args);
    if (n > 0) {
      len_ += static_cast<size_t>(n) < sizeof buf_ - len_ ? static_cast<size_t>(n) : sizeof buf_ - len_ - 1;
    }
    buf_[len_] = '\0';
  }

  const char* text() const { return buf_; }

 private:
  void drop() {
    const size_t half = sizeof buf_ / 2;
    std::memmove(buf_, buf_ + half, len_ - half + 1);
    len_ -= half;
  }

  char buf_[1024] = {0};
  size_t len_ = 0;
};

// The scratch every job shares. Static, not automatic: the Arduino loop task has 8 KB of stack
// and a neighbour list alone is more than that.
constexpr size_t kDataCap = 3584;   // 54 neighbours at about 55 characters each, and room over
constexpr size_t kBodyCap = 5632;   // the data, the envelope and the log
ClaimedJob g_jobs[kClaimMax];
char g_data[kDataCap];
char g_body[kBodyCap];
char g_pending[kBodyCap];
JobLog g_log;
Neighbour g_neighbours[kMaxNeighbours];
LppReading g_readings[kMaxLppReadings];
TraceHop g_hops[kMaxTraceHops + 1];
char g_pathHops[kMaxClaimPathHops][8];
const char* g_pathPtrs[kMaxClaimPathHops];
uint8_t g_pathBytes[kMaxClaimPathHops * 3];
uint8_t g_roundTrip[MAX_PATH_SIZE];

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// The claim's path hops as bytes, and the hash size they are written at (the hop hashes are hex,
// so their length says it). Returns the hop count, or 0 when the path cannot be read.
uint8_t pathToBytes(const ClaimedJob& job, uint8_t* out, size_t cap, uint8_t& hashSize) {
  hashSize = 0;
  if (!job.hasPath || job.pathCount <= 0) return 0;
  const size_t width = std::strlen(job.pathHops[0]);
  if (width == 0 || width % 2 != 0 || width > 6) return 0;
  hashSize = static_cast<uint8_t>(width / 2);
  size_t n = 0;
  for (int i = 0; i < job.pathCount; i++) {
    if (std::strlen(job.pathHops[i]) != width || n + hashSize > cap) return 0;
    for (size_t b = 0; b < hashSize; b++) {
      const int hi = hexValue(job.pathHops[i][b * 2]);
      const int lo = hexValue(job.pathHops[i][b * 2 + 1]);
      if (hi < 0 || lo < 0) return 0;
      out[n++] = static_cast<uint8_t>((hi << 4) | lo);
    }
  }
  return static_cast<uint8_t>(job.pathCount);
}

}  // namespace

void JobRunner::begin(AgentMesh& mesh, AgentClient& server, AgentClock& clock, void (*pump)()) {
  mesh_ = &mesh;
  server_ = &server;
  clock_ = &clock;
  pump_ = pump;
}

void JobRunner::setIdentity(const char* publicKeyHex) { publicKey_ = publicKeyHex; }

void JobRunner::randomBytes(uint8_t* out, size_t len) {
  for (size_t i = 0; i < len; i += 4) {
    const uint32_t r = esp_random();
    for (size_t b = 0; b < 4 && i + b < len; b++) out[i + b] = static_cast<uint8_t>(r >> (8 * b));
  }
}

void JobRunner::idle(uint32_t ms) {
  const uint32_t deadline = millis() + ms;
  while (static_cast<int32_t>(millis() - deadline) < 0) {
    mesh_->loop();
    if (pump_ != nullptr) pump_();
    delay(1);
  }
}

bool JobRunner::leaseRanOut(const ClaimedJob& job) const {
  if (job.leaseUntil == 0 || !clock_->hasTime()) return false;
  return clock_->now() >= job.leaseUntil;
}

uint32_t JobRunner::requestWaitMs(const ClaimedJob& job) const {
  // The per-hop timeout scaled by the hops (EXIT-NODES §7); a flood waits as a three-hop
  // request would, because nobody knows how far it went.
  const int hops = mesh_->hasPath() ? mesh_->pathHopCount() : 3;
  return static_cast<uint32_t>(job.perHopMs) * static_cast<uint32_t>(hops + 1);
}

uint32_t JobRunner::loginWaitMs(const ClaimedJob& job) const {
  const uint32_t request = requestWaitMs(job);
  const uint32_t login = static_cast<uint32_t>(job.loginMs);
  return login > request ? login : request;
}

bool JobRunner::spend(const ClaimedJob& job, Context& ctx) {
  if (leaseRanOut(job)) return false;
  uint32_t wait = gate_.waitMs(millis());
  if (wait > 0) {
    // A budget of two a minute means most of a job is waiting. Never wait past the lease: the
    // server has moved the job on by then, and the air is better spent on the next one.
    if (clock_->hasTime() && job.leaseUntil != 0 && clock_->now() + (wait + 999) / 1000 > job.leaseUntil) {
      g_log.add("the budget's next slot is past the lease");
      return false;
    }
    g_log.add("waiting %lu s for the airtime budget", static_cast<unsigned long>((wait + 999) / 1000));
    gate_.addWait(wait);
    idle(wait);
  }
  if (leaseRanOut(job)) return false;
  gate_.took(millis());
  ctx.transactions++;
  return true;
}

Outcome JobRunner::ensureSession(const ClaimedJob& job, Context& ctx) {
  if (job.credentialClass == CredentialClass::None) return Outcome::Ok;  // a trace asks nobody

  const uint32_t now = millis();
  if (const RepeaterSession* live = sessions_.get(job.repeaterKey, job.credentialClass, now)) {
    ctx.hasSession = true;
    ctx.sessionRole = live->role();
    ctx.sessionWasFresh = false;
    g_log.add("session reused (%s, %s, %lu s old)", credentialClassName(live->credentialClass), live->tier(),
              static_cast<unsigned long>((now - live->loggedInAtMs) / 1000));
    return Outcome::Ok;
  }

  for (int attempt = 1; attempt <= 2; attempt++) {
    if (!spend(job, ctx)) return Outcome::Timeout;
    uint32_t tag = 0;
    const SendResult sent = mesh_->sendAclLogin(tag);
    if (sent == SendResult::Failed) {
      g_log.add("login try %d: the packet could not be built", attempt);
      return Outcome::Failed;
    }
    if (!mesh_->awaitReply(tag, loginWaitMs(job), pump_)) {
      // Stock firmware answers a login it will not take with **silence** (`handleLoginReq`
      // returns 0), so this is both "the repeater is not there" and "our key is not in its ACL".
      // Two tries and then `unreachable`, which is what the .NET agent reports for the same.
      g_log.add("login (%s, %s) try %d: no reply", credentialClassName(job.credentialClass),
                sent == SendResult::Direct ? "direct" : "flood", attempt);
      continue;
    }
    const MeshReply& reply = mesh_->reply();
    LoginReply login;
    if (reply.kind != MeshReply::Kind::Response || !parseLoginReply(reply.data, reply.len, login)) {
      g_log.add("login try %d: refused", attempt);
      sessions_.invalidate(job.repeaterKey);
      return Outcome::Unauthorized;
    }
    const uint8_t permissions = login.hasAclPermissions ? login.aclPermissions : login.role();
    const RepeaterSession* stored = sessions_.put(job.repeaterKey, job.credentialClass, permissions, millis());
    ctx.hasSession = true;
    ctx.sessionRole = stored != nullptr ? stored->role() : login.role();
    ctx.sessionWasFresh = true;
    if (stored == nullptr || !stored->satisfies(job.credentialClass)) {
      // A login judged against the class it was for, the moment it comes back (task p3.5-41):
      // answered below what the job needs is the refusal it is, and the level travels in the
      // result so the server can tell a demoted entry from one that is gone.
      ctx.grantedBelowClass = true;
      ctx.grantedRole = login.role();
      g_log.add("logged in as %s, but this job needs %s", permissionName(login.role()),
                permissionName(roleNeededFor(job.credentialClass)));
      return Outcome::Unauthorized;
    }
    g_log.add("logged in as %s%s", permissionName(login.role()), login.legacy ? " (a legacy reply)" : "");
    return Outcome::Ok;
  }
  return Outcome::Unreachable;
}

Outcome JobRunner::request(const ClaimedJob& job, Context& ctx, const char* what, uint8_t reqType,
                           const uint8_t* data, uint8_t len) {
  for (int attempt = 1; attempt <= 2; attempt++) {
    if (!spend(job, ctx)) return Outcome::Timeout;
    const uint32_t started = millis();
    uint32_t tag = 0;
    const SendResult sent = mesh_->sendRequest(reqType, data, len, tag);
    if (sent == SendResult::Failed) {
      g_log.add("%s try %d: the packet could not be built", what, attempt);
      return Outcome::Failed;
    }
    if (mesh_->awaitReply(tag, requestWaitMs(job), pump_)) {
      ctx.hasRtt = true;
      ctx.rttMs = static_cast<int>(millis() - started);
      g_log.add("%s try %d: ok (%s, %lu ms)", what, attempt, sent == SendResult::Direct ? "direct" : "flood",
                static_cast<unsigned long>(ctx.rttMs));
      return Outcome::Ok;
    }
    g_log.add("%s try %d: no reply", what, attempt);
    if (attempt == 1 && !ctx.sessionWasFresh && ctx.hasSession) {
      // An old session and no answer: the repeater may have rebooted and forgotten us.
      sessions_.invalidate(job.repeaterKey);
      ctx.hasSession = false;
      g_log.add("no reply on a reused session: logging in again once");
      const Outcome again = ensureSession(job, ctx);
      if (again != Outcome::Ok) return again;
    }
  }
  return Outcome::Unreachable;
}

Outcome JobRunner::runStatus(const ClaimedJob& job, Context& ctx, size_t& dataLen) {
  const Outcome session = ensureSession(job, ctx);
  if (session != Outcome::Ok) return session;

  const Outcome got = request(job, ctx, "status request", AgentMesh::kReqTypeGetStatus, nullptr, 0);
  if (got != Outcome::Ok) return got;

  StatusReading reading;
  const MeshReply& reply = mesh_->reply();
  if (!parseRepeaterStats(reply.data, reply.len, reading)) {
    g_log.add("the status reply was %u bytes; a RepeaterStats is at least %u", static_cast<unsigned>(reply.len),
              static_cast<unsigned>(kRepeaterStatsMinLen));
    return Outcome::Failed;
  }

  char firmware[kMaxFirmwareVersionLen + 1] = {0};
  if (job.credentialClass == CredentialClass::Acl) {
    // The verify step of an association records the firmware it proved the ACL login on
    // (EXIT-NODES §4.1 step 5); the version string is only readable through the owner-info
    // request, one more transaction, so it is fetched for acl-class reads alone.
    if (request(job, ctx, "owner info (firmware)", AgentMesh::kReqTypeGetOwnerInfo, nullptr, 0) == Outcome::Ok) {
      const MeshReply& owner = mesh_->reply();
      parseOwnerInfoFirmware(owner.data, owner.len, firmware, sizeof firmware);
    } else {
      g_log.add("the firmware version was not read; the status itself stands");
    }
  }

  ErrorCounter errors[kMaxStatusErrors];
  const int errorCount = statusErrors(reading, errors, kMaxStatusErrors);
  dataLen = writeStatusResult(reading, errors, errorCount, firmware[0] != '\0' ? firmware : nullptr, g_data, kDataCap);
  return dataLen > 0 ? Outcome::Ok : Outcome::Failed;
}

Outcome JobRunner::runTelemetry(const ClaimedJob& job, Context& ctx, size_t& dataLen) {
  const Outcome session = ensureSession(job, ctx);
  if (session != Outcome::Ok) return session;

  // The request data of a telemetry read: the inverse permission mask (0 is everything the
  // sender may have) and four random bytes, for a packet hash nothing repeats.
  uint8_t data[8] = {0};
  randomBytes(&data[4], 4);
  const Outcome got = request(job, ctx, "telemetry request", AgentMesh::kReqTypeGetTelemetry, data, sizeof data);
  if (got != Outcome::Ok) return got;

  const MeshReply& reply = mesh_->reply();
  size_t undecodedFrom = 0;
  const int count = decodeLpp(reply.data, reply.len, g_readings, kMaxLppReadings, undecodedFrom);
  if (undecodedFrom < reply.len) {
    g_log.add("telemetry: %u byte(s) not decoded", static_cast<unsigned>(reply.len - undecodedFrom));
  }
  dataLen = writeTelemetryResult(g_readings, count, g_data, kDataCap);
  return dataLen > 0 ? Outcome::Ok : Outcome::Failed;
}

Outcome JobRunner::runNeighbours(const ClaimedJob& job, Context& ctx, size_t& dataLen) {
  const Outcome session = ensureSession(job, ctx);
  if (session != Outcome::Ok) return session;

  int collected = 0;
  int offset = 0;
  int total = 0;
  for (int page = 0; page < 8; page++) {
    // version 0, count, offset (u16), order_by, prefix length, four random bytes.
    uint8_t data[10] = {0};
    data[0] = 0;
    data[1] = kNeighboursPerPage;
    data[2] = static_cast<uint8_t>(offset & 0xFF);
    data[3] = static_cast<uint8_t>((offset >> 8) & 0xFF);
    data[4] = 0;  // newest to oldest
    data[5] = kNeighbourPrefixLen;
    randomBytes(&data[6], 4);

    char what[40];
    std::snprintf(what, sizeof what, "neighbours page at %d", offset);
    const Outcome got = request(job, ctx, what, AgentMesh::kReqTypeGetNeighbours, data, sizeof data);
    if (got != Outcome::Ok) {
      // A page that never came back after some already did is still a reading: the server gets
      // what was heard rather than nothing at all.
      if (collected == 0) return got;
      g_log.add("the list stops at %d of %d", collected, total);
      break;
    }

    const MeshReply& reply = mesh_->reply();
    NeighboursPage p;
    if (!parseNeighboursPage(reply.data, reply.len, kNeighbourPrefixLen, p)) {
      g_log.add("a neighbours page did not parse (%u bytes)", static_cast<unsigned>(reply.len));
      if (collected == 0) return Outcome::Failed;
      break;
    }
    total = p.total;
    if (p.count == 0) break;
    for (int i = 0; i < p.count && collected < kMaxNeighbours; i++) g_neighbours[collected++] = p.entries[i];
    offset += p.count;
    if (offset >= total || collected >= kMaxNeighbours) break;
  }
  if (collected < total) g_log.add("%d of the repeater's %d neighbour(s) reported", collected, total);

  dataLen = writeNeighboursResult(g_neighbours, collected, g_data, kDataCap);
  return dataLen > 0 ? Outcome::Ok : Outcome::Failed;
}

Outcome JobRunner::runTrace(const ClaimedJob& job, Context& ctx, size_t& dataLen) {
  uint8_t hashSize = 0;
  const uint8_t hops = pathToBytes(job, g_pathBytes, sizeof g_pathBytes, hashSize);
  if (hops == 0) {
    g_log.add("no path is known to this repeater: a trace needs one; run discover first");
    return Outcome::Unreachable;
  }
  if (!isTraceableHashSize(hashSize)) {
    // A trace packet states its hash size as an exponent, 1 << (flags & 3), which has no value
    // three: a path of three-byte hashes cannot be traced at all, by protocol and not by
    // omission (task p3.5-42). Say so instead of sending a frame nothing on the path can follow.
    g_log.add("this path is built of %u-byte hashes, and a trace packet can carry only 1, 2, 4 or 8",
              static_cast<unsigned>(hashSize));
    return Outcome::Unsupported;
  }

  // The round trip: out along the path, the repeater itself, and back the way it came.
  size_t n = 0;
  for (int i = 0; i < hops; i++) {
    std::memcpy(&g_roundTrip[n], &g_pathBytes[i * hashSize], hashSize);
    n += hashSize;
  }
  for (int i = 0; i < hashSize; i++) {
    const int hi = hexValue(job.repeaterKey[i * 2]);
    const int lo = hexValue(job.repeaterKey[i * 2 + 1]);
    if (hi < 0 || lo < 0) return Outcome::Failed;
    g_roundTrip[n++] = static_cast<uint8_t>((hi << 4) | lo);
  }
  for (int i = hops - 1; i >= 0; i--) {
    std::memcpy(&g_roundTrip[n], &g_pathBytes[i * hashSize], hashSize);
    n += hashSize;
  }
  if (n > MAX_PATH_SIZE) {
    g_log.add("the round trip is %u bytes, more than a trace packet's path", static_cast<unsigned>(n));
    return Outcome::Unsupported;
  }

  for (int attempt = 1; attempt <= 2; attempt++) {
    if (!spend(job, ctx)) return Outcome::Timeout;
    const uint32_t started = millis();
    uint32_t tag = 0;
    if (mesh_->sendTrace(g_roundTrip, static_cast<uint8_t>(n), hashSize, tag) == SendResult::Failed) {
      g_log.add("trace try %d: the packet could not be built", attempt);
      return Outcome::Failed;
    }
    if (!mesh_->awaitReply(tag, requestWaitMs(job), pump_)) {
      g_log.add("trace try %d: no reply", attempt);
      continue;
    }
    const MeshReply& reply = mesh_->reply();
    if (reply.kind != MeshReply::Kind::Trace) {
      g_log.add("trace try %d: the reply was not a trace", attempt);
      return Outcome::Failed;
    }
    ctx.hasRtt = true;
    ctx.rttMs = static_cast<int>(millis() - started);

    TraceReading trace;
    parseTrace(reply.traceFlags, reply.traceHashes, reply.traceHashBytes, reply.traceSnrs, reply.traceSnrCount,
               trace);
    int count = trace.count;
    if (count > kMaxTraceHops) count = kMaxTraceHops;
    for (int i = 0; i < count; i++) g_hops[i] = trace.hops[i];
    // The whole round trip, and this exit node's own hash last with the SNR it measured on the
    // packet coming home: the server takes the outward half of it (task 24).
    if (publicKey_ != nullptr && count < kMaxTraceHops + 1) {
      TraceHop& self = g_hops[count];
      self = TraceHop();
      std::snprintf(self.prefix, sizeof self.prefix, "%.*s", trace.hashSize * 2, publicKey_);
      self.hasSnr = true;
      self.snr = reply.traceFinalSnr;
      count++;
    }
    g_log.add("trace: %d hop(s) of the round trip, %lu ms", count, static_cast<unsigned long>(ctx.rttMs));
    dataLen = writeTraceResult(g_hops, count, g_data, kDataCap);
    return dataLen > 0 ? Outcome::Ok : Outcome::Failed;
  }
  return Outcome::Unreachable;
}

Outcome JobRunner::runJob(const ClaimedJob& job, Context& ctx, size_t& dataLen) {
  dataLen = 0;
  char why[160] = {0};
  if (!canRun(job, why, sizeof why)) {
    g_log.add("%s", why);
    return Outcome::Unsupported;
  }

  uint8_t hashSize = 0;
  const uint8_t hops = pathToBytes(job, g_pathBytes, sizeof g_pathBytes, hashSize);
  if (!mesh_->beginJob(job.repeaterKey, hops > 0 ? g_pathBytes : nullptr, hops, hashSize)) {
    g_log.add("the repeater's public key is not 64 hex characters");
    return Outcome::Failed;
  }
  g_log.add("path %s", hops > 0 ? "from the server" : "unknown: the mesh floods");

  if (std::strcmp(job.type, "status") == 0) return runStatus(job, ctx, dataLen);
  if (std::strcmp(job.type, "telemetry") == 0) return runTelemetry(job, ctx, dataLen);
  if (std::strcmp(job.type, "neighbours") == 0) return runNeighbours(job, ctx, dataLen);
  if (std::strcmp(job.type, "trace") == 0) return runTrace(job, ctx, dataLen);
  return Outcome::Unsupported;
}

bool JobRunner::post(long long jobId, const char* body, size_t len) {
  if (server_->postJobResult(jobId, body, len)) return true;
  postFailures_++;
  // Kept with its client id and retried: the server de-duplicates on that id, and a late result
  // is still taken (API §3).
  if (len < sizeof g_pending) {
    std::memcpy(g_pending, body, len);
    g_pending[len] = '\0';
    pendingJobId_ = jobId;
    pendingLen_ = len;
    LOG_W("jobs: the result of job %lld was not taken; it is kept and sent again", jobId);
  }
  return false;
}

void JobRunner::flushPending() {
  if (pendingLen_ == 0) return;
  const long long jobId = pendingJobId_;
  if (!server_->postJobResult(jobId, g_pending, pendingLen_)) {
    postFailures_++;
    return;
  }
  LOG_I("jobs: the kept result of job %lld went through", jobId);
  pendingLen_ = 0;
  pendingJobId_ = 0;
  for (int i = 0; i < recentCount_; i++) {
    if (recent_[i].id == jobId) recent_[i].posted = true;
  }
}

void JobRunner::remember(const ClaimedJob& job, Outcome outcome, int transactions, bool posted) {
  for (int i = kRemembered - 1; i > 0; i--) recent_[i] = recent_[i - 1];
  Record& r = recent_[0];
  r = Record();
  r.id = job.id;
  std::snprintf(r.type, sizeof r.type, "%s", job.type);
  r.outcome = outcome;
  r.transactions = transactions;
  r.atEpoch = clock_->now();
  r.posted = posted;
  if (recentCount_ < kRemembered) recentCount_++;
}

int JobRunner::claimAndRun() {
  flushPending();

  int count = 0;
  if (!server_->claimJobs(g_jobs, kClaimMax, count)) return -1;
  claims_++;
  if (count == 0) {
    LOG_D("jobs: the claim came back with nothing");
    return 0;
  }
  LOG_I("jobs: %d claimed", count);

  for (int i = 0; i < count; i++) {
    const ClaimedJob& job = g_jobs[i];
    const uint32_t startedAt = clock_->now();
    g_log.clear();
    g_log.add("job %lld %s for repeater %lld \"%s\" (%s)", job.id, job.type, job.repeaterId, job.repeaterLabel,
              credentialClassName(job.credentialClass));
    for (int g = 0; g < job.groupCount; g++) {
      // The other jobs of this claim for the same repeater: they share one login (EXIT-NODES §8),
      // which on this board is the session cache doing it rather than anything this loop does.
      if (i + 1 < count && sharesLogin(job, g_jobs[i + 1])) {
        g_log.add("grouped with job %lld: one login for the pair", job.groupWith[g]);
        break;
      }
    }
    LOG_I("jobs: job %lld %s for repeater %lld \"%s\" (%s)", job.id, job.type, job.repeaterId, job.repeaterLabel,
          credentialClassName(job.credentialClass));

    Context ctx;
    ctx.job = &job;
    size_t dataLen = 0;
    Outcome outcome = runJob(job, ctx, dataLen);

    // A login answered below the class carries the tier it was answered at, for a type that has
    // no result of its own to say it (task p4.1-60).
    if (dataLen == 0 && outcome == Outcome::Unauthorized && ctx.grantedBelowClass) {
      dataLen = writeLoginBelowClassResult(permissionName(ctx.grantedRole),
                                           permissionName(roleNeededFor(job.credentialClass)), g_data, kDataCap);
    }

    // The path the mesh holds *after* the exchange, which is the claim's own unless a flooded
    // reply taught a better one on the way (EXIT-NODES §3: every result reports what path was
    // used). Read before the job is closed, because closing it forgets the peer.
    const bool hadPath = mesh_->hasPath();
    const int pathCount = mesh_->describePath(g_pathHops, kMaxClaimPathHops);
    for (int h = 0; h < pathCount; h++) g_pathPtrs[h] = g_pathHops[h];
    mesh_->endJob();

    char clientId[48];
    std::snprintf(clientId, sizeof clientId, "%lld-%08" PRIx32 "%08" PRIx32 "%08" PRIx32, job.id, esp_random(),
                  esp_random(), esp_random());

    JobReport report;
    report.clientId = clientId;
    report.outcome = outcome;
    report.startedAt = startedAt;
    report.finishedAt = clock_->now();
    // No path known is not the same as a direct neighbour: the first leaves both fields out, as
    // the .NET runner does for a contact the companion has no path for; the second sends an
    // empty array and a zero.
    report.hasPathUsed = hadPath;
    report.pathUsed = g_pathPtrs;
    report.pathCount = pathCount;
    report.hasHops = hadPath;
    report.hops = pathCount;
    report.hasRttMs = ctx.hasRtt;
    report.rttMs = ctx.rttMs;
    report.transactions = ctx.transactions;
    report.data = dataLen > 0 ? g_data : nullptr;
    report.log = g_log.text();

    LOG_I("jobs: job %lld: %s after %d transaction(s)", job.id, outcomeName(outcome), ctx.transactions);
    const size_t len = writeJobResult(report, g_body, kBodyCap);
    bool posted = false;
    if (len == 0) {
      LOG_E("jobs: the result of job %lld did not fit in %u bytes", job.id, static_cast<unsigned>(kBodyCap));
    } else {
      posted = post(job.id, g_body, len);
    }
    ran_++;
    remember(job, outcome, ctx.transactions, posted);
  }
  return count;
}

void JobRunner::report(Stream& out) const {
  out.printf("budget    %d/min, gap %d s; %lu transaction(s) sent, %lu s waited for the budget\r\n",
             gate_.budget().maxTransactionsPerMinute, gate_.budget().minGapSeconds,
             static_cast<unsigned long>(gate_.transmissions()), static_cast<unsigned long>(gate_.totalWaitMs() / 1000));
  out.printf("claims    %lu made, %lu job(s) run, %lu result(s) not taken first time\r\n",
             static_cast<unsigned long>(claims_), static_cast<unsigned long>(ran_),
             static_cast<unsigned long>(postFailures_));
  out.printf("types     status, telemetry, neighbours, trace\r\n");
  if (pendingLen_ > 0) out.printf("pending   the result of job %lld is waiting to be sent\r\n", pendingJobId_);
  if (recentCount_ == 0) {
    out.println("last ten  none yet");
    return;
  }
  out.println("last ten  newest first: id, type, outcome, transactions, when");
  char when[24];
  for (int i = 0; i < recentCount_; i++) {
    const Record& r = recent_[i];
    formatClock(r.atEpoch, when, sizeof when);
    out.printf("  %-8lld %-11s %-13s %d tx  %s%s\r\n", r.id, r.type, outcomeName(r.outcome), r.transactions, when,
               r.posted ? "" : "  (not posted)");
  }
}

void JobRunner::describe(char* out, size_t cap) const {
  if (ran_ == 0) {
    std::snprintf(out, cap, "none run yet; budget %d/min gap %d s", gate_.budget().maxTransactionsPerMinute,
                  gate_.budget().minGapSeconds);
    return;
  }
  const Record& r = recent_[0];
  std::snprintf(out, cap, "%lu run, %lu transaction(s); last job %lld %s %s after %d tx", static_cast<unsigned long>(ran_),
                static_cast<unsigned long>(gate_.transmissions()), r.id, r.type, outcomeName(r.outcome),
                r.transactions);
}

}  // namespace mm
