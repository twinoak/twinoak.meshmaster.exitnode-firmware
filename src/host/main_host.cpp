// `pio run -e host` builds this: the firmware's whole agent loop on Linux, against a fake mesh and
// a plain-HTTP server (docs/host.md).
//
// What runs here is `src/agent/AgentLoop.cpp`, `src/agent/JobRunner.cpp`, every handler, the
// observation ring, the result buffer, the session cache, the airtime gate and the console parser -
// **the firmware's own files, not a port of them**. Two things are swapped, through the two
// interfaces task p1.1-08 put in: the mesh is `HostMesh` (a fake repeater written from MeshCore's
// source) and the server client is `HostClient` (plain HTTP to a private address). Everything else
// is the board's, including what it prints.
//
//   program --url http://127.0.0.1:8080 --token mma_... [--board heltec_v3] [--key <64 hex>]
//           [--scenario tests/fixtures/host/one-repeater.json] [--journal out/journal.json]
//           [--idle-exits 2] [--max-seconds 1800] [--log-level debug] [--frames] [--console]
//
// It exits 0 when the cycle it was asked for is done, 1 on a bad argument or an endpoint it may not
// open, 2 when `--max-seconds` ran out with work still going, and 70 when the agent loop asked to
// restart (twelve unanswered calls with the link up - src/Health.h), because a harness run that
// reboots has found something.

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "Health.h"
#include "Log.h"
#include "LogRing.h"
#include "agent/AgentLoop.h"
#include "agent/Iso8601.h"
#include "agent/JobRunner.h"
#include "arduino/Arduino.h"
#include "console/Console.h"
#include "host/HostClient.h"
#include "host/HostMesh.h"
#include "host/HostScenario.h"
#include "net/Clock.h"
#include "net/WifiLink.h"
#include "settings/Store.h"
#include "version.h"

