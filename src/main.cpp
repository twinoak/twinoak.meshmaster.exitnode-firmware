// The MeshMaster agent firmware: a Heltec V3 that boots, takes its settings over the USB serial
// console, joins WiFi, sets its clock, and is an exit node on meshmaster.dk - it heartbeats over
// verified TLS, posts the adverts its radio hears, and claims and answers the jobs of EXIT-NODES
// §7: the reads that log in as this board - status, telemetry, neighbours and trace (task 03) -
// discover, acl.set and config.read, which carry a repeater's password for the length of the
// job and nothing longer (task 04, agent/Credentials.h), and the six that change a repeater
// - config.apply, cli, advert.request, clock.sync, password.set, reboot - each one send per
// command (task 05, agent/ConfigApply.h). What leaves the radio is one packet per
// transaction and nothing else (src/mesh/AgentMesh.h).
//
// The pieces, each in its own file. Since task p1.5-16 the portable half of the agent is a
// library of its own - `lib/agent-core/`, reached as `<agent/...>` - and what is left under
// `src/` is this board:
//   agent/      the TLS client and the root it verifies against (AgentClient.h, TlsRoots.h):
//               the WiFi end of the uplink seam, and all that is left here of agent/
//   settings/   the key table (host-portable) and the NVS store
//   console/    the line parser (host-portable) and the console that acts on it
//   net/        WiFi with backoff, the clock (SNTP, the server as fallback), and since task
//               p1.7-27 the MQTT client the observer's broker connections run on (esp-mqtt)
//   platform/   the Arduino-on-ESP32 end of the platform seam: millis, delay, random, the heap,
//               the reboot for health
//   mesh/       the mesh in companion role: hears adverts, forwards nothing, and speaks a job
//   ui/         the OLED's four lines, as a pure function over a struct this file fills
//   SerialLog.h where a log line goes on this board: the serial port and the 4 kB ring `dump`
//               prints it back out of (the formatting is the library's, agent/Log.h)
//   Health.h    the task watchdog, the deliberate reboot on a wedged stack, and why this board
//               last restarted - kept in RTC memory, printed by `reboots` (task 06)
//
// and the library is the job logic itself: the envelopes and the reply structs, the observation
// ring, the airtime budget, the session cache, the job runner and the loop that drives them, on
// the three seams of task p1.5-15 (IMesh, IUplink, IPlatform) which this file wires to the real
// things below.

#include <Arduino.h>
#include <SPIFFS.h>
#include <esp_system.h>

#include <cinttypes>
#include <cstdio>
#include <cstring>

#include <agent/AgentLoop.h>
#include <agent/Iso8601.h>
#include <agent/JobRunner.h>
#include <agent/Log.h>
#include <agent/LogRing.h>

#include "mesh/MeshCoreHeaders.h"

#include "Health.h"
#include "SerialLog.h"
#include "agent/AgentClient.h"
#include "agent/TlsRoots.h"
#include "console/Console.h"
#include "console/StreamOut.h"
#include "mesh/AgentMesh.h"
#include "net/Clock.h"
#include "net/MqttClient.h"
#include "net/WifiLink.h"
#include "observer/Observer.h"
#include "platform/BootChoice.h"
#include "platform/Esp32Platform.h"
#include "platform/OtaSlot.h"
#include "platform/TableBytes.h"
#include "platform/TableFlash.h"
#include "platform/TlsHeap.h"
#include "settings/Store.h"
#include "update/Ask.h"
#include "update/Fetch.h"
#include "update/FetchSeams.h"
#include "update/ImageDescriptor.h"
#include "update/SlotRecord.h"
#include "update/Trial.h"
#include "update/Trust.h"
#include "update/UpdateRecord.h"
#include "ui/DisplayLines.h"
#include "ui/Oled.h"
#include "version.h"

// ---- the objects ----------------------------------------------------------------------------
// board, radio_driver, rtc_clock, display and user_btn come from MeshCore's variants/heltec_v3
// (target.h); the rest is ours.

namespace {

mm::SettingsStore settings;
mm::WifiLink wifi;
mm::AgentClock agentClock;
mm::Esp32Platform platform(agentClock);
mm::AgentClient server;
mm::JobRunner jobs;  // the scratch every job shares is inside it (JobRunner::Scratch), as .bss
mm::AgentLoop agent;
mm::HeartbeatIdentity identity;  // what the heartbeat says about this board; refreshed on `set radio.*`
mm::Console console;
mm::Oled oled;
mm::Observer observer;  // the observer dialect over this board's frames (task p1.7-26) and its two broker connections (27)
mm::MqttClient mqttSlot1;  // the clients the observer's two slots run on: esp-mqtt, one task each, verified TLS
mm::MqttClient mqttSlot2;
// Fetching a release into the other application slot (task p1.15-59). The fetch is a step
// machine driven from loop(), one request a pass, so that a file of 1.4 MB does not starve the
// mesh, the console or the watchdog; `update fetch <id>` starts it and the steps go to the log,
// which on this board is this same serial port and the 4 kB ring `dump` prints.
mm::ServerRoutes releaseRoutes(server);
mm::SettingsRecord slotRecordStore(settings);
mm::update::Fetcher fetcher;
bool fetcherReady = false;
// Switching to a judged image, and the ten minutes it then has to prove itself (task p1.15-60).
// The machine is portable; its four seams are the three small classes below and the board's
// src/platform/BootChoice.cpp.
mm::update::Updater updater;
bool updaterReady = false;
// The ask meshmaster.dk makes in the reply to a heartbeat, and the steps it is taken in (task
// p1.15-61). It is the loop's `IUpdateProvider`: the heartbeat's `update` member comes from it,
// the reply's ask goes to it, and its one result is posted for it. The machine is portable; its
// six seams are task 59's record store, task 60's clock and restart, and the three small classes
// below.
mm::update::Asked asked;

uint32_t uplinkMemoryFailuresSeen = 0;  // AgentClient::memoryFailures() as last handed to the observer
uint32_t uplinkCallsSeen = 0;           // KeepAlive's call count as last handed to the observer's gate (task p1.8-31)

ArduinoMillis ms_clock;
StdRNG fast_rng;
SimpleMeshTables tables;
StaticPoolPacketManager packet_pool(16);
mm::AgentMesh the_mesh(radio_driver, ms_clock, fast_rng, rtc_clock, packet_pool, tables);

IdentityStore identity_store(SPIFFS, "/identity");  // where the companion firmware keeps it
bool radio_ok = false;
bool identity_ok = false;
bool display_ok = false;

// The identity as 64 lowercase hex, made once at boot: what the heartbeat sends as
// companionPublicKey and what `status` prints.
char public_key_hex[2 * PUB_KEY_SIZE + 1] = {0};

uint32_t clockForLog() { return agentClock.now(); }

// Where the fetch's step lines go: the log, at info, so that the port shows them as they happen
// and `dump` still has them afterwards. One line per step, never a header and never a token.
class LogTextOut final : public mm::TextOut {
 public:
  void write(const char* text, size_t len) override {
    for (size_t i = 0; i < len; i++) {
      if (text[i] == '\r' || text[i] == '\n') continue;
      if (held_ + 1 < sizeof(line_)) line_[held_++] = text[i];
    }
    // A println() ends the line; mm::TextOut always ends one with CR LF.
    if (len >= 2 && text[len - 1] == '\n') {
      line_[held_] = '\0';
      if (held_ > 0) mm::logf(mm::LogLevel::Info, "%s", line_);
      held_ = 0;
    }
  }

