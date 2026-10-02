#include "Clock.h"

#include <Arduino.h>
#include <esp_sntp.h>

#include <cstdio>

#include "Log.h"
#include "agent/Iso8601.h"
#include "mesh/MeshCoreHeaders.h"  // Clock.h only forward-declares mesh::RTCClock (task p1.1-08)

namespace mm {

volatile bool AgentClock::s_synced = false;

void AgentClock::onSntpSync(::timeval*) { s_synced = true; }

void AgentClock::begin(mesh::RTCClock& rtc) {
  rtc_ = &rtc;
  if (rtc_->getCurrentTime() >= kEarliestPlausibleTime) {
    source_ = Source::Rtc;
    char when[24];
    formatUtc(rtc_->getCurrentTime(), when, sizeof when);
    LOG_I("clock: %s kept by the RTC across the reset", when);
  } else {
    LOG_I("clock: no time yet");
  }
}

void AgentClock::startSntp() {
  // The pool, three servers, UTC. configTzTime() stops and restarts SNTP, so calling it on every
  // reconnect is what the arduino-esp32 core expects.
  sntp_set_time_sync_notification_cb(&AgentClock::onSntpSync);
  configTzTime("UTC0", "0.pool.ntp.org", "1.pool.ntp.org", "2.pool.ntp.org");
  sntpStarted_ = true;
  LOG_I("clock: sntp started against pool.ntp.org");
}

void AgentClock::loop() {
  if (s_synced) {
    s_synced = false;
    // SNTP set the system clock; MeshCore's ESP32RTCClock reads time(), so nothing to copy - and
    // an I2C RTC, if AutoDiscoverRTCClock found one, is brought along here.
    const uint32_t t = rtc_->getCurrentTime();
    if (t < kEarliestPlausibleTime) {
      LOG_W("clock: sntp answered with an implausible time, ignored");
      return;
    }
    rtc_->setCurrentTime(t);
    const bool first = source_ != Source::Sntp;
    source_ = Source::Sntp;
    char when[24];
    formatUtc(t, when, sizeof when);
    if (first) {
      LOG_I("clock: %s from sntp", when);
    } else {
      LOG_D("clock: %s sntp resync", when);
    }
  }
}

uint32_t AgentClock::now() const {
  if (!hasTime() || rtc_ == nullptr) return 0;
  return rtc_->getCurrentTime();
}

const char* AgentClock::sourceName() const {
  switch (source_) {
    case Source::None: return "none";
    case Source::Rtc: return "rtc";
    case Source::Sntp: return "sntp";
    case Source::Server: return "server";
    case Source::Host: return "host";
  }
  return "?";
}

bool AgentClock::takeServerTime(uint32_t serverEpoch, int32_t& difference) {
  const uint32_t local = rtc_->getCurrentTime();
  difference = static_cast<int32_t>(serverEpoch - local);
  const int32_t magnitude = difference < 0 ? -difference : difference;

  if (source_ == Source::Sntp) {
    if (magnitude > kToleranceS) {
      LOG_W("clock: the server's time is %ld s from sntp's; sntp kept", static_cast<long>(difference));
    }
    return false;
  }
  if (hasTime() && magnitude <= kToleranceS) return false;

  rtc_->setCurrentTime(serverEpoch);
  source_ = Source::Server;
  char when[24];
  formatUtc(serverEpoch, when, sizeof when);
  LOG_I("clock: %s from the server (corrected by %ld s)", when, static_cast<long>(difference));
  return true;
}

void AgentClock::describe(char* out, size_t cap) const {
  if (!hasTime()) {
    std::snprintf(out, cap, "no time (%s)", sntpStarted_ ? "sntp has not answered" : "sntp not started, no wifi");
    return;
  }
  char when[24];
  formatUtc(now(), when, sizeof when);
  std::snprintf(out, cap, "%s (%s)", when, sourceName());
}

void AgentClock::oledLine(char* out, size_t cap) const {
  if (!hasTime()) {
    std::snprintf(out, cap, "no time");
    return;
  }
  char hms[12];
  formatClock(now(), hms, sizeof hms);
  std::snprintf(out, cap, "%s UTC (%s)", hms, sourceName());
}

}  // namespace mm
