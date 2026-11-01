// The board's clock: SNTP against the pool once WiFi is up, and the server's serverTime as the
// fallback when NTP is blocked (EXIT-NODES section 5, "Clock"; the .NET agent's AgentClock).
//
// The clock is what every login and every sender timestamp will depend on from task 04 on, so
// `status` says where the time came from and "no time" until it came from somewhere; the OLED's
// first line is the clock itself, or "no time" (src/ui/DisplayLines.h).
// The tolerance is the .NET agent's: five seconds. SNTP, once it has answered, wins over the
// server; the server's time is taken when there is nothing better and it disagrees by more.

#pragma once

#include <sys/time.h>

#include <cstddef>
#include <cstdint>

#include <agent/IPlatform.h>

#include "mesh/MeshCoreHeaders.h"

namespace mm {

// The clock half of the platform seam (agent/IPlatform.h's IClock): what the runner's lease
// check, the heartbeat's `at` and every result's timestamps read.
class AgentClock : public IClock {
 public:
  enum class Source : uint8_t {
    None,    // the RTC holds MeshCore's seed (1 March 2026 since 1.18) or something implausible
    Rtc,     // the RTC held a plausible time at boot (kept across a software reset, or restored
             // from MeshCore's RTC backup after a crash - kEarliestPlausibleTime below)
    Sntp,    // the pool answered
    Server,  // taken from a config reply's serverTime
  };

  static constexpr int32_t kToleranceS = 5;

  void begin(mesh::RTCClock& rtc);

  // When WiFi comes up: start (or restart) SNTP.
  void startSntp();

  void loop();

  bool hasTime() const override { return source_ != Source::None; }
  uint32_t now() const override;
  Source source() const { return source_; }
  const char* sourceName() const override;
  // SNTP having answered, or the server's own time having been taken, means a packet of ours
  // left this board and something answered it; an RTC that held a time across a software reset
  // proves nothing about the network (task p1.2-10).
  bool fromNetwork() const override { return source_ == Source::Sntp || source_ == Source::Server; }

  // Offer the server's time. Returns true when the clock was set from it; `difference` is
  // server minus local before any change, for the log.
  bool takeServerTime(uint32_t serverEpoch, int32_t& difference) override;

  // One line for `status`: "2026-09-28T12:03:14Z (sntp)" or "no time (sntp not answered yet)".
  void describe(char* out, size_t cap) const;

 private:
  static void onSntpSync(::timeval* tv);
  static volatile bool s_synced;

  mesh::RTCClock* rtc_ = nullptr;
  Source source_ = Source::None;
  bool sntpStarted_ = false;
};

// Earlier than this is not a time this firmware can have been running at: 2026-11-01T00:00:00Z,
// the day of the task that set it (p1.8-34). It is how "no time" is told from a time, and it has
// to sit **above MeshCore's seed**: since 1.18 (`35142a88`, #1896) ESP32RTCClock::begin seeds the
// system clock at 1 March 2026 (RTC_TIME_MIN, src/helpers/ESP32Board.h:206-248 at 81dbae59) on
// every reset that has no valid RTC backup, and restores the backup - the last time this board
// ran at, kept in RTC_NOINIT memory across a crash, a watchdog or a brownout - when it has one.
// At 1.17.1 the seed was 15 May 2024 and the floor was 1 January 2025; left there, a fresh
// power-on would have been read as "kept by the RTC" with March 2026 on the clock. A board that
// has run for eight months on the seed alone, never reaching SNTP or the server, would cross
// this floor - and would have been an exit node offline for eight months first.
constexpr uint32_t kEarliestPlausibleTime = 1793491200u;

}  // namespace mm