 private:
  char line_[256] = {0};
  size_t held_ = 0;
};

LogTextOut fetchLog;

// The sector that holds no table, and what is done about it (task p1.16-63). `addSecondSlot` tries
// the new table three times and then puts the old one back three times, and only when neither
// holds does this start: the old table once a minute, for as long as this firmware runs, because a
// board that reboots before one holds needs a merged image over USB and loses its key, its token
// and its WiFi with it. `update` and `status` say so in their first lines while it is so.
mm::TableKeeper tableKeeper;

// The one call of `addSecondSlot` in this firmware: the console's `update slot` and the server's
// own ask both come here, so that whichever asked, the keeper is told how it ended.
mm::AddSlotResult addTheSecondSlot() {
  const mm::TablePair tables = mm::tablesForThisBoard();
  const mm::AddSlotResult r = mm::addSecondSlot(mm::partitionTableFlash(), tables.oneSlot, tables.twoSlots);
  tableKeeper.note(r.ending, millis());
  mm::logf(r.ending == mm::AddSlotEnding::Added ? mm::LogLevel::Warn
                                                : (r.ending == mm::AddSlotEnding::NoTable ? mm::LogLevel::Error
                                                                                          : mm::LogLevel::Info),
           "update slot: %s", r.line);
  return r;
}

// The trial's three conditions (task p1.15-60, deliverable 2), each a plain reading of something
// this file already has: the mesh started, the WiFi joined and the clock came off the network,
// and a heartbeat was answered 2xx. Nothing is measured here that is not measured anyway.
class TrialNow final : public mm::update::TrialConditions {
 public:
  bool meshStarted() const override { return radio_ok; }
  bool linkAndClock() const override { return wifi.connected() && agentClock.fromNetwork(); }
  // Since task p1.15-61 this is not simply *a heartbeat was answered*: while an ask from the
  // server is in hand it is a heartbeat that **carried `step` `proving`** and was answered 2xx,
  // which is what the server reads as *the new firmware has reached me with its token*
  // (deliverable 5). With no ask in hand - a switch made at the console - it is task 60's own
  // rule, and nothing changed.
  bool heartbeatAnswered() const override { return asked.heartbeatProvesTheTrial(agent.everHeartbeated()); }
};

// Both clocks the trial needs: the milliseconds the ten minutes are counted in, and the UNIX
// seconds a journal entry is stamped with - 0 while the board has no time, which is what a
// journal entry then says.
class TrialClockNow final : public mm::update::TrialClock {
 public:
  uint32_t ms() const override { return millis(); }
  uint32_t epoch() const override { return agentClock.now(); }
};

// The firmware's own deliberate restart path (src/Health.h): the reason kept where every
// restart's reason is kept, what waits in RAM given its one try, and then the restart.
class TrialRestart final : public mm::update::DeliberateRestart {
 public:
  void note(const char* why) override { mm::healthNoteReboot(why); }
  void postWhatWaits() override {
    const mm::AgentLoop::Posted posted = agent.postWhatWaits();
    LOG_W("update: %d result(s) and %u observation(s) went out before the restart", posted.results,
          static_cast<unsigned>(posted.observations));
  }
  void restart() override {
    Serial.flush();
    delay(100);
    board.reboot();
  }
};

TrialNow trialConditions;
TrialClockNow trialClock;
TrialRestart trialRestart;

// The ask's three board seams (task p1.15-61). Each is a plain forwarding of something this file
// already does for the console: the partition table of task 57, the fetch of task 59 and the
// switch of task 60.
//
// The table word is read out of the sector once and remembered, because the heartbeat asks for it
// every thirty seconds and the sector is four kilobytes of flash; writing the slot forgets it, so
// the boot after that reads the table again.
class AskTableNow final : public mm::update::AskTable {
 public:
  const char* word() const override {
    if (word_ == nullptr) {
      const mm::TablePair tables = mm::tablesForThisBoard();
      const mm::TableReading reading = mm::readTable(mm::partitionTableFlash(), tables.oneSlot);
      word_ = mm::tableKindWord(reading.read ? reading.kind : mm::TableKind::Other);
    }
    return word_;
  }

  bool addSecondSlot(char* line, size_t cap, bool* theSectorHoldsATable) override {
    const mm::AddSlotResult r = addTheSecondSlot();
    std::snprintf(line, cap, "%s", r.line);
    if (theSectorHoldsATable != nullptr) *theSectorHoldsATable = r.ending != mm::AddSlotEnding::NoTable;
    word_ = nullptr;
    return r.added;
  }

