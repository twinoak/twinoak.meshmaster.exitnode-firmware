// The OLED's four lines, as a pure function over a plain struct - written for the window a board
// is mounted in rather than for a bench (Thomas, 4 October 2026, task p1.2-09):
//
//   ID: 46F6     12:03:14      who this is, and what time it is
//   WiFi: yes Online: yes      whether the link and the server are there
//   Heartbeat: 12:03:14        when meshmaster.dk last heard from this board - and, once an
//   HB 12:03:14  MQTT 2/2      observer slot is configured (task p1.7-27), how many of its broker
//                              connections are up, against the right edge
//   Job 1234 acl.set           what it is doing, and why it is not when it is not
//
// Host-portable: no Arduino, no MeshCore, no board. `main.cpp`'s `fillOled` is the one place
// that gathers a DisplayState from the live objects; everything about *what the lines say* is
// here and under test natively (test/test_display_lines), because the toggle and the timeout in
// Oled.h are the only parts of the display that need a board.
//
// The geometry lives here and not in Oled.h so that this header stays host-portable; Oled::kLines
// and Oled::kCols are these two. The 6x8 font gives 21 characters across 128 pixels, so every
// line below is written to fit in kDisplayCols - 1 and `renderLines` truncates whatever does not.

#pragma once

#include <cstddef>
#include <cstdint>

namespace mm {

constexpr int kDisplayLines = 4;
constexpr int kDisplayCols = 22;  // 21 characters of the 6x8 font across 128 pixels, and a NUL
constexpr int kDisplayWidth = kDisplayCols - 1;

// Why the board is doing nothing, when that is the answer - the fourth line's business since
// task p1.2-09 (line three carried it before). Line two's *no* says something is wrong; this
// says what, and `renderLines` picks in this order when more than one is true.
enum class DisplayFault : uint8_t {
  None,
  TokenRefused,  // a 401: nothing is sent until the token is set again (AgentLoop::halted)
  NoWifi,        // the link is down, so the server cannot be reached either
  NoToken,       // the link is up and there is no token to use it with (AgentClient::configured)
  Backoff,       // the link is up and meshmaster.dk is not answering; counting down
};

// Everything the four lines are made of, and nothing else: no pointer into a live object is
// kept past the call.
struct DisplayState {
  // Line one.
  const char* publicKeyHex = nullptr;  // 64 lowercase hex, or nullptr while the radio has none
  bool hasTime = false;                // AgentClock::hasTime()
  uint32_t nowEpoch = 0;               // AgentClock::now()

  // Line two.
  bool wifiConnected = false;  // WifiLink::connected()
  bool online = false;         // the last call to MeshMaster was answered (AgentLoop::online())

  // Line three.
  bool everHeartbeated = false;
  uint32_t lastHeartbeatAtEpoch = 0;
  // The observer's broker slots (Observer::slotsConfigured / slotsConnected): with none
  // configured the line is the heartbeat alone, as before task p1.7-27; with one or two it is
  // "HB <clock>" at the left and "MQTT <up>/<configured>" against the right edge.
  int observerSlotsConfigured = 0;
  int observerSlotsConnected = 0;

  // Line four: the job that is open, else the newest one that ran, else why neither.
  bool jobOpen = false;
  long long jobId = 0;
  const char* jobType = nullptr;
  bool hasLastJob = false;
  uint32_t lastJobAtEpoch = 0;
  bool tokenRefused = false;   // AgentLoop::halted()
  // The server is set: a URL and a token (AgentClient::configured()). False is a board nobody
  // has provisioned yet - exactly what the first merged flash of task p1.2-11 leaves behind -
  // and the line says so rather than calling it idle.
  bool serverConfigured = false;
  // A wait before the next call, whatever its reason: a doubling backoff, or the Retry-After a
  // deploy's 503 named. `AgentLoop::retrySeconds() > 0` and not the failure count, because a
  // believed Retry-After is deliberately not a consecutive failure (task p1.2-10).
  bool backingOff = false;
  uint32_t retrySeconds = 0;   // AgentLoop::retrySeconds(), the countdown to the next attempt
};

// Which of the four faults line four names, or None. The order is task 06's, with task p1.3-12's
// fourth in the middle of it: a refused token first (nothing is sent at all), then the missing
// link, then a link with no token to use it with, then the server that is not answering. *No
// token* is below *no wifi* because a board with no WiFi cannot use a token yet, and above the
// backoff because an unprovisioned board never gets as far as one.
DisplayFault faultOf(const DisplayState& state);

// The frame. Every line is NUL-terminated and at most kDisplayWidth characters long.
void renderLines(const DisplayState& state, char lines[kDisplayLines][kDisplayCols]);

}  // namespace mm
