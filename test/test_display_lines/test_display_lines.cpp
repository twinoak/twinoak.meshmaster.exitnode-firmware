// The OLED's four lines (task p1.2-09): one case per state, with the exact strings a person at
// the window reads, and one that holds every one of them to the font's 21 characters.
//
// This is the whole of the display that runs on a host. The toggle and the timeout live in
// src/ui/Oled.h, which needs a DisplayDriver and a MomentaryButton, so they are the bench's -
// tasks/phase-1.2/reports/09.md lists the four checks.

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include <agent/Iso8601.h>
#include "ui/DisplayLines.h"

using mm::DisplayFault;
using mm::DisplayState;
using mm::kDisplayCols;
using mm::kDisplayLines;
using mm::kDisplayWidth;
using mm::renderLines;

namespace {

// The identity meshmaster.dk shows as 46F6, lowercase on the board as it is in SPIFFS.
const char* kKey = "46f6a1b2c3d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e";

uint32_t at(const char* utc) {
  uint32_t epoch = 0;
  EXPECT_TRUE(mm::parseUtc(utc, epoch)) << utc;
  return epoch;
}

// One frame, as four std::strings.
struct Frame {
  std::string line[kDisplayLines];

  explicit Frame(const DisplayState& state) {
    char lines[kDisplayLines][kDisplayCols];
    std::memset(lines, 'x', sizeof lines);  // renderLines owes us a NUL on every line
    renderLines(state, lines);
    for (int i = 0; i < kDisplayLines; i++) {
      EXPECT_EQ('\0', lines[i][kDisplayCols - 1]) << "line " << (i + 1) << " is not terminated";
      line[i] = lines[i];
      EXPECT_LE(line[i].size(), static_cast<size_t>(kDisplayWidth))
          << "line " << (i + 1) << " is wider than the font: '" << line[i] << "'";
    }
  }
};

// A board at the window with everything working: an identity, a clock, WiFi, the server, and a
// job that ran a minute ago.
DisplayState working() {
  DisplayState s;
  s.publicKeyHex = kKey;
  s.hasTime = true;
  s.nowEpoch = at("2026-10-04T12:03:14Z");
  s.wifiConnected = true;
  s.serverConfigured = true;
  s.online = true;
  s.everHeartbeated = true;
  s.lastHeartbeatAtEpoch = at("2026-10-04T12:03:14Z");
  s.hasLastJob = true;
  s.lastJobAtEpoch = at("2026-10-04T12:34:56Z");
  return s;
}

// Build a frame and throw it away: the constructor is where the width and the NUL are asserted.
void checkFrame(const DisplayState& state) {
  const Frame f(state);
  (void)f;
}

}  // namespace

// ---- line one: who, and the clock -----------------------------------------------------------

TEST(LineOne, TheIdentityPrefixLeftAndTheClockRightAligned) {
  const Frame f(working());
  EXPECT_EQ("ID: 46F6     12:03:14", f.line[0]);
  EXPECT_EQ(static_cast<size_t>(kDisplayWidth), f.line[0].size());
}

TEST(LineOne, NoTimeTakesTheClocksPlaceAndIsAlsoRightAligned) {
  DisplayState s = working();
  s.hasTime = false;
  const Frame f(s);
  EXPECT_EQ("ID: 46F6      no time", f.line[0]);
  EXPECT_EQ(static_cast<size_t>(kDisplayWidth), f.line[0].size());
}

TEST(LineOne, NoIdentityIsTheWholeLine) {
  DisplayState s = working();
  s.publicKeyHex = nullptr;
  EXPECT_EQ("no identity", Frame(s).line[0]);

  s.publicKeyHex = "";  // the radio came up but made no key
  EXPECT_EQ("no identity", Frame(s).line[0]);
}

// ---- line two: WiFi and Online --------------------------------------------------------------

TEST(LineTwo, BothYesIsExactlyTheFontsTwentyOneCharacters) {
  const Frame f(working());
  EXPECT_EQ("WiFi: yes Online: yes", f.line[1]);
  EXPECT_EQ(static_cast<size_t>(kDisplayWidth), f.line[1].size());
}

TEST(LineTwo, EachOfTheFourCombinations) {
  DisplayState s = working();
  s.wifiConnected = true;
  s.online = false;
  EXPECT_EQ("WiFi: yes Online: no", Frame(s).line[1]);
  s.wifiConnected = false;
  EXPECT_EQ("WiFi: no Online: no", Frame(s).line[1]);
  s.online = true;  // not a state the board reaches; the line still says what it is told
  EXPECT_EQ("WiFi: no Online: yes", Frame(s).line[1]);
}