 private:
  mutable const char* word_ = nullptr;
};

class AskFetchNow final : public mm::update::AskFetch {
 public:
  bool ready() const override { return fetcherReady && server.configured() && wifi.connected(); }
  bool start(const char* base) override { return fetcherReady && fetcher.start(base, millis()); }
  bool running() const override { return fetcher.running(); }
  const mm::update::FetchResult& result() const override { return fetcher.result(); }
};

class AskTrialNow final : public mm::update::AskTrial {
 public:
  bool apply(char* why, size_t cap) override {
    if (!updaterReady) {
      std::snprintf(why, cap, "this board has not finished starting up");
      return false;
    }
    return updater.apply(why, cap);
  }
  bool onTrial() const override { return updater.onTrial(); }
  const mm::update::UpdateRecord& record() const override { return updater.record(); }
};

AskTableNow askTable;
AskFetchNow askFetch;
AskTrialNow askTrial;

// What the observer's connections read of this board (observer/Connection.h, Environment).
uint32_t millisNow() { return millis(); }
uint32_t heapFree() { return ESP.getFreeHeap(); }  // internal heap: MALLOC_CAP_INTERNAL in this core's Esp.cpp
uint32_t heapLargestBlock() { return ESP.getMaxAllocHeap(); }
uint32_t psramFree() { return ESP.getFreePsram(); }  // 0 on a board without PSRAM
uint32_t psramLargestBlock() { return ESP.getMaxAllocPsram(); }
uint32_t randomBits() { return esp_random(); }

// What the uplink's calls tell the observer (src/observer/Connection.h): a call that failed for
// want of memory, and - since task p1.8-31 - every call's cost, so the gate reads what a call
// dips and a call under the floor takes the highest slot down. From the main loop and from the
// job pump alike, so that a heartbeat made inside a long job is read as soon as it is made.
void handUplinkToObserver() {
  if (server.memoryFailures() != uplinkMemoryFailuresSeen) {
    uplinkMemoryFailuresSeen = server.memoryFailures();
    observer.onUplinkMemoryFailure();
  }
  const uint32_t calls = server.line().counters().calls;
  if (calls != uplinkCallsSeen) {
    uplinkCallsSeen = calls;
    const mm::AgentClient::HeapCost& c = server.lastCost();
    if (c.any) observer.onUplinkCall(c.freeBefore, c.lowest, c.freeAfter);
  }
}

// The neighbour sweep's "a job is open" (task p1.8-33, src/observer/Neighbours.h): from the
// claim to the end of the job, whatever the mesh's peer says, nothing of the sweep goes on the air.
bool jobOpen() { return jobs.openJob() != nullptr; }

// What the board measures for the observer's status message (src/observer/Observer.h,
// BoardStats): the radio's noise floor, the dispatcher's air-time counters in whole seconds,
// and the battery as MeshCore's board object reads it - both Heltecs measure one on an ADC pin
// (variants/heltec_v3/HeltecV3Board.h:53, variants/heltec_v4/HeltecV4Board.cpp:43 at the pin), and
// a board on USB with nothing on that pin reads noise, which is left out below a volt: this
// firmware's own line, since the observer firmware publishes whatever the ADC says.
void fillBoardStats(mm::BoardStats& s) {
  if (!radio_ok) return;
  s.noiseFloor = radio_driver.getNoiseFloor();
  s.txAirSecs = static_cast<int>(the_mesh.getTotalAirTime() / 1000);
  s.rxAirSecs = static_cast<int>(the_mesh.getReceiveAirTime() / 1000);
  const uint16_t mv = board.getBattMilliVolts();
  s.batteryMv = mv >= 1000 ? static_cast<int>(mv) : -1;
}

// ---- the radio ------------------------------------------------------------------------------

void applyRadioSettings() {
  const float freq = settings.number(*mm::findKey("radio.freq"));
  const float bw = settings.number(*mm::findKey("radio.bw"));
  const int sf = static_cast<int>(settings.number(*mm::findKey("radio.sf")));
  const int cr = static_cast<int>(settings.number(*mm::findKey("radio.cr")));
  const int tx = static_cast<int>(settings.number(*mm::findKey("radio.tx")));
  radio_driver.setParams(freq, bw, static_cast<uint8_t>(sf), static_cast<uint8_t>(cr));
  radio_driver.setTxPower(static_cast<int8_t>(tx));
  LOG_I("radio: %.3f MHz, bw %.1f kHz, sf %d, cr %d, tx %d dBm", static_cast<double>(freq), static_cast<double>(bw), sf, cr, tx);
}

// What the heartbeat says about this board (agent/AgentLoop.h): the version, the board, the
// companion firmware this image is built from, and the radio block as the settings have it -
// read here, once at boot and again on every `set radio.*` and `erase confirm`, so that the loop
// reads no setting of its own.
void fillIdentity() {
  identity.agentVersion = meshmaster_agent_version();
  identity.board = mm::boardName();
  identity.companionFirmware = mm::meshcoreTag();
  identity.radio.freqMHz = settings.numberDouble(*mm::findKey("radio.freq"));
  identity.radio.bwKHz = settings.numberDouble(*mm::findKey("radio.bw"));
  identity.radio.sf = static_cast<int>(settings.number(*mm::findKey("radio.sf")));
  identity.radio.cr = static_cast<int>(settings.number(*mm::findKey("radio.cr")));
}

void applyLogSettings() {
  char buf[16];
  settings.effective(*mm::findKey("log.level"), buf, sizeof buf);
  mm::logSetLevelByName(buf);
  settings.effective(*mm::findKey("log.frames"), buf, sizeof buf);
  the_mesh.setFrameLog(std::strcmp(buf, "on") == 0);
}

// The flood scope (task p1.8-32; meshmaster.dk's D-47): which region every flood of this board
// carries, and whether a flooded transaction's second try drops it. Two objects read it - the
// mesh computes the region's transport key and puts the codes on its floods
// (src/mesh/AgentMesh.h), the runner owns the rule and the counts (agent/FloodScope.h) - and
// this is the one place either is told. Re-read on every `set flood.*` and on `erase confirm`,
// so no reboot is part of changing a scope.
void applyFloodSettings() {
  char buf[mm::kMaxValueLen + 1];
  settings.effective(*mm::findKey("flood.scope"), buf, sizeof buf);
  the_mesh.setFloodScope(buf);
  settings.effective(*mm::findKey("flood.unscoped"), buf, sizeof buf);
  jobs.setFloodRetry(std::strcmp(buf, "never") == 0 ? mm::UnscopedRetry::Never : mm::UnscopedRetry::Retry);
  LOG_I("flood: scope %s, %s", the_mesh.floodScope(),
        std::strcmp(buf, "never") == 0 ? "every flood scoped (flood.unscoped never)"
                                       : "the second flood of a transaction unscoped");
}

// `display.timeout`: how long the panel stays lit after a button press, 0 never turning it off,
// which is the default (src/ui/Oled.h). Re-read on every `set display.*`, so no reboot is part
// of changing it.
void applyDisplaySettings() {
  const long seconds = static_cast<long>(settings.number(*mm::findKey("display.timeout")));
  oled.setTimeout(static_cast<uint32_t>(seconds < 0 ? 0 : seconds));
}

void setUpIdentity() {
  // MeshCore's own way (examples/companion_radio/MyMesh.cpp, begin()): load /identity/_main.id,
  // else make one from radio noise, avoiding the two reserved first bytes, and save it.
  identity_store.begin();
  if (identity_store.load("_main", the_mesh.self_id)) {
    identity_ok = true;
    return;
  }
  the_mesh.self_id = radio_new_identity();
  int count = 0;
  while (count < 10 && (the_mesh.self_id.pub_key[0] == 0x00 || the_mesh.self_id.pub_key[0] == 0xFF)) {
    the_mesh.self_id = radio_new_identity();
    count++;
  }
  identity_ok = identity_store.save("_main", the_mesh.self_id);
  LOG_I("identity: created a new one%s", identity_ok ? " and saved it" : ", but it could not be saved");
}

void publicKeyHex(char* out, size_t cap) {
  std::snprintf(out, cap, "%s", public_key_hex[0] != 0 ? public_key_hex : "none");
}

// ---- the console's commands -----------------------------------------------------------------

void formatUptime(char* out, size_t cap) {
  const uint32_t s = millis() / 1000;
  std::snprintf(out, cap, "%lud %02lu:%02lu:%02lu", static_cast<unsigned long>(s / 86400),
                static_cast<unsigned long>((s / 3600) % 24), static_cast<unsigned long>((s / 60) % 60),
                static_cast<unsigned long>(s % 60));
}

void printStatus(Stream& out) {
  // Before anything else, and for as long as it is true: this board's table sector holds no table,
  // so a restart now is a board that needs a cable (task p1.16-63, deliverable 4).
  if (tableKeeper.theSectorHoldsNoTable()) out.printf("!!        %s\r\n", tableKeeper.line());

  char line[320];
  char key[80];
  char uptime[32];
  formatUptime(uptime, sizeof uptime);
  publicKeyHex(key, sizeof key);

  // *on trial* while a switched image has not proved itself: the first thing a person at the
  // window reads, because it is the one state in which this board may restart by itself and come
  // back as another firmware (task p1.15-60, deliverable 8).
  const char* trial = updater.statusNote();
  out.printf("firmware  %s (%.8s)%s%s\r\n", mm::firmwareString(), mm::meshcorePin(), trial[0] != 0 ? " - " : "",
             trial);
  out.printf("board     %s, %s, up %s\r\n", mm::boardName(), board.getResetReasonString(board.getResetReason()), uptime);
  out.printf("identity  %s%s\r\n", key, identity_ok ? "" : (radio_ok ? "" : " (radio init failed, none made)"));

  if (radio_ok) {
    const mm::MeshCounters& c = the_mesh.counters();
    out.printf("radio     %.3f MHz, bw %.1f kHz, sf %d, cr %d, tx %d dBm; companion role, no repeat\r\n",
               static_cast<double>(settings.number(*mm::findKey("radio.freq"))),
               static_cast<double>(settings.number(*mm::findKey("radio.bw"))),
               static_cast<int>(settings.number(*mm::findKey("radio.sf"))),
               static_cast<int>(settings.number(*mm::findKey("radio.cr"))),
               static_cast<int>(settings.number(*mm::findKey("radio.tx"))));
    out.printf("frames    rx %" PRIu32 " (advert %" PRIu32 ", grp-txt %" PRIu32 ", txt %" PRIu32 ", ack %" PRIu32
               ", path %" PRIu32 ", trace %" PRIu32 ", other %" PRIu32 "), tx %" PRIu32 ", noise floor %d dBm, frame log %s\r\n",
               c.rx, c.rxByType[PAYLOAD_TYPE_ADVERT], c.rxByType[PAYLOAD_TYPE_GRP_TXT], c.rxByType[PAYLOAD_TYPE_TXT_MSG],
               c.rxByType[PAYLOAD_TYPE_ACK], c.rxByType[PAYLOAD_TYPE_PATH], c.rxByType[PAYLOAD_TYPE_TRACE],
               c.rx - c.rxByType[PAYLOAD_TYPE_ADVERT] - c.rxByType[PAYLOAD_TYPE_GRP_TXT] - c.rxByType[PAYLOAD_TYPE_TXT_MSG] -
                   c.rxByType[PAYLOAD_TYPE_ACK] - c.rxByType[PAYLOAD_TYPE_PATH] - c.rxByType[PAYLOAD_TYPE_TRACE],
               c.tx, radio_driver.getNoiseFloor(), the_mesh.frameLog() ? "on" : "off");
  } else {
    out.println("radio     init failed: no identity, nothing received");
  }

  wifi.describe(line, sizeof line);
  out.printf("wifi      %s\r\n", line);
  agentClock.describe(line, sizeof line);
  out.printf("time      %s\r\n", line);
  server.describe(line, sizeof line);
  out.printf("server    %s\r\n", line);
  agent.describe(line, sizeof line);
  out.printf("heartbeat %s\r\n", line);
  jobs.describe(line, sizeof line);
  out.printf("jobs      %s\r\n", line);
  // The flood scope and what each shape has earned (task p1.8-32): the number that says whether
  // the unscoped try is still worth its airtime.
  jobs.describeFlood(line, sizeof line);
  out.printf("flood     %s\r\n", line);

  observer.describe(line, sizeof line);
  out.printf("observer  %s\r\n", line);

  const mm::AgentClient::Outcome& o = server.last();
  out.printf("heap      free %lu, min ever %lu, largest block %lu", static_cast<unsigned long>(ESP.getFreeHeap()),
             static_cast<unsigned long>(ESP.getMinFreeHeap()), static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  if (o.attempted && o.heapAfter != 0) {
    out.printf("; after the tls call %lu", static_cast<unsigned long>(o.heapAfter));
  }
  if (mm::tlsheap::psramInUse()) {
    out.printf("; psram free %lu, largest block %lu", static_cast<unsigned long>(ESP.getFreePsram()),
               static_cast<unsigned long>(ESP.getMaxAllocPsram()));
  }
  out.print("\r\n");
  // The kept connection and what a call costs (task p1.8-30): the figures task 31 sets the
  // observer's gate from.
  server.describeUplink(line, sizeof line);
  out.printf("uplink    %s\r\n", line);
  out.printf("log       level %s, frames %s, oled %s\r\n", mm::logLevelName(mm::logLevel()), the_mesh.frameLog() ? "on" : "off",
             display_ok ? (oled.isOn() ? "on" : "off") : "not found");
}

void printWifi(Stream& out) { wifi.scan(out); }

// The one key whose value alone does not say what it does: `get flood.scope` prints the region's
// transport key - the SHA-256 of `#name`, public by construction (meshmaster.dk's D-47) - and the
// transport code of the example payload of src/mesh/TransportScope.h, so that two boards given
// the same scope can be held against each other without a repeater in the middle.
void printKeyNote(const mm::KeyDef& key, Stream& out) {
  if (std::strcmp(key.name, "flood.scope") != 0) return;
  char line[220];
  the_mesh.describeFloodScope(line, sizeof line);
  out.printf("scope     %s\r\n", line);
}

// `get mqtt.status`: one line per broker slot - preset or host, state, since, published,
// dropped, last error - and the memory rule the slots live under: this board's figures (task
// p1.8-31, src/observer/Board.h), what the uplink has done to the heap, where the TLS buffers
// are, and the least stack the clients' tasks ever had.
void printMqttStatus(Stream& out) {
  char line[400];
  observer.describe(line, sizeof line);
  out.printf("observer  %s\r\n", line);
  for (int s = 0; s < mm::observer::kSlots; s++) {
    if (!observer.slotShown(s)) continue;
    observer.describeSlot(s, line, sizeof line);
    out.printf("%s\r\n", line);
  }
  observer.describeGate(line, sizeof line);
  out.printf("memory    %s\r\n", line);
  // The neighbour report's schedule and its last outcome (task p1.8-33; `discover.scopes` prints the table).
  observer.describeNeighbours(line, sizeof line);
  out.printf("%s\r\n", line);
  out.printf("uplink    %lu memory failure(s) so far, %lu call(s)", static_cast<unsigned long>(server.memoryFailures()),
             static_cast<unsigned long>(server.line().counters().calls));
  const mm::AgentClient::HeapCost& c = server.lastCost();
  if (c.any) {
    out.printf("; the last %lu free before, lowest %lu during, %lu after", static_cast<unsigned long>(c.freeBefore),
               static_cast<unsigned long>(c.lowest), static_cast<unsigned long>(c.freeAfter));
  }
  out.print("\r\n");
  if (mm::tlsheap::psramInUse()) {
    out.printf("tls       buffers of %u bytes and over in psram: %lu allocation(s) routed there, %lu fell back to internal heap\r\n",
               static_cast<unsigned>(mm::tlsheap::kPsramFromBytes), static_cast<unsigned long>(mm::tlsheap::psramRouted()),
               static_cast<unsigned long>(mm::tlsheap::psramFallbacks()));
  } else {
    out.println("tls       buffers in internal heap (this board has no psram)");
  }
  const mm::MqttClient* const clients[mm::observer::kSlots] = {&mqttSlot1, &mqttSlot2};
  for (int s = 0; s < observer.slotsOnBoard(); s++) {
    const uint32_t low = clients[s]->stackLowWater();
    if (low == 0) {
      out.printf("stack     slot %d task: %lu bytes; nothing measured yet\r\n", s + 1,
                 static_cast<unsigned long>(mm::MqttClient::kTaskStackBytes));
    } else {
      out.printf("stack     slot %d task: %lu bytes, the least ever free %lu\r\n", s + 1,
                 static_cast<unsigned long>(mm::MqttClient::kTaskStackBytes), static_cast<unsigned long>(low));
    }
  }
}

// `discover.scopes` (task p1.8-33): the neighbour table as it stands - the repeaters heard at
// zero hops, with what the last cycle's regions request found for each - and a cycle of the
// sweep started now: sixty seconds of node discovery, then one regions request per neighbour,
// newest first, each on the airtime budget, and the report to every connected slot. The cycle
// runs in the background of the main loop; the log says how it went, and `get mqtt.status`'s
// `nbr:` line has the schedule and the last outcome.
void printDiscoverScopes(Stream& out) {
  const mm::observer::NeighbourTable& table = observer.neighbourTable();
  const mm::observer::NeighbourSweep& sweep = observer.sweep();
  char line[160];
  observer.describeNeighbours(line, sizeof line);
  out.printf("neighbours %d heard at zero hops (table of %d); %s\r\n", table.count(), mm::observer::kNeighbourCap, line);
  if (table.count() == 0) {
    out.println("  none yet: a repeater's advert with no hops, or an answer to the node discovery, puts one here");
  } else {
    out.println("  key           snr     rssi  heard       last cycle");
    const uint32_t now = millis();
    for (int i = 0; i < mm::observer::kNeighbourCap; i++) {
      const mm::observer::Neighbour& n = table.at(i);
      if (!n.used) continue;
      char key[2 * PUB_KEY_SIZE + 1];
      mesh::Utils::toHex(key, n.key, PUB_KEY_SIZE);
      // What the last cycle found for this key, if it was in that cycle's snapshot.
      const char* found = "-";
      const char* scopes = "";
      for (int j = 0; j < sweep.snapshotCount(); j++) {
        if (std::memcmp(sweep.entry(j).key, n.key, PUB_KEY_SIZE) == 0) {
          found = mm::observer::queryStatusName(sweep.entry(j).status);
          scopes = sweep.entry(j).scopes;
          break;
        }
      }
      char rssi[12];
      if (n.rssi == mm::observer::kNeighbourNoRssi) {
        std::snprintf(rssi, sizeof rssi, "-");
      } else {
        std::snprintf(rssi, sizeof rssi, "%d", n.rssi);
      }
      out.printf("  %.12s  %6.2f  %4s  %6lu s ago  %s%s%s\r\n", key, static_cast<double>(n.snrQ4) / 4.0, rssi,
                 static_cast<unsigned long>((now - n.heardAtMs) / 1000), found, scopes[0] != '\0' ? ": " : "", scopes);
    }
  }
  if (!radio_ok) {
    out.println("cycle     not started: the radio did not initialise");
    return;
  }
  if (observer.discoverScopes()) {
    out.printf("cycle     starts now: %lu s of node discovery, then one regions request per neighbour on the airtime budget; "
               "the report goes to every connected slot (get mqtt.status: nbr)\r\n",
               static_cast<unsigned long>(mm::observer::kDiscoveryWindowMs / 1000));
  } else {
    out.println("cycle     already running (the nbr line above says which phase)");
  }
}

// `update`: which partition table this board runs on, and the application slots it has (task
// p1.15-57). A board in the field upgraded with the plain `.bin` keeps the table it was flashed
// with, so this is how a person at a window finds out whether this board can be updated from the
// server yet, and `update slot` below is how it comes to be able to.
void printUpdate(Stream& out) {
  // The same first line as `status` has, and for the same reason: whichever of the two a person
  // types, a board whose sector holds no table says so before it says anything else.
  if (tableKeeper.theSectorHoldsNoTable()) out.printf("!!        %s\r\n", tableKeeper.line());

  // What this image says it is (task p1.15-58): the 96 bytes the build placed inside it, read
  // back from the image that is running. It is the one line that proves on a bench what
  // tools/check-descriptor.py proves in CI, and reading it here is also what keeps the linker
  // from collecting a descriptor nothing refers to.
  mm::update::Descriptor self;
  if (mm::update::thisImageDescriptor(self)) {
    char role[16];
    out.printf("image     %s %s, band %s, version %s\r\n",
               mm::update::roleName(self.role, role, sizeof(role)), self.board, self.band,
               self.version);
  } else {
    out.println("image     carries no descriptor this firmware can read");
  }
  // And the keys it would check a release's signature with. No key is the safe state and the
  // whole answer: nothing can be signed in a way this build believes, so no update is taken.
  char keys[192];
  mm::update::renderKeysLine(mm::update::trustedKeys(), mm::update::trustedKeyCount(),
                             mm::update::trustsTestKey(), keys, sizeof(keys));
  out.printf("keys      %s\r\n", keys);

  const mm::TablePair tables = mm::tablesForThisBoard();
  const mm::TableReading r = mm::readTable(mm::partitionTableFlash(), tables.oneSlot);
  if (!r.read) {
    out.println("table     could not be read; nothing is claimed about this board's slots");
    return;
  }
  out.printf("table     %s\r\n", mm::tableKindWord(r.kind));
  if (r.app0.present) {
    out.printf("app0      0x%06lX, 0x%06lX bytes\r\n", static_cast<unsigned long>(r.app0.offset),
               static_cast<unsigned long>(r.app0.size));
  }
  switch (r.kind) {
    case mm::TableKind::Two:
      out.printf("app1      0x%06lX, 0x%06lX bytes\r\n", static_cast<unsigned long>(r.app1.offset),
                 static_cast<unsigned long>(r.app1.size));
      break;
    case mm::TableKind::One:
      out.println("app1      none: this is the table boards were flashed with before the second slot existed");
      out.printf("          type `update %s` to add it, then `reboot`; the settings and the identity are untouched\r\n",
                 mm::kSlotWord);
      break;
    case mm::TableKind::Other:
      out.println("          this is not a table this firmware knows, so it will not be written over");
      out.println("          flash the merged image of a release over USB to get one with two slots");
      break;
  }

  // What the other slot holds, from this task's own record (task p1.15-59, deliverable 3): a
  // judged image survives a restart, and this is where a person sees that it did. Nothing of
  // this is a secret: a version, a hash, a length and a path the server published.
  char held[200];
  const mm::update::SlotRecord judged = mm::update::loadRecord(slotRecordStore);
  mm::update::renderRecordLine(judged, held, sizeof(held));
  out.printf("other     %s\r\n", held);
  if (fetcher.running()) {
    out.println("          a fetch is running now; its steps are in the log");
  } else if (r.kind == mm::TableKind::Two && !judged.judged) {
    out.printf("          `update %s <release id>` fetches a release into it and judges it\r\n", mm::kFetchWord);
  } else if (r.kind == mm::TableKind::Two && !updater.onTrial()) {
    out.printf("          `update %s` switches to it and restarts; it then has ten minutes to prove itself\r\n",
               mm::kApplyWord);
  }

  // The trial while there is one - each condition and the minutes left - and the last update's
  // ending, which is what the record kept across the restarts (task p1.15-60, deliverable 8).
  mm::StreamOut text(out);
  updater.report(text);
  // And what meshmaster.dk asked for, if anything (task p1.15-61): the ask in hand and the step
  // it stands at, or the last one's outcome and whether the server has its result.
  asked.report(text);
}

// `update fetch <id>`: fetch release `/agent/v1/firmware/<id>` into the other application slot
// and judge what was written (task p1.15-59). It starts the fetch and returns - the file is
// fetched in pieces from loop(), so there is nothing to wait for here - and every step, and the
// ending, goes to the log and so to this same port. Nothing is switched: the running firmware and
// the boot choice are as they were (task 60 is what switches).
void printUpdateFetch(const char* id, Stream& out) {
  if (!fetcherReady) {
    out.println("fetch     not ready: this board has not finished starting up");
    return;
  }
  if (fetcher.running()) {
    out.println("fetch     one is already running; its steps are in the log");
    return;
  }
  // Not while meshmaster.dk's own ask is in hand (task p1.15-61): it is doing these steps itself.
  if (const char* const asking = asked.whyNotNow()) {
    out.printf("fetch     %s\r\n", asking);
    return;
  }
  // Not while a firmware is on trial (task p1.15-60): the slot a fetch writes is the one holding
  // the image this board would go back to, and writing over it during a trial would leave a board
  // with nothing that is known to work.
  const char* const inTheWay = updater.whyTheOtherSlotMayNotBeWritten();
  if (inTheWay != nullptr) {
    out.printf("fetch     %s\r\n", inTheWay);
    return;
  }
  if (!server.configured()) {
    out.println("fetch     the server is not configured: set meshmaster.token first");
    return;
  }
  char base[mm::update::kMaxBaseLen + 1];
  if (!mm::update::baseForRelease(id, base, sizeof(base))) {
    out.printf("fetch     %.16s is not a release id this firmware will ask for\r\n", id);
    return;
  }
  if (!fetcher.start(base, millis())) {
    const mm::update::FetchResult& r = fetcher.result();
    out.printf("%-9s %s\r\n", mm::update::endingWord(r.ending), r.why);
    return;
  }
  out.printf("fetch     %s: the manifest and its signature first, then the image in pieces; the steps follow\r\n",
             base);
}

// `update apply`: switch to the image task 59 judged and restart into it, so that it runs on
// trial for ten minutes (task p1.15-60, deliverable 8). Either this does not return - the board
// restarts - or nothing at all was switched and one sentence says why.
void printUpdateApply(Stream& out) {
  if (!updaterReady) {
    out.println("apply     not ready: this board has not finished starting up");
    return;
  }
  if (const char* const asking = asked.whyNotNow()) {
    out.printf("apply     %s\r\n", asking);
    return;
  }
  if (fetcher.running()) {
    out.println("apply     a fetch is running; wait for its ending before switching to anything");
    return;
  }
  char why[224];
  if (updater.apply(why, sizeof(why))) {
    out.println("apply     switching; the board restarts now");  // not reached on a board
    return;
  }
  out.printf("apply     %s\r\n", why);
  mm::logf(mm::LogLevel::Info, "update apply: %s", why);
}

// `update slot`: the one command of this console that erases and writes flash. One sector, at the
// partition table's own address, and only when the table is the one boards were flashed with
// (src/platform/PartitionTable.h). A power cut during it leaves a board that needs a merged image
// over USB; it is the only moment of this pile that can. A write that *fails* does not: the new
// table is tried three times, the old one is put back three times after that, and only a sector
// that takes neither is left with no table - and the keeper above goes on at it (task p1.16-63).
void printUpdateSlot(Stream& out) {
  const mm::AddSlotResult r = addTheSecondSlot();
  out.printf("%s\r\n", r.line);
  // Which of the three endings it was, in the one word, and the tries it took when it took more
  // than one. The sentence above says both in words; this says them in a shape two boards can be
  // compared by (task p1.16-63, deliverable 3). A sector nothing was written to has no tries.
  if (r.tries > 0) {
    out.printf("          ending `%s`", mm::addSlotEndingWord(r.ending));
    if (r.tries > 1) out.printf(", after %d tries of the new table", r.tries);
    if (r.triesBack > 0) out.printf(", and %d of the old one", r.triesBack);
    out.printf("\r\n");
  }
  if (tableKeeper.theSectorHoldsNoTable()) out.printf("          %s\r\n", tableKeeper.line());
}

// `mesh`: what the radio has heard. The names are printed here and in no log line - this is the
// one place a person reads them, and they come from strangers, so they are the bounded, valid
// UTF-8 the ring kept (agent/Observations.h).
void printMesh(Stream& out) {
  if (!radio_ok) {
    out.println("radio     init failed: nothing is being heard");
    return;
  }
  const mm::MeshCounters& c = the_mesh.counters();
  const mm::ObservationRing& ring = agent.ring();
  out.printf("adverts   %" PRIu32 " heard, %u waiting to be posted (ring %u), %" PRIu32 " dropped, %" PRIu32
             " undecodable\r\n",
             c.adverts, static_cast<unsigned>(ring.size()), static_cast<unsigned>(mm::ObservationRing::kCapacity),
             ring.dropped(), c.advertsDropped);
  out.printf("frames    rx %" PRIu32 ", tx %" PRIu32 ", overheard %" PRIu32 " (counted, never read)\r\n", c.rx, c.tx,
             the_mesh.overheard());
  if (the_mesh.recentCount() == 0) {
    out.println("last five none yet");
    return;
  }
  out.println("last five newest first: key, name, role, hops, snr, rssi, heard");
  const uint32_t now = millis();
  for (int i = 0; i < the_mesh.recentCount(); i++) {
    const mm::HeardAdvert& a = the_mesh.recent(i);
    char key[2 * PUB_KEY_SIZE + 1];
    mesh::Utils::toHex(key, a.publicKey, PUB_KEY_SIZE);
    const char* role = mm::roleName(a.advType);
    out.printf("  %.12s  %-32s %-10s %u hop(s)  snr %.2f  rssi %d  %lu s ago\r\n", key,
               a.name[0] != 0 ? a.name : "(no name)", role != nullptr ? role : "unknown",
               static_cast<unsigned>(a.hops), static_cast<double>(a.snrQ4) / 4.0,
               a.rssi == mm::kNoRssi ? 0 : a.rssi, static_cast<unsigned long>((now - a.heardAtMillis) / 1000));
    if (a.hasPosition) {
      out.printf("                %ld.%06ld, %ld.%06ld\r\n", static_cast<long>(a.latE6 / 1000000),
                 static_cast<long>(a.latE6 < 0 ? -(a.latE6 % 1000000) : a.latE6 % 1000000),
                 static_cast<long>(a.lonE6 / 1000000),
                 static_cast<long>(a.lonE6 < 0 ? -(a.lonE6 % 1000000) : a.lonE6 % 1000000));
    }
  }
}

// `server`: the heartbeat and the observation batches - the last, the next, and the counters
// the heartbeat itself carries. The library's reports write to a TextOut (agent/TextOut.h);
// StreamOut is the serial port behind one.
void printServer(Stream& out) {
  mm::StreamOut text(out);
  agent.report(text);
}

// `jobs`: the budget in force, what has been claimed, and the last ten with their outcome and
// their transaction count.
void printJobs(Stream& out) {
  mm::StreamOut text(out);
  jobs.report(text);
}

// `sessions`: the repeater logins held - per repeater the class, the level and the age. A
// session is a key, a class, a role byte and a time; no secret is in one.
void printSessions(Stream& out) {
  mm::StreamOut text(out);
  jobs.reportSessions(text);
}

// `errors`: every counter the heartbeat carries, by the name the server shows it under, and the
// ones beside them that only a person at the port ever sees.
void printErrors(Stream& out) {
  mm::StreamOut text(out);
  agent.reportErrors(text);
}

// `reboots`: why this board restarted, the last five, from RTC memory - which a power cut clears
// and a watchdog reset does not (src/Health.h).
void printReboots(Stream& out) {
  char uptime[32];
  formatUptime(uptime, sizeof uptime);
  out.printf("this boot %s, up %s; watchdog %lu s on the loop task\r\n", mm::bootReason(), uptime,
             static_cast<unsigned long>(mm::kWatchdogSeconds));
  out.printf("counted   %lu restart(s) since the last power cut, %lu of them this firmware's own\r\n",
             static_cast<unsigned long>(mm::rebootsSeen()), static_cast<unsigned long>(mm::selfReboots()));
  if (!mm::rtcLogSurvived()) {
    out.println("log       empty: the RTC memory did not survive, which is what a power cut looks like");
  }
  if (mm::rebootCount() == 0) {
    out.println("last five none remembered");
    return;
  }
  out.println("last five newest first: when, uptime at the time, why");
  char when[24];
  for (int i = 0; i < mm::rebootCount(); i++) {
    const mm::RebootRecord& r = mm::rebootAt(i);
    if (r.atEpoch != 0) {
      mm::formatClock(r.atEpoch, when, sizeof when);
    } else {
      std::snprintf(when, sizeof when, "no clock");
    }
    out.printf("  %-10s after %lud %02lu:%02lu:%02lu  %s\r\n", when,
               static_cast<unsigned long>(r.uptimeS / 86400), static_cast<unsigned long>((r.uptimeS / 3600) % 24),
               static_cast<unsigned long>((r.uptimeS / 60) % 60), static_cast<unsigned long>(r.uptimeS % 60), r.why);
  }
}

// `dump`: the last 4 kB of the log, oldest first, whole lines only. The same bytes the port
// carried, which is the whole of the redaction - a line that could carry a secret here would
// already have carried it to a terminal (agent/LogRing.h).
void dumpSink(void* ctx, const char* text, size_t len) {
  Stream* out = static_cast<Stream*>(ctx);
  out->write(reinterpret_cast<const uint8_t*>(text), len);
}

void printDump(Stream& out) {
  const mm::LogRing& ring = mm::logRing();
  out.printf("log       %u of %u bytes, %lu line(s) written, %lu overwritten\r\n",
             static_cast<unsigned>(ring.size()), static_cast<unsigned>(mm::LogRing::kCapacity),
             static_cast<unsigned long>(ring.lines()), static_cast<unsigned long>(ring.dropped()));
  if (ring.empty()) {
    out.println("---- nothing logged yet ----");
    return;
  }
  out.println("---- the log, oldest first ----");
  ring.forEachChunk(&out, &dumpSink);
  out.println("---- end ----");
}

// A job's exchange blocks: a transaction is a send and up to sixteen seconds of waiting, and the
// budget's gap is twenty more. The console and the OLED are pumped through all of it, so a board
// running a job is still a board a person can type at; the mesh is pumped by the runner itself.
void pumpDuringAJob() {
  rtc_clock.tick();
  mm::healthTick();  // a thirteen-minute region load must not look like a wedged board
  console.loop();
  oled.loop();
  // And the observer's queues and events (task p1.7-27): the frames heard during a long job go
  // to the brokers as they are heard, rather than piling up in a two-kilobyte queue - and what
  // the uplink's calls inside the job cost (task p1.8-31).
  observer.setLinkUp(wifi.connected());
  handUplinkToObserver();
  observer.loop();
  // And the heartbeat that fell due while the job was waiting for the budget (task 06): a claim
  // is run to the end before the loop gets its turn back, and a board that is working hard is
  // not a board the server should be marking *quiet*.
  agent.heartbeatIfOverdue();
  // And the trial, if one is running (task p1.15-60): a thirteen-minute region load must not be
  // what makes a working image miss its ten minutes, and the heartbeat above is the condition it
  // is usually waiting for.
  updater.step();
}

void doReboot() {
  mm::healthNoteReboot("asked for on the console");
  delay(100);
  board.reboot();
}

void onSettingChanged(const mm::KeyDef& key) {
  if (std::strncmp(key.name, "wifi.", 5) == 0) {
    wifi.reconfigure();
  } else if (std::strncmp(key.name, "meshmaster.", 11) == 0) {
    server.reconfigure();
    agent.reconfigure();
  } else if (std::strncmp(key.name, "radio.", 6) == 0) {
    fillIdentity();  // the next heartbeat says what the radio is now programmed with
    if (radio_ok) applyRadioSettings();
  } else if (std::strncmp(key.name, "log.", 4) == 0) {
    applyLogSettings();
  } else if (std::strncmp(key.name, "display.", 8) == 0) {
    applyDisplaySettings();
  } else if (std::strncmp(key.name, "flood.", 6) == 0) {
    applyFloodSettings();  // the mesh's key and the runner's rule, re-read together
  } else if (std::strncmp(key.name, "mqtt", 4) == 0) {
    observer.reconfigure();  // mqtt.* and mqttN.*: the observer re-reads every one of its keys
  }
}

void onErased() {
  applyLogSettings();
  applyDisplaySettings();
  applyFloodSettings();
  observer.reconfigure();
  server.reconfigure();
  agent.reconfigure();
  wifi.reconfigure();
  fillIdentity();
  if (radio_ok) applyRadioSettings();
}

// ---- the OLED -------------------------------------------------------------------------------

// The one place the display's struct is filled: every field is a plain read of an object that
// already exists, and what the four lines *say* is src/ui/DisplayLines.cpp's, under test
// natively. Called from Oled::render once a second - and from the job pump, which is why the
// open job on line four is live.
void fillOled(char lines[mm::Oled::kLines][mm::Oled::kCols]) {
  mm::DisplayState state;

  state.publicKeyHex = (identity_ok && public_key_hex[0] != 0) ? public_key_hex : nullptr;
  state.hasTime = agentClock.hasTime();
  state.nowEpoch = agentClock.now();

  state.wifiConnected = wifi.connected();
  state.online = agent.online();

  state.everHeartbeated = agent.everHeartbeated();
  state.lastHeartbeatAtEpoch = agent.lastHeartbeatAtEpoch();

  const mm::ClaimedJob* open = jobs.openJob();
  if (open != nullptr) {
    state.jobOpen = true;
    state.jobId = open->id;
    state.jobType = open->type;
  }
  const mm::JobRunner::Record* last = jobs.newest();
  if (last != nullptr) {
    state.hasLastJob = true;
    state.lastJobAtEpoch = last->atEpoch;
  }
  state.tokenRefused = agent.halted();
  state.serverConfigured = server.configured();
  state.observerSlotsConfigured = observer.slotsConfigured();
  state.observerSlotsConnected = observer.slotsConnected();
  // Any wait before the next call, not only a doubling one: a 503 that named Retry-After is by
  // design not a consecutive failure (task p1.2-10), and the panel still has to say *retry*
  // through a deploy (task p1.3-12).
  state.backingOff = agent.retrySeconds() > 0;
  state.retrySeconds = agent.retrySeconds();

  mm::renderLines(state, lines);
}

}  // namespace

// ---- setup and loop -------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  board.begin();

