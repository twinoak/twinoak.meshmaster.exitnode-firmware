// `mm::WifiLink` for the host build: the link is always up, because there is no link.
//
// The agent loop asks the WiFi three questions - is it connected, how many links have there been,
// how many drops - and acts on the answers: it holds off while the link is down, counts
// `wifiReconnects` for the heartbeat, and reboots only on failures *with the link up*
// (src/Health.h). On a workstation the network is the operating system's and there is nothing to
// join, so this answers "connected, one link, no drops" and the loop runs its real course.
//
// `src/net/WifiLink.cpp` is the board's implementation of the same class and is not compiled here
// (platformio.ini's `[env:host]` build_src_filter). Nothing of the passphrase handling is in this
// file because there is no passphrase: the one copy of a secret in the harness is the agent token
// the host program was given, and it lives where every other setting does (src/host/arduino/
// Preferences.h).

#include <cstdio>

#include "net/WifiLink.h"

namespace mm {

void WifiLink::begin(SettingsStore& settings) {
  settings_ = &settings;
  state_ = State::Connected;
  connects_ = 1;
  edge_ = true;  // the one edge: the clock and the config call act on it, as on the board
  std::snprintf(ssid_, sizeof ssid_, "%s", "the host's own network");
}

void WifiLink::loop() {}

void WifiLink::reconfigure() {}

bool WifiLink::takeConnectedEdge() {
  const bool was = edge_;
  edge_ = false;
  return was;
}

void WifiLink::describe(char* out, size_t cap) const {
  std::snprintf(out, cap, "the host's own network (no radio: this is the host harness, docs/host.md)");
}

void WifiLink::oledLine(char* out, size_t cap) const { std::snprintf(out, cap, "host"); }

void WifiLink::scan(Stream& out) {
  out.println("wifi      there is no radio in the host build: the network is the workstation's");
}

void WifiLink::startAttempt() {}

void WifiLink::scheduleRetry() {}

}  // namespace mm