namespace {

mm::SettingsStore settings;
mm::WifiLink wifi;
mm::AgentClock agentClock;
mm::HostClient server;
mm::HostMesh the_mesh;
mm::JobRunner jobs;
mm::AgentLoop agent;
mm::Console console;
mm::HostScenario scenario;

// The exit node's own identity. A board makes one on its radio's RNG and keeps it in SPIFFS; this
// one is an argument, with a default that is plainly a harness key and could never be a real
// node's - so a scenario's ACL, the restore's `setperm` and the heartbeat's `companionPublicKey`
// all name the same thing and a run is repeatable.
char public_key_hex[mm::kPublicKeyHexLen + 1] = "f7b8f191d0aa0d23976459c419f2d131371f1caecaef35289295f803078f2442";

bool consoleEnabled = false;
int idleExits = 2;
uint32_t maxSeconds = 1800;
const char* journalPath = nullptr;

uint32_t clockForLog() { return agentClock.now(); }

void printStatus(Stream& out) {
  char line[280];
  out.printf("firmware  %s (the host harness: no board, no radio - docs/host.md)\r\n", mm::firmwareString());
  out.printf("identity  %s\r\n", public_key_hex);
  agentClock.describe(line, sizeof line);
  out.printf("time      %s\r\n", line);
  wifi.describe(line, sizeof line);
  out.printf("link      %s\r\n", line);
  server.describe(line, sizeof line);
  out.printf("server    %s\r\n", line);
  agent.describe(line, sizeof line);
  out.printf("agent     %s\r\n", line);
  jobs.describe(line, sizeof line);
  out.printf("jobs      %s\r\n", line);
  out.printf("fake      %d repeater(s) in the scenario, %d transaction(s) seen\r\n", scenario.repeaterCount,
             the_mesh.transactionCount());
  out.printf("log       level %s, frames %s\r\n", mm::logLevelName(mm::logLevel()),
             the_mesh.frameLog() ? "on" : "off");
}

// `mesh` on the host console is the fake's journal: every frame this exit node sent, in order, with
// the sender timestamp it carried and whether anything came back.
void printMesh(Stream& out) {
  const mm::MeshCounters& c = the_mesh.counters();
  out.printf("fake      a repeater written from MeshCore at the pin, not a radio (src/host/HostMesh.h)\r\n");
  out.printf("frames    tx %" PRIu32 ", rx %" PRIu32 "\r\n", c.tx, c.rx);
  if (the_mesh.transactionCount() == 0) {
    out.println("journal   nothing sent yet");
    return;
  }
  out.println("journal   seq, repeater, kind, what, sender timestamp, route, answered");
  for (int i = 0; i < the_mesh.transactionCount(); i++) {
    const mm::HostTransaction& t = the_mesh.transaction(i);
    out.printf("  %-4d %s  %-9s %-40s %lu  %-6s %s\r\n", t.seq, t.repeater, t.kind, t.what,
               static_cast<unsigned long>(t.timestamp), t.flood ? "flood" : "direct",
               t.answered ? "answered" : "silence");
  }
}

void printServer(Stream& out) { agent.report(out); }
void printJobs(Stream& out) { jobs.report(out); }
void printSessions(Stream& out) { jobs.reportSessions(out); }
void printErrors(Stream& out) { agent.reportErrors(out); }
void printWifi(Stream& out) { wifi.scan(out); }

void printReboots(Stream& out) {
  out.printf("this boot %s; there is no watchdog in the host build\r\n", mm::bootReason());
  out.printf("counted   %lu restart(s), %lu of them the agent's own\r\n",
             static_cast<unsigned long>(mm::rebootsSeen()), static_cast<unsigned long>(mm::selfReboots()));
  out.println("log       empty at start and gone with the process: a host run has no RTC memory");
  if (mm::rebootCount() == 0) return;
  out.println("last five newest first: when, uptime at the time, why");
  char when[24];
  for (int i = 0; i < mm::rebootCount(); i++) {
    const mm::RebootRecord& r = mm::rebootAt(i);
    if (r.atEpoch != 0) {
      mm::formatUtc(r.atEpoch, when, sizeof when);
    } else {
      std::snprintf(when, sizeof when, "(no clock)");
    }
    out.printf("  %-22s up %lus  %s\r\n", when, static_cast<unsigned long>(r.uptimeS), r.why);
  }
}

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

void doReboot() {
  mm::healthNoteReboot("asked for on the console");
  mm::healthRebootNow("asked for on the console");
}

void onSettingChanged(const mm::KeyDef& key) {
  if (std::strncmp(key.name, "meshmaster.", 11) == 0) {
    server.reconfigure();
    agent.reconfigure();
  } else if (std::strncmp(key.name, "log.", 4) == 0) {
    char level[16];
    const mm::KeyDef* logLevel = mm::findKey("log.level");
    if (logLevel != nullptr) {
      settings.effective(*logLevel, level, sizeof level);
      mm::logSetLevelByName(level);
    }
  }
}

void onErased() {
  server.reconfigure();
  agent.reconfigure();
}

// The pump a job's waiting runs: on the board it keeps the console and the OLED alive and sends an
// overdue heartbeat. There is no OLED here and no watchdog, and the rest is the same.
void pumpDuringAJob() {
  if (consoleEnabled) console.loop();
  agent.heartbeatIfOverdue();
}

void usage() {
  std::printf(
      "the meshmaster agent firmware's host harness (docs/host.md)\n"
      "\n"
      "  --url <http://host:port>   the server: a loopback or RFC 1918 address, plain HTTP\n"
      "  --token <mma_...>          the agent token the exit node was registered with\n"
      "  --board <name>             what the heartbeat reports as `board` (default heltec_v3)\n"
      "  --key <64 hex>             this exit node's companion public key\n"
      "  --scenario <file>          the fake mesh (default: one repeater, see docs/host.md)\n"
      "  --journal <file>           write the fake's transaction journal here as JSON\n"
      "  --idle-exits <n>           stop after n claims in a row that brought no work (default 2)\n"
      "  --max-seconds <n>          stop with 2 after this long (default 1800)\n"
      "  --log-level <level>        error | warn | info | debug (default info)\n"
      "  --frames                   log one line per frame the fake saw\n"
      "  --console                  read the firmware's console from stdin\n");
}

}  // namespace