  mm::logBeginSerial(&clockForLog);
  if (!settings.begin()) {
    Serial.println("error: NVS could not be opened; settings will not persist");
  }
  applyLogSettings();

  Serial.println();
  LOG_I("%s", mm::firmwareString());
  LOG_I("board: %s, %s", mm::boardName(), board.getResetReasonString(board.getResetReason()));

  // The watchdog, and the reason this board is starting again (src/Health.h). Early, because
  // everything after it can be the thing that has to be survived.
  mm::healthBegin(&clockForLog, board.getResetReasonString(board.getResetReason()));

  // The panel is probed here so that a missing one is the first thing logged - but the four lines
  // are first drawn below, once every object fillOled reads from has had its begin(): the first
  // flash of a V3 crashed drawing a frame from a server_ that agent.begin() had not set yet
  // (LoadProhibited at 0xc, the first frame rendered from Oled::begin before agent.begin ran).
  // Since task p1.2-09 every field fillOled reads is a plain member of an object that is already
  // constructed, so the order is no longer load-bearing - but it is still the right one.
  display_ok = display.begin();
  if (!display_ok) LOG_W("oled: not found");

  radio_ok = radio_init();
  if (radio_ok) {
    fast_rng.begin(radio_driver.getRngSeed());
    SPIFFS.begin(true);
    setUpIdentity();
    applyRadioSettings();
    the_mesh.begin();
    if (identity_ok) mesh::Utils::toHex(public_key_hex, the_mesh.self_id.pub_key, PUB_KEY_SIZE);
    LOG_I("identity: %s", public_key_hex[0] != 0 ? public_key_hex : "none");
  } else {
    LOG_E("radio: init failed; the console works, the mesh does not");
  }

