// WiFi, station mode, from the settings: join, and rejoin with backoff when the link drops.
//
// The passphrase is read from NVS into a stack buffer for the one call that needs it and wiped
// after; the driver's own habit of persisting credentials in its NVS namespace is turned off
// (WiFi.persistent(false)), so the one copy on flash is the settings store's. `status` shows the
// state in full and the OLED's second line shows `connected()` as one word; `wifi` scans.
//
// The board announces itself to the network as `meshmaster-` and the last four hex digits of its
// station MAC (src/net/Hostname.h, task p1.22-74), and `status` prints the name beside the address
// - so two boards on one network are two names in a router's lease list, and a reservation is made
// by the address shown there. Neither is settable: the name follows the chip.

#pragma once

#include <Stream.h>

#include <cstddef>
#include <cstdint>

#include "net/Hostname.h"
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

  // This board's station address, and the name it announces to the network: `aa:bb:cc:dd:ee:ff`
  // and `meshmaster-eeff` (src/net/Hostname.h, task p1.22-74). Both are the chip's - nothing
  // stores them and nothing sets them - and both are read once in `begin()`, so they are the same
  // words whether the link is up or not. `unknown` is the eFuse read having failed, which is the
  // one case where the framework's own name stands instead.
  const char* macText() const { return macText_; }
  const char* hostname() const { return hostname_; }

  // One line for `status`, e.g. "connected to Twinoak, 192.168.1.20, rssi -61 dBm (1 connects,
  // 0 drops), mac 34:85:18:1a:2b:3c, name meshmaster-2b3c".
  void describe(char* out, size_t cap) const;

  // `wifi`: a blocking scan (a few seconds), one line per network, then the state.
  void scan(Stream& out);

 private:
  void startAttempt();
  void scheduleRetry();
  // Reads the station MAC out of the eFuse and hands the framework the name built from it. Called
  // from `begin()` **before** `WiFi.mode(WIFI_STA)`, which is the one call that gives the name to
  // the station's netif and so to the DHCP request; WifiLink.cpp says why, at length.
  void nameThisBoard();

  SettingsStore* settings_ = nullptr;
  State state_ = State::Off;
  char ssid_[33] = {0};
  char macText_[kMacTextCap] = {0};
  char hostname_[kHostnameCap] = {0};
  uint32_t attemptStartedAt_ = 0;
  uint32_t nextAttemptAt_ = 0;
  uint32_t backoffMs_ = 0;   // the sequence's step
  uint32_t waitMs_ = 0;      // what is actually waited: that step, jittered (agent/Backoff.h)
  uint32_t lastPoll_ = 0;
  uint16_t attempts_ = 0;
  uint16_t connects_ = 0;
  uint16_t drops_ = 0;
  bool edge_ = false;
};

}  // namespace mm
