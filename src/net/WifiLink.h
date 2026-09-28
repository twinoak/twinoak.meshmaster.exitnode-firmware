// WiFi, station mode, from the settings: join, and rejoin with backoff when the link drops.
//
// The passphrase is read from NVS into a stack buffer for the one call that needs it and wiped
// after; the driver's own habit of persisting credentials in its NVS namespace is turned off
// (WiFi.persistent(false)), so the one copy on flash is the settings store's. The console and
// the OLED's first line show the state; `wifi` scans.

#pragma once

#include <Stream.h>

#include <cstddef>
#include <cstdint>

#include "settings/Store.h"

namespace mm {

class WifiLink {
 public:
  enum class State : uint8_t {
    Off,         // wifi.ssid is unset
    Connecting,  // an attempt is running
    Connected,
    Waiting,     // between attempts, backing off
  };

  void begin(SettingsStore& settings);
  void loop();

  // After `set wifi.*`: drop the link and start over with the new values.
  void reconfigure();

  State state() const { return state_; }
  bool connected() const { return state_ == State::Connected; }

  // True once for every new connection - the edge the clock and the server client act on.
  bool takeConnectedEdge();

  // How many links there have been, and how many dropped: the heartbeat's `wifiReconnects` is
  // one less than the first, which is the number of times this board had to find the net again.
  uint16_t connects() const { return connects_; }
  uint16_t drops() const { return drops_; }

  // One line for `status`, e.g. "connected Twinoak 192.168.1.20 rssi -61 dBm (1 connect, 0 drops)".
  void describe(char* out, size_t cap) const;

  // The OLED's first line, 21 characters at most.
  void oledLine(char* out, size_t cap) const;

  // `wifi`: a blocking scan (a few seconds), one line per network, then the state.
  void scan(Stream& out);

 private:
  void startAttempt();
  void scheduleRetry();

  SettingsStore* settings_ = nullptr;
  State state_ = State::Off;
  char ssid_[33] = {0};
  uint32_t attemptStartedAt_ = 0;
  uint32_t nextAttemptAt_ = 0;
  uint32_t backoffMs_ = 0;
  uint32_t lastPoll_ = 0;
  uint16_t attempts_ = 0;
  uint16_t connects_ = 0;
  uint16_t drops_ = 0;
  bool edge_ = false;
};

}  // namespace mm
