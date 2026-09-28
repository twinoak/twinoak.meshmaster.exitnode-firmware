// The board's clock: SNTP against the pool once WiFi is up, and the server's serverTime as the
// fallback when NTP is blocked (EXIT-NODES section 5, "Clock"; the .NET agent's AgentClock).
//
// The clock is what every login and every sender timestamp will depend on from task 04 on, so
// `status` and the OLED say where the time came from and "no time" until it came from somewhere.
// The tolerance is the .NET agent's: five seconds. SNTP, once it has answered, wins over the
// server; the server's time is taken when there is nothing better and it disagrees by more.

#pragma once

#include <sys/time.h>

#include <cstddef>
#include <cstdint>

#include "mesh/MeshCoreHeaders.h"

namespace mm {

class AgentClock {
 public:
  enum class Source : uint8_t {
    None,    // the RTC holds MeshCore's placeholder (15 May 2024) or something implausible
    Rtc,     // the RTC held a plausible time at boot (kept across a software reset)
    Sntp,    // the pool answered
    Server,  // taken from a config reply's serverTime
  };

  static constexpr int32_t kToleranceS = 5;

  void begin(mesh::RTCClock& rtc);

  // When WiFi comes up: start (or restart) SNTP.
  void startSntp();

  void loop();

  bool hasTime() const { return source_ != Source::None; }
  uint32_t now() const;
  Source source() const { return source_; }
  const char* sourceName() const;

  // Offer the server's time. Returns true when the clock was set from it; `difference` is
  // server minus local before any change, for the log.
  bool takeServerTime(uint32_t serverEpoch, int32_t& difference);

  // One line for `status`: "2026-09-28T12:03:14Z (sntp)" or "no time (sntp not answered yet)".
  void describe(char* out, size_t cap) const;

  // The OLED's second line: "12:03:14 UTC" or "no time".
  void oledLine(char* out, size_t cap) const;

 private:
  static void onSntpSync(::timeval* tv);
  static volatile bool s_synced;

  mesh::RTCClock* rtc_ = nullptr;
  Source source_ = Source::None;
  bool sntpStarted_ = false;
};

// Earlier than this is not a time this firmware can have been running at: 2025-01-01T00:00:00Z.
// MeshCore's fallback clock starts at 15 May 2024 on a power-on, which is how "no time" is told
// from a time.
constexpr uint32_t kEarliestPlausibleTime = 1735689600u;

}  // namespace mm