  agentClock.begin(rtc_clock);

  // **What is this boot?** (task p1.15-60). Early, and before the WiFi, the server and the mesh:
  // a board that was switched to a new image is on trial from here, a board the bootloader sent
  // back finishes the record here, and a board running from the second slot with a firmware
  // flashed over USB into the first one switches to it here and restarts - so none of what
  // follows is started twice. The clock is up, so a journal entry has a time; the RTC usually
  // holds one across a software reset, and an entry made without one says 0.
  {
    // What this image says it is, out of the 96 bytes the build placed inside it (task p1.15-58):
    // the version the record is written with, and the one an image found in the other slot is
    // told from. Static, because the machine keeps the pointer.
    static char runningVersion[33] = {0};
    mm::update::Descriptor self;
    if (mm::update::thisImageDescriptor(self)) std::snprintf(runningVersion, sizeof runningVersion, "%s", self.version);
    mm::update::Updater::Seams seams;
    seams.slots = &mm::applicationSlots();
    seams.slot = &mm::otherApplicationSlot();
    seams.store = &slotRecordStore;
    seams.conditions = &trialConditions;
    seams.restart = &trialRestart;
    seams.clock = &trialClock;
    updater.begin(seams, runningVersion, mm::boardName(), mm::bootReason(), &fetchLog);
    updaterReady = true;
    // The ask's machine, on the same version string and the same log (task p1.15-61). A build
    // that trusts no release key hands the loop a provider that writes no heartbeat member and
    // takes no ask, because nothing could be signed in a way it believes (task p1.15-58).
    mm::update::Asked::Seams askSeams;
    askSeams.table = &askTable;
    askSeams.fetch = &askFetch;
    askSeams.trial = &askTrial;
    askSeams.store = &slotRecordStore;
    askSeams.clock = &trialClock;
    askSeams.restart = &trialRestart;
    const bool canBeUpdated = mm::update::trustedKeyCount() > 0 || mm::update::trustsTestKey();
    asked.begin(askSeams, runningVersion, canBeUpdated, &fetchLog);
    const mm::update::BootFinding found = updater.atBoot();
    if (found != mm::update::BootFinding::Nothing) {
      LOG_W("update: this boot is %s", mm::update::bootFindingWord(found));
    }
  }