int main(int argc, char** argv) {
  const char* url = nullptr;
  const char* token = nullptr;
  const char* board = "heltec_v3";
  const char* scenarioPath = nullptr;
  const char* logLevel = "info";
  bool frames = false;

  for (int i = 1; i < argc; i++) {
    const char* arg = argv[i];
    const char* next = i + 1 < argc ? argv[i + 1] : nullptr;
    auto wants = [&](const char* name) { return std::strcmp(arg, name) == 0 && next != nullptr; };
    if (wants("--url")) { url = next; i++; }
    else if (wants("--token")) { token = next; i++; }
    else if (wants("--board")) { board = next; i++; }
    else if (wants("--key")) { std::snprintf(public_key_hex, sizeof public_key_hex, "%s", next); i++; }
    else if (wants("--scenario")) { scenarioPath = next; i++; }
    else if (wants("--journal")) { journalPath = next; i++; }
    else if (wants("--idle-exits")) { idleExits = std::atoi(next); i++; }
    else if (wants("--max-seconds")) { maxSeconds = static_cast<uint32_t>(std::atol(next)); i++; }
    else if (wants("--log-level")) { logLevel = next; i++; }
    else if (std::strcmp(arg, "--frames") == 0) { frames = true; }
    else if (std::strcmp(arg, "--console") == 0) { consoleEnabled = true; }
    else if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) { usage(); return 0; }
    else {
      std::fprintf(stderr, "unknown or incomplete argument: %s\n\n", arg);
      usage();
      return 1;
    }
  }
  if (std::strlen(public_key_hex) != mm::kPublicKeyHexLen) {
    std::fprintf(stderr, "--key must be %u hex characters\n", static_cast<unsigned>(mm::kPublicKeyHexLen));
    return 1;
  }

  mm::logBegin(&clockForLog);
  mm::logSetLevelByName(logLevel);
  settings.begin();
  agentClock.loop();  // there is no RTC to begin(): the host's own clock is the clock
  mm::healthBegin(&clockForLog, "the host harness started");

  LOG_I("%s", mm::firmwareString());
  LOG_I("host: the agent loop on this workstation, against a fake mesh - docs/host.md says what this proves and what "
        "it does not");
  LOG_I("identity: %s", public_key_hex);

  // The scenario first, so that the fake is ready before the first claim can arrive.
  mm::defaultScenario(scenario, public_key_hex);
  if (scenarioPath != nullptr) {
    std::FILE* f = std::fopen(scenarioPath, "rb");
    if (f == nullptr) {
      std::fprintf(stderr, "the scenario %s could not be opened\n", scenarioPath);
      return 1;
    }
    static char json[16384];
    const size_t len = std::fread(json, 1, sizeof json - 1, f);
    std::fclose(f);
    json[len] = '\0';
    char err[160];
    if (!mm::parseScenario(json, len, scenario, err, sizeof err)) {
      std::fprintf(stderr, "the scenario %s was refused: %s\n", scenarioPath, err);
      return 1;
    }
    LOG_I("scenario: %s, %d repeater(s)", scenarioPath, scenario.repeaterCount);
  } else {
    LOG_I("scenario: the default one repeater (no --scenario given)");
  }
  for (int i = 0; i < scenario.repeaterCount; i++) {
    const mm::HostRepeaterSpec& r = scenario.repeaters[i];
    LOG_I("scenario: %.12s... \"%s\" %d hop(s), %d ACL entr%s, %d neighbour(s)%s", r.key, r.name, r.hops, r.aclCount,
          r.aclCount == 1 ? "y" : "ies", r.neighbourCount, r.online ? "" : ", OFF the air");
  }

  // The settings the board takes over its serial console, taken from the command line instead. The
  // token goes where the board's goes - the settings store - and is wiped with the process; the URL
  // does **not**, because `meshmaster.url` means https:// and verified and this build may not write
  // anything else into it (src/host/HostClient.h).
  if (token != nullptr) {
    const mm::KeyDef* key = mm::findKey("meshmaster.token");
    if (key != nullptr) settings.put(*key, token);
  }
  // `board` is not a setting on the board either: it is the compile-time MESHMASTER_BOARD, and here
  // it is what `--board` says, so the exit node's page can say *onboard . Heltec V3* (src/version.h).
  mm::setBoardName(board);

  char why[200];
  if (url == nullptr || !server.setEndpoint(url, why, sizeof why)) {
    std::fprintf(stderr, "--url: %s\n", url == nullptr ? "missing" : why);
    return 1;
  }

  the_mesh.begin(scenario, public_key_hex);
  the_mesh.setClock(&clockForLog);
  the_mesh.setFrameLog(frames);

  wifi.begin(settings);
  server.begin(settings, agentClock, meshmaster_agent_version());
  jobs.begin(the_mesh, server, agentClock, &pumpDuringAJob);
  agent.begin(settings, server, agentClock, wifi, the_mesh, jobs);
  agent.setIdentity(public_key_hex);
  if (!server.configured()) {
    std::fprintf(stderr, "no token: pass --token mma_...\n");
    return 1;
  }

  mm::ConsoleHost host = {&printStatus, &printMesh,  &printServer,      &printJobs,
                          &printSessions, &printErrors, &printReboots,   &printDump,
                          &printWifi,     &doReboot,    &onSettingChanged, &onErased};
  if (consoleEnabled) console.begin(Serial, settings, host);

  // The loop, as src/main.cpp runs it, with the board's own parts taken out.
  const uint32_t startedAt = millis();
  uint32_t lastRan = 0;
  uint32_t lastClaims = 0;
  int idle = 0;
  int exitCode = 0;
  while (true) {
    mm::healthTick();
    the_mesh.loop();
    wifi.loop();
    agentClock.loop();
    if (wifi.takeConnectedEdge()) {
      agentClock.startSntp();
      agent.onWifiUp();
    }
    agent.loop();
    if (consoleEnabled) console.loop();

    if (agent.halted()) {
      LOG_E("host: the server refused the token (http 401): nothing more will be tried");
      exitCode = 1;
      break;
    }
    // A claim that brought no work, `idleExits` times in a row with nothing run between them, is
    // the cycle being over. The script queues the jobs before this program starts and reads the
    // journal after it stops.
    if (jobs.claims() != lastClaims) {
      lastClaims = jobs.claims();
      idle = jobs.ran() == lastRan ? idle + 1 : 0;
      lastRan = jobs.ran();
      if (idle >= idleExits) {
        LOG_I("host: %d claim(s) in a row brought no work: the cycle is done", idle);
        break;
      }
    }
    if ((millis() - startedAt) / 1000 >= maxSeconds) {
      LOG_E("host: --max-seconds (%lu) ran out with work still going", static_cast<unsigned long>(maxSeconds));
      exitCode = 2;
      break;
    }
    delay(5);
  }

  // What the run proved, for the script and for a person: the agent's own numbers, then the fake's.
  std::printf("\n");
  printStatus(Serial);
  printJobs(Serial);
  printMesh(Serial);
  Serial.flush();

  if (journalPath != nullptr) {
    static char journal[200000];
    const size_t len = the_mesh.writeJournal(journal, sizeof journal);
    if (len == 0) {
      std::fprintf(stderr, "the journal did not fit in %u bytes\n", static_cast<unsigned>(sizeof journal));
      exitCode = exitCode != 0 ? exitCode : 1;
    } else {
      std::FILE* f = std::fopen(journalPath, "wb");
      if (f == nullptr) {
        std::fprintf(stderr, "the journal %s could not be written\n", journalPath);
        exitCode = exitCode != 0 ? exitCode : 1;
      } else {
        std::fwrite(journal, 1, len, f);
        std::fclose(f);
        LOG_I("host: the journal is %s (%u bytes, %d transaction(s))", journalPath, static_cast<unsigned>(len),
              the_mesh.transactionCount());
      }
    }
  }
  return exitCode;
}
