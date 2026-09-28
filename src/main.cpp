// The MeshMaster agent firmware, task 01: a Heltec V3 that boots, takes its settings over the
// USB serial console, joins WiFi, sets its clock, and makes one authenticated HTTPS call to
// meshmaster.dk (GET /agent/v1/config) with the certificate chain verified. The radio is
// initialised so the identity exists, and no packet leaves (src/mesh/AgentMesh.h).
//
// The pieces, each in its own file:
//   settings/   the key table (host-portable) and the NVS store
//   console/    the line parser (host-portable) and the console that acts on it
//   net/        WiFi with backoff, and the clock (SNTP, the server as fallback)
//   agent/      the TLS client, the root it verifies against, the reply structs (host-portable)
//   mesh/       the mesh in companion role: counts frames, sends nothing
//   ui/         the OLED's four lines
//   Log.h       what the serial port prints besides the console's replies

#include <Arduino.h>
#include <SPIFFS.h>

#include <cinttypes>
#include <cstdio>
#include <cstring>

#include "Log.h"
#include "mesh/MeshCoreHeaders.h"
#include "agent/AgentClient.h"
#include "agent/Iso8601.h"
#include "console/Console.h"
#include "mesh/AgentMesh.h"
#include "net/Clock.h"
#include "net/WifiLink.h"
#include "settings/Store.h"
#include "ui/Oled.h"
#include "version.h"

// ---- the objects ----------------------------------------------------------------------------
// board, radio_driver, rtc_clock, display and user_btn come from MeshCore's variants/heltec_v3
// (target.h); the rest is ours.

namespace {

mm::SettingsStore settings;
mm::WifiLink wifi;
mm::AgentClock agentClock;
mm::AgentClient server;
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

// The config call: once per WiFi connection, retried with backoff until it succeeds.
bool configDue = false;
uint32_t configAt = 0;
uint32_t configBackoffMs = 0;
constexpr uint32_t kConfigFirstBackoffMs = 30000;
constexpr uint32_t kConfigMaxBackoffMs = 10 * 60000;

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

void applyLogSettings() {
  char buf[16];
  settings.effective(*mm::findKey("log.level"), buf, sizeof buf);
  mm::logSetLevelByName(buf);
  settings.effective(*mm::findKey("log.frames"), buf, sizeof buf);
  the_mesh.setFrameLog(std::strcmp(buf, "on") == 0);
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
  if (!identity_ok || cap < 65) {
    std::snprintf(out, cap, "none");
    return;
  }
  mesh::Utils::toHex(out, the_mesh.self_id.pub_key, PUB_KEY_SIZE);
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

void doReboot() {
  delay(100);
  board.reboot();
}

void scheduleConfigCall(uint32_t delayMs) {
  configDue = true;
  configAt = millis() + delayMs;
}

void onSettingChanged(const mm::KeyDef& key) {
  if (std::strncmp(key.name, "wifi.", 5) == 0) {
    wifi.reconfigure();
  } else if (std::strncmp(key.name, "meshmaster.", 11) == 0) {
    server.reconfigure();
    configBackoffMs = 0;
    if (wifi.connected()) scheduleConfigCall(0);
  } else if (std::strncmp(key.name, "radio.", 6) == 0) {
    if (radio_ok) applyRadioSettings();
  } else if (std::strncmp(key.name, "log.", 4) == 0) {
    applyLogSettings();
  }
}

void onErased() {
  applyLogSettings();
  server.reconfigure();
  configDue = false;
  wifi.reconfigure();
  if (radio_ok) applyRadioSettings();
}

// ---- the OLED -------------------------------------------------------------------------------

void fillOled(char lines[mm::Oled::kLines][mm::Oled::kCols]) {
  wifi.oledLine(lines[0], mm::Oled::kCols);
  agentClock.oledLine(lines[1], mm::Oled::kCols);
  server.oledLine(lines[2], mm::Oled::kCols);
  if (identity_ok) {
    char key[80];
    publicKeyHex(key, sizeof key);
    std::snprintf(lines[3], mm::Oled::kCols, "id %.8s  agent", key);
  } else {
    std::snprintf(lines[3], mm::Oled::kCols, "no identity");
  }
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

  display_ok = display.begin();
  if (display_ok) {
    oled.begin(&display, &user_btn, &fillOled);
  } else {
    LOG_W("oled: not found");
  }

  radio_ok = radio_init();
  if (radio_ok) {
    fast_rng.begin(radio_driver.getRngSeed());
    SPIFFS.begin(true);
    setUpIdentity();
    applyRadioSettings();
    the_mesh.begin();
    char key[80];
    publicKeyHex(key, sizeof key);
    LOG_I("identity: %s", key);
  } else {
    LOG_E("radio: init failed; the console works, the mesh does not");
  }

  agentClock.begin(rtc_clock);
  wifi.begin(settings);
  server.begin(settings, agentClock, meshmaster_agent_version());
  if (!server.configured()) LOG_I("server: not configured; set meshmaster.url and meshmaster.token");

  mm::ConsoleHost host = {&printStatus, &printWifi, &doReboot, &onSettingChanged, &onErased};
  console.begin(Serial, settings, host);
  board.onBootComplete();
}

void loop() {
  if (radio_ok) the_mesh.loop();
  rtc_clock.tick();
  wifi.loop();
  agentClock.loop();

  if (wifi.takeConnectedEdge()) {
    agentClock.startSntp();
    configBackoffMs = 0;
    scheduleConfigCall(1500);  // a moment for DNS and SNTP to get going
  }

  if (configDue && wifi.connected() && static_cast<int32_t>(millis() - configAt) >= 0) {
    configDue = false;
    if (server.configured()) {
      if (!server.fetchConfig()) {
        configBackoffMs = configBackoffMs == 0 ? kConfigFirstBackoffMs : configBackoffMs * 2;
        if (configBackoffMs > kConfigMaxBackoffMs) configBackoffMs = kConfigMaxBackoffMs;
        LOG_I("server: next attempt in %lu s", static_cast<unsigned long>(configBackoffMs / 1000));
        scheduleConfigCall(configBackoffMs);
      }
    }
  }

  console.loop();
  oled.loop();
}