  wifi.begin(settings);
  server.begin(settings, agentClock, wifi, meshmaster_agent_version());
  fillIdentity();
  // The runner and the loop on their three seams (agent/IMesh.h, IUplink.h, IPlatform.h):
  // the mesh in this process, the WiFi client, and Arduino on this ESP32. The Heltec sets no job
  // extension (IJobExtension.h).
  jobs.begin(the_mesh, server, platform, &pumpDuringAJob);
  // The flood scope, before any job can put a packet on the air: the mesh's key and the runner's
  // rule (task p1.8-32).
  applyFloodSettings();
  agent.begin(server, platform, the_mesh, jobs, identity);
  agent.setIdentity(public_key_hex[0] != 0 ? public_key_hex : nullptr);
  // The observer: off until a slot is configured, and then the dialect over every frame the
  // radio hears (src/observer/Observer.h) and two broker connections on esp-mqtt
  // (src/net/MqttClient.h), signed in with this identity's own key and verified against the
  // roots in src/agent/TlsRoots.h. The tap is set only with an identity, because a message
  // without an origin_id is nothing a broker could file.
  mm::observer::Client* const mqttClients[mm::observer::kSlots] = {&mqttSlot1, &mqttSlot2};
  mm::observer::Environment observerEnv;
  observerEnv.millis = &millisNow;
  observerEnv.epoch = &clockForLog;
  observerEnv.freeHeap = &heapFree;
  observerEnv.largestBlock = &heapLargestBlock;
  observerEnv.freePsram = &psramFree;
  observerEnv.largestPsram = &psramLargestBlock;
  observerEnv.random = &randomBits;
  observerEnv.caPem = mm::kCarriedRootsPem;
  observer.begin(settings, identity_ok ? &the_mesh.self_id : nullptr, identity_ok ? public_key_hex : nullptr,
                 board.getManufacturerName(), &clockForLog, mqttClients, observerEnv);
  if (identity_ok) the_mesh.setRxTap(&observer);
  // The neighbours (task p1.8-33): the mesh fills the observer's table from zero-hop repeater
  // adverts and discovery answers, the sweep sends its requests through the mesh on the jobs'
  // own airtime gate and never while a job is open, and the status message carries what the
  // radio and the board measure.
  if (identity_ok) {
    the_mesh.setNeighbourTable(&observer.neighbourTable());
    the_mesh.setBusyHook(&jobOpen);
    the_mesh.setPump(&pumpDuringAJob);
    observer.beginNeighbours(the_mesh, &jobs.gate());
    observer.setDefaultScope(the_mesh.floodScope());
  }
  observer.setBoardStats(&fillBoardStats);
  // And the observer's state in the heartbeat (task p1.7-28): the loop asks this object at every
  // heartbeat and writes what it says, which is nothing at all while the observer is off. The
  // library knows nothing of brokers; MeshMaster shows the state and configures none of it
  // (meshmaster.dk D-46).
  agent.setHeartbeatObserver(&observer);
  // And the update in the heartbeat, the ask in its reply and its one result (task p1.15-61): the
  // loop carries the three and decides none of them, and a build with no release key writes no
  // member at all (agent/AgentLoop.h, IUpdateProvider).
  agent.setUpdateProvider(&asked);
  if (!server.configured()) LOG_I("server: not configured; set meshmaster.token (meshmaster.url is https://meshmaster.dk unless set)");
  if (display_ok) {
    applyDisplaySettings();  // `display.timeout`, before the first frame starts its clock
    oled.begin(&display, &user_btn, &fillOled);  // the first frame, now that its sources exist
  }

