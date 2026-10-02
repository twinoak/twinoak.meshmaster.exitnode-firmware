// `src/Health.h` for the host build: the reboot log, without the watchdog and without RTC memory.
//
// Two of the three things the board's src/Health.cpp does cannot exist here. There is no ESP-IDF
// task watchdog on a Linux process, and nothing survives the process: a host run's reboot log
// starts empty and ends with the run. What does survive is the **third**: the loop's rule that
// `kRebootAfterFailures` consecutive unanswered calls with the link up earn a deliberate restart,
// noted first. That rule is in src/agent/AgentLoop.cpp and is exercised here - the harness can
// point the host program at a dead port and watch it count to twelve and exit 70, which is a
// thing nobody can watch on a board without standing at it for an hour and a half.
//
// `rtcLogSurvived()` answers false, always, and says why on the console: a reader of a host run's
// `reboots` must not mistake an empty log for a board that has never crashed.

#include <cstdio>
#include <cstring>

#include "Health.h"
#include "Log.h"
#include "arduino/Arduino.h"
#include "arduino/esp_system.h"

namespace mm {

namespace {

struct Log {
  uint32_t count = 0;
  uint32_t reboots = 0;
  uint32_t selfReboots = 0;
  RebootRecord items[kRebootsRemembered] = {};
};

Log g_log;
const RebootRecord kNoRecord = {0, 0, {0}};
uint32_t (*s_clock)() = nullptr;
char s_bootReason[48] = {0};

void push(const char* why, uint32_t atEpoch, uint32_t uptimeS) {
  for (int i = kRebootsRemembered - 1; i > 0; i--) g_log.items[i] = g_log.items[i - 1];
  RebootRecord& r = g_log.items[0];
  r.atEpoch = atEpoch;
  r.uptimeS = uptimeS;
  std::snprintf(r.why, sizeof r.why, "%s", why == nullptr ? "unknown" : why);
  if (g_log.count < static_cast<uint32_t>(kRebootsRemembered)) g_log.count++;
}

}  // namespace

void healthBegin(uint32_t (*clock)(), const char* resetReason) {
  s_clock = clock;
  std::snprintf(s_bootReason, sizeof s_bootReason, "%s", resetReason == nullptr ? "the host harness started" : resetReason);
  LOG_I("watchdog: none in the host build - a Linux process has no task watchdog, and nothing here "
        "survives the process (docs/host.md)");
}

void healthTick() {}

void healthNoteReboot(const char* why) {
  push(why, s_clock != nullptr ? s_clock() : 0, millis() / 1000);
  g_log.selfReboots++;
  g_log.reboots++;
  LOG_W("reboots: the agent asked to restart - %s; in the host build that is the process exiting 70",
        why == nullptr ? "unknown" : why);
}

void healthRebootNow(const char* why) {
  healthNoteReboot(why);
  Serial.flush();
  esp_restart();
}

int rebootCount() { return static_cast<int>(g_log.count); }

const RebootRecord& rebootAt(int i) {
  if (i < 0 || static_cast<uint32_t>(i) >= g_log.count) return kNoRecord;
  return g_log.items[i];
}

uint32_t rebootsSeen() { return g_log.reboots; }

uint32_t selfReboots() { return g_log.selfReboots; }

const char* bootReason() { return s_bootReason; }

// Never: there is no RTC memory here, and an empty log must not read as a board that has never
// crashed. src/host/main_host.cpp's `reboots` says so in words.
bool rtcLogSurvived() { return false; }

}  // namespace mm
