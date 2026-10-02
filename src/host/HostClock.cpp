// `mm::AgentClock` for the host build: the workstation's clock, which is always set.
//
// The board's clock is the RTC, corrected by SNTP or by the server's `serverTime`
// (src/net/Clock.cpp, not compiled here). A workstation has a clock before the process starts, so
// `source()` is `Source::Host` from the first call and `hasTime()` is true - which is what lets
// the harness exercise the parts of the firmware that refuse to run without a time: `clock.sync`
// sends `time <unix>` from it, and `leaseLapsed` is a real comparison against the server's real
// lease rather than the "a board with no clock cannot tell" branch.
//
// `takeServerTime` is honoured and reported, as it is on the board, and answers false: the host's
// clock is not moved by a server, because it is not ours to move. The difference is logged, which
// is how a host run says the workstation and the stack disagree.

#include <ctime>
#include <cstdio>

#include "Log.h"
#include "agent/Iso8601.h"
#include "net/Clock.h"

namespace mm {

volatile bool AgentClock::s_synced = false;

void AgentClock::begin(mesh::RTCClock&) {
  // Never called in the host build: there is no RTC to begin. The source is set in now()'s own
  // terms below, so a program that forgets to call this still has a clock.
  source_ = Source::Host;
}

void AgentClock::startSntp() { sntpStarted_ = false; }

void AgentClock::loop() {
  if (source_ == Source::None) source_ = Source::Host;
}

uint32_t AgentClock::now() const { return static_cast<uint32_t>(std::time(nullptr)); }

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
  const uint32_t local = now();
  difference = static_cast<int32_t>(serverEpoch - local);
  const int32_t magnitude = difference < 0 ? -difference : difference;
  if (magnitude > kToleranceS) {
    LOG_W("clock: the server's time is %ld s from this host's; the host's is kept (docs/host.md)",
          static_cast<long>(difference));
  }
  return false;
}

void AgentClock::describe(char* out, size_t cap) const {
  char when[24];
  formatUtc(now(), when, sizeof when);
  std::snprintf(out, cap, "%s (%s)", when, sourceName());
}

void AgentClock::oledLine(char* out, size_t cap) const {
  char hms[12];
  formatClock(now(), hms, sizeof hms);
  std::snprintf(out, cap, "%s UTC (%s)", hms, sourceName());
}

}  // namespace mm