  mm::ConsoleHost host = {&printStatus,  &printMesh,    &printServer, &printJobs,
                          &printSessions, &printErrors,  &printReboots, &printDump,
                          &printWifi,     &doReboot,     &onSettingChanged, &onErased,
                          &printMqttStatus, &printKeyNote, &printDiscoverScopes,
                          &printUpdate,   &printUpdateSlot,  &printUpdateFetch,
                          &printUpdateApply};
  console.begin(Serial, settings, host);
  // The fetch's three seams (task p1.15-59): the server over the one TLS client every call goes
  // through, the other application slot over the ESP-IDF's OTA writer, and the record in NVS.
  fetcher.begin(releaseRoutes, mm::otherApplicationSlot(), slotRecordStore, mm::update::trustedKeys(),
                mm::update::trustedKeyCount(), mm::boardName(), &fetchLog);
  fetcherReady = true;
  {
    char held[200];
    mm::update::renderRecordLine(mm::update::loadRecord(slotRecordStore), held, sizeof(held));
    LOG_I("update: the other application slot %s", held);
  }
  // And what meshmaster.dk asked for, if an ask was in hand when this board last restarted (task
  // p1.15-61). **After** the fetcher, because an ask at `fetching` begins its fetch again here,
  // and after the updater's own `atBoot`, whose answer is what an ask at `switching` reads.
  asked.atBoot();
  board.onBootComplete();
}

