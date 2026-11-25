#include "WifiLink.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_system.h>

#include <cstdio>
#include <cstring>

#include <agent/Backoff.h>
#include <agent/Log.h>

#include "Hostname.h"

namespace mm {

namespace {

constexpr uint32_t kAttemptTimeoutMs = 20000;   // one attempt gets this long to reach GOT_IP
constexpr uint32_t kFirstBackoffMs = 5000;      // then 5 s, 10 s, 20 s ... up to
constexpr uint32_t kMaxBackoffMs = 5 * 60000;   // five minutes between attempts
constexpr uint32_t kPollMs = 250;

void wipe(char* buf, size_t len) {
  volatile char* p = buf;
  for (size_t i = 0; i < len; i++) p[i] = 0;
}

const char* authName(wifi_auth_mode_t mode) {
  switch (mode) {
    case WIFI_AUTH_OPEN: return "open";
    case WIFI_AUTH_WEP: return "wep";
    case WIFI_AUTH_WPA_PSK: return "wpa";
    case WIFI_AUTH_WPA2_PSK: return "wpa2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "wpa/wpa2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "wpa2-enterprise";
    case WIFI_AUTH_WPA3_PSK: return "wpa3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "wpa2/wpa3";
    default: return "other";
  }
}

}  // namespace

void WifiLink::begin(SettingsStore& settings) {
  settings_ = &settings;
  WiFi.persistent(false);       // the settings store is the one copy of the credentials on flash
  WiFi.setAutoReconnect(false);  // the backoff below is the reconnect policy, and it is visible
  nameThisBoard();               // before WiFi.mode(WIFI_STA), which is the only call that takes it
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  LOG_I("wifi: mac %s, name %s", macText_, hostname_);
  reconfigure();
}

// The name the DHCP request carries (task p1.22-74), and why it is set here and not anywhere else.
//
// At the Arduino core this repository pins (2.0.17,
// packages/framework-arduinoespressif32/libraries/WiFi/src/WiFiGeneric.cpp) `WiFi.setHostname()`
// writes nothing to the network stack: it copies the name into a static buffer
// (`set_esp_netif_hostname`, L292). The **one** call that pushes that buffer onto the station's
// netif - and so onto the DHCP request lwIP makes from it - is `WiFiGenericClass::mode()`, which
// does `set_esp_interface_hostname(ESP_IF_WIFI_STA, get_esp_netif_hostname())` at L1265 right
// after it has created the netifs and started the driver. `mode()` returns at its first line when
// the mode it is asked for is the mode already in force (L1249), so the push happens **once**, at
// the first `WiFi.mode(WIFI_STA)`: a name set after that one call is a name the network never
// hears. Hence the order above, and hence the rail in test/test_hostname that reads this file and
// fails if the two calls ever swap.
//
// The six bytes come from `esp_read_mac(ESP_MAC_WIFI_STA)`, which reads the eFuse and needs no
// WiFi driver - `WiFi.macAddress()` goes through `esp_wifi_get_mac()`, which has nothing to answer
// with until `mode()` has run, which is too late. The two give the same address: the station MAC
// is the base MAC (esp_mac.h at framework-espidf 4.4.7), which is what the core's own default name
// is built from too.
//
// `CONFIG_LWIP_LOCAL_HOSTNAME` ("espressif", sdkconfig.defaults L1464) is lwIP's fallback for a
// netif nobody named; it is not what this board announced, because the core's `mode()` names the
// station every time - as `esp32s3-XXYYZZ`, the last three bytes of this same MAC, until this
// task. Reading it is how that was established; it is left alone.
void WifiLink::nameThisBoard() {
  uint8_t mac[6] = {0};
  const esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
  if (err != ESP_OK) {
    // Nothing to build a name from. The core's own default name stands, and `status` says so
    // rather than printing six zeroes as though they were an address.
    LOG_W("wifi: the station address could not be read (%d), so this board keeps the framework's name", err);
    std::snprintf(macText_, sizeof macText_, "unknown");
    std::snprintf(hostname_, sizeof hostname_, "unknown");
    return;
  }
  macToText(mac, macText_, sizeof macText_);
  hostnameFromMac(mac, hostname_, sizeof hostname_);
  WiFi.setHostname(hostname_);
}

void WifiLink::reconfigure() {
  ssid_[0] = '\0';
  settings_->get(*findKey("wifi.ssid"), ssid_, sizeof ssid_);
  WiFi.disconnect(false, false);
  backoffMs_ = 0;
  attempts_ = 0;
  if (ssid_[0] == '\0') {
    state_ = State::Off;
    LOG_I("wifi: off, wifi.ssid is unset");
    return;
  }
  startAttempt();
}

void WifiLink::startAttempt() {
  char pass[64] = {0};
  const size_t n = settings_->get(*findKey("wifi.pass"), pass, sizeof pass);
  attempts_++;
  attemptStartedAt_ = millis();
  state_ = State::Connecting;
  LOG_I("wifi: connecting to %s (attempt %u)", ssid_, static_cast<unsigned>(attempts_));
  if (n == 0) {
    LOG_W("wifi: wifi.pass is unset, trying the network as open");
    WiFi.begin(ssid_);
  } else {
    WiFi.begin(ssid_, pass);
  }
  wipe(pass, sizeof pass);
}

void WifiLink::scheduleRetry() {
  // The sequence is the one the server loop backs off with (agent/Backoff.h) at this link's own
  // numbers, and the wait carries jitter: after a power cut every board in the house wakes at
  // the same second, and a router that is still booting refuses them all together.
  backoffMs_ = nextBackoffMs(backoffMs_, kFirstBackoffMs, kMaxBackoffMs);
  waitMs_ = withJitter(backoffMs_, esp_random());
  nextAttemptAt_ = millis() + waitMs_;
  state_ = State::Waiting;
  WiFi.disconnect(false, false);
  LOG_W("wifi: not connected, next attempt in %lu s (the %lu s step, jittered)",
        static_cast<unsigned long>(waitMs_ / 1000), static_cast<unsigned long>(backoffMs_ / 1000));
}

void WifiLink::loop() {
  const uint32_t now = millis();
  if (now - lastPoll_ < kPollMs) return;
  lastPoll_ = now;

  switch (state_) {
    case State::Off:
      return;

    case State::Connecting:
      if (WiFi.status() == WL_CONNECTED) {
        state_ = State::Connected;
        connects_++;
        backoffMs_ = 0;
        edge_ = true;
        LOG_I("wifi: connected, %s, rssi %d dBm", WiFi.localIP().toString().c_str(), WiFi.RSSI());
      } else if (now - attemptStartedAt_ > kAttemptTimeoutMs) {
        scheduleRetry();
      }
      return;

    case State::Connected:
      if (WiFi.status() != WL_CONNECTED) {
        drops_++;
        LOG_W("wifi: link dropped");
        scheduleRetry();
      }
      return;

    case State::Waiting:
      if (static_cast<int32_t>(now - nextAttemptAt_) >= 0) startAttempt();
      return;
  }
}

bool WifiLink::takeConnectedEdge() {
  const bool e = edge_;
  edge_ = false;
  return e;
}

void WifiLink::describe(char* out, size_t cap) const {
  if (out == nullptr || cap == 0) return;
  out[0] = '\0';
  int n = 0;
  switch (state_) {
    case State::Off:
      n = std::snprintf(out, cap, "off, wifi.ssid is unset");
      break;
    case State::Connecting:
      n = std::snprintf(out, cap, "connecting to %s, attempt %u", ssid_, static_cast<unsigned>(attempts_));
      break;
    case State::Connected:
      n = std::snprintf(out, cap, "connected to %s, %s, rssi %d dBm (%u connects, %u drops)", ssid_,
                        WiFi.localIP().toString().c_str(), WiFi.RSSI(), static_cast<unsigned>(connects_),
                        static_cast<unsigned>(drops_));
      break;
    case State::Waiting: {
      const int32_t left = static_cast<int32_t>(nextAttemptAt_ - millis());
      n = std::snprintf(out, cap, "waiting to retry %s in %ld s (attempt %u failed, %u drops)", ssid_,
                        static_cast<long>(left > 0 ? left / 1000 : 0), static_cast<unsigned>(attempts_),
                        static_cast<unsigned>(drops_));
      break;
    }
  }
  // The address and the name on every state's line (task p1.22-74). The link being down is exactly
  // when a person reads this line and goes to reserve an address in their router, and the two are
  // known from `begin()` whether there is a link or not.
  if (n > 0 && static_cast<size_t>(n) < cap) {
    std::snprintf(out + n, cap - static_cast<size_t>(n), ", mac %s, name %s", macText_, hostname_);
  }
}


void WifiLink::scan(Stream& out) {
  out.println("scanning...");
  const int n = WiFi.scanNetworks(false, false);
  if (n < 0) {
    out.println("scan failed");
  } else {
    for (int i = 0; i < n; i++) {
      out.printf("  %-32s  ch %2d  %4d dBm  %s\r\n", WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i),
                 authName(WiFi.encryptionType(i)));
    }
    out.printf("%d network%s\r\n", n, n == 1 ? "" : "s");
  }
  WiFi.scanDelete();
  char line[160];
  describe(line, sizeof line);
  out.printf("wifi: %s\r\n", line);
}

}  // namespace mm
