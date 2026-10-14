// The MeshMaster agent firmware: a Heltec V3 that boots, takes its settings over the USB serial
// console, joins WiFi, sets its clock, and is an exit node on meshmaster.dk - it heartbeats over
// verified TLS, posts the adverts its radio hears, and claims and answers the jobs of EXIT-NODES
// §7: the reads that log in as this board - status, telemetry, neighbours and trace (task 03) -
// discover, acl.set and config.read, which carry a repeater's password for the length of the
// job and nothing longer (task 04, src/agent/Credentials.h), and the six that change a repeater
// - config.apply, cli, advert.request, clock.sync, password.set, reboot - each one send per
// command (task 05, src/agent/ConfigApply.h). What leaves the radio is one packet per
// transaction and nothing else (src/mesh/AgentMesh.h).
//
// The pieces, each in its own file:
//   settings/   the key table (host-portable) and the NVS store
//   console/    the line parser (host-portable) and the console that acts on it
//   net/        WiFi with backoff, and the clock (SNTP, the server as fallback)
//   agent/      the TLS client, the root it verifies against, the envelopes and the reply
//               structs (host-portable), the observation ring, the airtime budget, the session
//               cache, the job runner, and the loop that drives them - the runner and the loop
//               on three seams since task p1.5-15 (IMesh, IUplink, IPlatform), which this file
//               wires to the real things below
//   platform/   the Arduino-on-ESP32 end of the platform seam: millis, delay, random, the heap,
//               the reboot for health
//   mesh/       the mesh in companion role: hears adverts, forwards nothing, and speaks a job
//   ui/         the OLED's four lines, as a pure function over a struct this file fills
//   Log.h       what the serial port prints besides the console's replies, and the 4 kB ring
//               `dump` prints it back out of (LogRing.h)
//   Health.h    the task watchdog, the deliberate reboot on a wedged stack, and why this board
//               last restarted - kept in RTC memory, printed by `reboots` (task 06)

#include <Arduino.h>
#include <SPIFFS.h>

#include <cinttypes>
#include <cstdio>
#include <cstring>

#include "Health.h"
#include "Log.h"
#include "LogRing.h"
#include "mesh/MeshCoreHeaders.h"
#include "agent/AgentClient.h"
#include "agent/AgentLoop.h"
#include "agent/Iso8601.h"
#include "agent/JobRunner.h"
#include "console/Console.h"
#include "console/StreamOut.h"
#include "mesh/AgentMesh.h"
#include "net/Clock.h"
#include "net/WifiLink.h"
#include "platform/Esp32Platform.h"
#include "settings/Store.h"
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

// What the heartbeat says about this board (src/agent/AgentLoop.h): the version, the board, the
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
  char line[320];
  char key[80];
  char uptime[32];
  formatUptime(uptime, sizeof uptime);
  publicKeyHex(key, sizeof key);

  out.printf("firmware  %s (%.8s)\r\n", mm::firmwareString(), mm::meshcorePin());
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

  const mm::AgentClient::Outcome& o = server.last();
  out.printf("heap      free %lu, min ever %lu, largest block %lu", static_cast<unsigned long>(ESP.getFreeHeap()),
             static_cast<unsigned long>(ESP.getMinFreeHeap()), static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  if (o.attempted && o.heapAfter != 0) {
    out.printf("; after the tls call %lu", static_cast<unsigned long>(o.heapAfter));
  }
  out.print("\r\n");
  out.printf("log       level %s, frames %s, oled %s\r\n", mm::logLevelName(mm::logLevel()), the_mesh.frameLog() ? "on" : "off",
             display_ok ? (oled.isOn() ? "on" : "off") : "not found");
}

void printWifi(Stream& out) { wifi.scan(out); }

// `mesh`: what the radio has heard. The names are printed here and in no log line - this is the
// one place a person reads them, and they come from strangers, so they are the bounded, valid
// UTF-8 the ring kept (src/agent/Observations.h).
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
// the heartbeat itself carries. The library's reports write to a TextOut (src/agent/TextOut.h);
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
// already have carried it to a terminal (src/LogRing.h).
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
  // And the heartbeat that fell due while the job was waiting for the budget (task 06): a claim
  // is run to the end before the loop gets its turn back, and a board that is working hard is
  // not a board the server should be marking *quiet*.
  agent.heartbeatIfOverdue();
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
  }
}

void onErased() {
  applyLogSettings();
  applyDisplaySettings();
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

  mm::logBegin(&clockForLog);
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
  wifi.begin(settings);
  server.begin(settings, agentClock, wifi, meshmaster_agent_version());
  fillIdentity();
  // The runner and the loop on their three seams (src/agent/IMesh.h, IUplink.h, IPlatform.h):
  // the mesh in this process, the WiFi client, and Arduino on this ESP32. The Heltec sets no job
  // extension (IJobExtension.h).
  jobs.begin(the_mesh, server, platform, &pumpDuringAJob);
  agent.begin(server, platform, the_mesh, jobs, identity);
  agent.setIdentity(public_key_hex[0] != 0 ? public_key_hex : nullptr);
  if (!server.configured()) LOG_I("server: not configured; set meshmaster.token (meshmaster.url is https://meshmaster.dk unless set)");
  if (display_ok) {
    applyDisplaySettings();  // `display.timeout`, before the first frame starts its clock
    oled.begin(&display, &user_btn, &fillOled);  // the first frame, now that its sources exist
  }

  mm::ConsoleHost host = {&printStatus,  &printMesh,    &printServer, &printJobs,
                          &printSessions, &printErrors,  &printReboots, &printDump,
                          &printWifi,     &doReboot,     &onSettingChanged, &onErased};
  console.begin(Serial, settings, host);
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
  // one blocking call at a time, and nothing at all after a 401 (src/agent/AgentLoop.h).
  agent.loop();

  console.loop();
  oled.loop();
}