void loop() {
  mm::healthTick();
  if (radio_ok) the_mesh.loop();
  rtc_clock.tick();
  wifi.loop();
  agentClock.loop();

  if (wifi.takeConnectedEdge()) {
    agentClock.startSntp();
    agent.onLinkUp();
  }

  // The config call, the heartbeat every pollIntervalS and the observation batches between them:
  // one blocking call at a time, and nothing at all after a 401 (agent/AgentLoop.h).
  agent.loop();

  // The observer's broker connections: events, timers and queues, never a wait (the clients run
  // on tasks of their own). The uplink comes first: a call of its that failed for want of memory,
  // or dipped the heap under the floor, takes the highest slot down for ten minutes
  // (src/observer/Connection.h).
  observer.setLinkUp(wifi.connected());
  handUplinkToObserver();
  observer.loop();

  console.loop();
  oled.loop();

  // The old partition table, once a minute, while the sector holds none (task p1.16-63). It does
  // nothing at all otherwise, and the first write that holds ends it for good: a board whose table
  // is the one it had is a board that boots.
  if (tableKeeper.theSectorHoldsNoTable()) {
    const mm::TablePair tables = mm::tablesForThisBoard();
    const mm::KeeperStep went = tableKeeper.step(mm::partitionTableFlash(), tables.oneSlot, millis());
    if (went == mm::KeeperStep::Back) {
      mm::logf(mm::LogLevel::Warn, "update slot: %s", tableKeeper.line());
    } else if (went == mm::KeeperStep::Tried) {
      mm::logf(mm::LogLevel::Error, "update slot: %s", tableKeeper.line());
    }
  }

  // The trial, if a switched image is on one (task p1.15-60): the three conditions latched as
  // each is first seen, the image marked valid the moment the third is, and the board sent back
  // to the old one when ten minutes pass without them. It does nothing at all otherwise.
  updater.step();

  // The ask the server made, a step a pass (task p1.15-61): the steps in order, each written down
  // before it begins, and a restart where the step needs one. **From here and not from the pump
  // inside a job**, so that a job that was running when the ask came is finished first.
  asked.step();

  // One piece of a fetch a pass, and only when one is running: at most one request, so the mesh,
  // the console, the observer and the watchdog above all had their turn before it and have it
  // again straight after (task p1.15-59, deliverable 5).
  if (fetcher.running()) {
    fetcher.step(millis());
    if (!fetcher.running()) {
      const mm::update::FetchResult& r = fetcher.result();
      const mm::AgentClient::HeapCost& cost = server.lastCost();
      mm::logf(r.ending == mm::update::Ending::Fetched ? mm::LogLevel::Warn : mm::LogLevel::Error,
               "update: %s%s: %s (%lu ms, %lu request(s); the last call left %lu bytes of heap, lowest %lu)",
               mm::update::endingWord(r.ending), r.check != 0 ? " (see the number below)" : "", r.why,
               static_cast<unsigned long>(r.elapsedMs), static_cast<unsigned long>(r.requests),
               static_cast<unsigned long>(cost.freeAfter), static_cast<unsigned long>(cost.lowest));
    }
  }
}