// ---- line three: the heartbeat --------------------------------------------------------------

TEST(LineThree, TheLastHeartbeatOrNoneBeforeTheFirst) {
  DisplayState s = working();
  s.lastHeartbeatAtEpoch = at("2026-10-04T09:00:07Z");
  EXPECT_EQ("Heartbeat: 09:00:07", Frame(s).line[2]);

  s.everHeartbeated = false;
  EXPECT_EQ("Heartbeat: none", Frame(s).line[2]);
}

TEST(LineThree, WithAnObserverSlotConfiguredTheBrokerCountTakesTheRightEdge) {
  // Task p1.7-27: "HB <clock>" at the left, "MQTT <up>/<configured>" against the right edge,
  // exactly the font's 21 characters; with no slot configured the line is as it was.
  DisplayState s = working();
  s.lastHeartbeatAtEpoch = at("2026-10-04T12:03:14Z");
  s.observerSlotsConfigured = 2;
  s.observerSlotsConnected = 2;
  EXPECT_EQ("HB 12:03:14  MQTT 2/2", Frame(s).line[2]);
  EXPECT_EQ(static_cast<size_t>(kDisplayWidth), Frame(s).line[2].size());

  s.observerSlotsConnected = 1;
  EXPECT_EQ("HB 12:03:14  MQTT 1/2", Frame(s).line[2]);
  s.observerSlotsConnected = 0;
  s.observerSlotsConfigured = 1;
  EXPECT_EQ("HB 12:03:14  MQTT 0/1", Frame(s).line[2]);

  s.everHeartbeated = false;
  EXPECT_EQ("HB none      MQTT 0/1", Frame(s).line[2]);

  s.observerSlotsConfigured = 0;
  EXPECT_EQ("Heartbeat: none", Frame(s).line[2]) << "the observer off: nothing about MQTT on the panel";
  s.everHeartbeated = true;
  EXPECT_EQ("Heartbeat: 12:03:14", Frame(s).line[2]);
}

// ---- line four: what it is doing ------------------------------------------------------------

TEST(LineFour, TheOpenJobsIdAndType) {
  DisplayState s = working();
  s.jobOpen = true;
  s.jobId = 1234;
  s.jobType = "acl.set";
  EXPECT_EQ("Job 1234 acl.set", Frame(s).line[3]);
}

TEST(LineFour, ALongJobLineIsTruncatedToTheFont) {
  DisplayState s = working();
  s.jobOpen = true;
  s.jobId = 1234567890;
  s.jobType = "config.apply";
  const Frame f(s);
  EXPECT_EQ("Job 1234567890 config", f.line[3]);
  EXPECT_EQ(static_cast<size_t>(kDisplayWidth), f.line[3].size());
}

TEST(LineFour, IdleWithTheNewestJobsTimeAndBeforeAnyJob) {
  DisplayState s = working();
  EXPECT_EQ("Idle, last 12:34:56", Frame(s).line[3]);

  s.hasLastJob = false;
  EXPECT_EQ("Idle, no jobs yet", Frame(s).line[3]);
}

TEST(LineFour, TheFourFaults) {
  DisplayState s = working();

  s.wifiConnected = false;
  s.online = false;
  EXPECT_EQ(DisplayFault::NoWifi, mm::faultOf(s));
  EXPECT_EQ("no wifi", Frame(s).line[3]);

  s = working();
  s.online = false;
  s.backingOff = true;
  s.retrySeconds = 30;
  EXPECT_EQ(DisplayFault::Backoff, mm::faultOf(s));
  EXPECT_EQ("no server: retry 30s", Frame(s).line[3]);

  s = working();
  s.online = false;
  s.tokenRefused = true;
  EXPECT_EQ(DisplayFault::TokenRefused, mm::faultOf(s));
  EXPECT_EQ("token revoked/wrong", Frame(s).line[3]);

  // The state the first merged flash of task p1.2-11 leaves: WiFi up, no token, no heartbeat.
  // It read `Idle, no jobs yet` under `Online: no` until task p1.3-12.
  s = working();
  s.serverConfigured = false;
  s.online = false;
  s.everHeartbeated = false;
  s.hasLastJob = false;
  EXPECT_EQ(DisplayFault::NoToken, mm::faultOf(s));
  const Frame f(s);
  EXPECT_EQ("WiFi: yes Online: no", f.line[1]);
  EXPECT_EQ("Heartbeat: none", f.line[2]);
  EXPECT_EQ("no token: set it", f.line[3]);
}

