#include "Health.h"

#include <Arduino.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <esp_task_wdt.h>

#include <cstdio>
#include <cstring>

#include "Log.h"

namespace mm {

namespace {

// RTC slow memory: kept across a software reset, a panic and a watchdog reset, cleared by a
// power cut. Nothing here has an initialiser - `RTC_NOINIT_ATTR` means "do not touch this at
// boot", and an initialised object in that section would either be zeroed or refuse to link.
struct RtcLog {
  uint32_t magic;
  uint32_t reboots;      // restarts since the last power cut
  uint32_t selfReboots;  // of those, the ones this firmware asked for
  uint32_t lastEpoch;    // the clock, a moment ago
  uint32_t lastUptimeS;  // the uptime, a moment ago
  uint32_t count;        // how many of items[] are real
  uint32_t pending;      // 1: items[0] explains the restart that is about to happen
  RebootRecord items[kRebootsRemembered];
};

RTC_NOINIT_ATTR RtcLog g_rtc;

// Bumped when the shape of RtcLog changes, so a firmware update cannot read the old one as the
// new one. "mmr" and a version.
constexpr uint32_t kMagic = 0x6D6D7201;

const RebootRecord kNoRecord = {0, 0, {0}};

uint32_t (*s_clock)() = nullptr;
char s_bootReason[48] = {0};
bool s_survived = false;
bool s_watchdog = false;

void push(const char* why, uint32_t atEpoch, uint32_t uptimeS) {
  for (int i = kRebootsRemembered - 1; i > 0; i--) g_rtc.items[i] = g_rtc.items[i - 1];
  RebootRecord& r = g_rtc.items[0];
  r.atEpoch = atEpoch;
  r.uptimeS = uptimeS;
  std::snprintf(r.why, sizeof r.why, "%s", why == nullptr ? "unknown" : why);
  if (g_rtc.count < static_cast<uint32_t>(kRebootsRemembered)) g_rtc.count++;
}

}  // namespace

void healthBegin(uint32_t (*clock)(), const char* resetReason) {
  s_clock = clock;
  std::snprintf(s_bootReason, sizeof s_bootReason, "%s", resetReason == nullptr ? "unknown" : resetReason);

  if (g_rtc.magic != kMagic) {
    // A power cut, or the first boot of this firmware: there is nothing to remember and nothing
    // to infer. Everything after this reads a log that starts here.
    std::memset(&g_rtc, 0, sizeof g_rtc);
    g_rtc.magic = kMagic;
    s_survived = false;
  } else {
    s_survived = true;
    g_rtc.reboots++;
    if (g_rtc.pending != 0) {
      // The newest entry is this restart's reason, written before it happened.
      g_rtc.pending = 0;
    } else {
      // Nothing asked for this one: a panic, the watchdog, a brownout, the reset button. The
      // board's own name for the reset is the best reason there is, timed from the last tick.
      push(s_bootReason, g_rtc.lastEpoch, g_rtc.lastUptimeS);
    }
  }
  g_rtc.lastEpoch = 0;
  g_rtc.lastUptimeS = 0;

  // The watchdog on this task - setup() and loop() are the same task, and on this firmware that
  // task is also the network: every call to meshmaster.dk is made from it.
  const esp_err_t initialised = esp_task_wdt_init(kWatchdogSeconds, true);
  const esp_err_t added = esp_task_wdt_add(nullptr);
  s_watchdog = initialised == ESP_OK && added == ESP_OK;
  if (s_watchdog) {
    LOG_I("watchdog: %lu s on the loop task, which is this board's network task too",
          static_cast<unsigned long>(kWatchdogSeconds));
  } else {
    LOG_W("watchdog: could not be started (init %d, add %d); the board is not watched",
          static_cast<int>(initialised), static_cast<int>(added));
  }

  if (s_survived && g_rtc.count > 0) {
    LOG_W("reboots: this board restarted - %s; `reboots` on the console has the last %d",
          g_rtc.items[0].why, kRebootsRemembered);
  }
}

void healthTick() {
  if (s_watchdog) esp_task_wdt_reset();
  g_rtc.lastEpoch = s_clock != nullptr ? s_clock() : 0;
  g_rtc.lastUptimeS = millis() / 1000;
}

void healthNoteReboot(const char* why) {
  if (g_rtc.magic != kMagic) {
    std::memset(&g_rtc, 0, sizeof g_rtc);
    g_rtc.magic = kMagic;
  }
  push(why, s_clock != nullptr ? s_clock() : 0, millis() / 1000);
  g_rtc.pending = 1;
  g_rtc.selfReboots++;
  LOG_W("reboots: restarting - %s", why == nullptr ? "unknown" : why);
}

void healthRebootNow(const char* why) {
  healthNoteReboot(why);
  Serial.flush();
  delay(100);
  esp_restart();
}

int rebootCount() { return static_cast<int>(g_rtc.count); }

const RebootRecord& rebootAt(int i) {
  if (i < 0 || static_cast<uint32_t>(i) >= g_rtc.count) return kNoRecord;
  return g_rtc.items[i];
}

uint32_t rebootsSeen() { return g_rtc.reboots; }

uint32_t selfReboots() { return g_rtc.selfReboots; }

const char* bootReason() { return s_bootReason; }

bool rtcLogSurvived() { return s_survived; }

}  // namespace mm