TEST(LineFour, ARefusedTokenComesFirstThenTheLinkThenTheTokenThenTheBackoff) {
  DisplayState s = working();
  s.online = false;
  s.tokenRefused = true;
  s.wifiConnected = false;
  s.serverConfigured = false;
  s.backingOff = true;
  s.retrySeconds = 45;
  EXPECT_EQ("token revoked/wrong", Frame(s).line[3]);

  s.tokenRefused = false;
  EXPECT_EQ("no wifi", Frame(s).line[3]) << "a board with no WiFi cannot use a token yet";

  s.wifiConnected = true;
  EXPECT_EQ("no token: set it", Frame(s).line[3]) << "an unprovisioned board never gets as far as a backoff";

  s.serverConfigured = true;
  EXPECT_EQ("no server: retry 45s", Frame(s).line[3]);
}

// The hold a deploy's 503 makes, which `online()` and `DisplayState.backingOff` both read from
// `AgentLoop::retrySeconds()` since task p1.3-12: before it, a believed `Retry-After` moved no
// counter, so the panel said `Online: yes` and `Idle` through the whole of it (finding 2).
TEST(LineFour, ARetryAfterHoldReadsAsTheWaitItIs) {
  DisplayState s = working();
  s.online = false;      // AgentLoop::online() is false while the hold runs
  s.backingOff = true;   // retrySeconds() > 0, and not one consecutive failure
  s.retrySeconds = 45;   // the 503's own number
  const Frame f(s);
  EXPECT_EQ("WiFi: yes Online: no", f.line[1]);
  EXPECT_EQ("Heartbeat: 12:03:14", f.line[2]) << "the heartbeat that did answer is still the last one";
  EXPECT_EQ("no server: retry 45s", f.line[3]);
}

TEST(LineFour, AnOpenJobIsWhatItIsDoingWhateverElseIsWrong) {
  // Nothing runs in the three fault states, so this is belt and braces: a job on the air is
  // what the line says, because that is what a person is looking for.
  DisplayState s = working();
  s.jobOpen = true;
  s.jobId = 7;
  s.jobType = "status";
  s.backingOff = true;
  s.retrySeconds = 60;
  EXPECT_EQ("Job 7 status", Frame(s).line[3]);
}

TEST(LineFour, TheBackoffCountdownNeverOverflowsTheLine) {
  DisplayState s = working();
  s.online = false;
  s.backingOff = true;
  s.retrySeconds = 600;  // the backoff's own ceiling, ten minutes
  EXPECT_EQ("no server: retry 600s", Frame(s).line[3]);
  EXPECT_EQ(static_cast<size_t>(kDisplayWidth), Frame(s).line[3].size());

  s.retrySeconds = 86400;  // a Retry-After of a day: the line reads 999s rather than growing
  EXPECT_EQ("no server: retry 999s", Frame(s).line[3]);
}

// ---- every line, every state ----------------------------------------------------------------

TEST(TheFrame, NoLineIsEverWiderThanTheFont) {
  // The Frame constructor asserts the width and the NUL on every line it builds; this walks the
  // states a board actually passes through so that the assertion sees all of them.
  DisplayState s;
  checkFrame(s);  // a board just switched on: no identity, no clock, no wifi, no jobs

  checkFrame(working());

  const char* types[] = {"status", "telemetry", "neighbours", "config.read", "config.apply",
                         "advert.request", "password.set", "0123456789abcdef"};
  for (const char* type : types) {
    DisplayState j = working();
    j.jobOpen = true;
    j.jobId = 9223372036854775807LL;  // the widest id a long long can carry
    j.jobType = type;
    checkFrame(j);
  }

  for (uint32_t retry = 0; retry <= 100000; retry += 7919) {
    DisplayState b = working();
    b.online = false;
    b.backingOff = true;
    b.retrySeconds = retry;
    checkFrame(b);
  }

  for (int mask = 0; mask < 64; mask++) {
    DisplayState v = working();
    v.hasTime = (mask & 1) != 0;
    v.wifiConnected = (mask & 2) != 0;
    v.online = (mask & 4) != 0;
    v.everHeartbeated = (mask & 8) != 0;
    v.tokenRefused = (mask & 16) != 0;
    v.serverConfigured = (mask & 32) != 0;
    v.publicKeyHex = (mask & 1) != 0 ? kKey : nullptr;
    checkFrame(v);
    for (int configured = 0; configured <= 2; configured++) {
      for (int up = 0; up <= configured; up++) {
        v.observerSlotsConfigured = configured;
        v.observerSlotsConnected = up;
        checkFrame(v);
      }
    }
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
