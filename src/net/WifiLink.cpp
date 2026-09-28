#include "WifiLink.h"

#include <Arduino.h>
#include <WiFi.h>

#include <cstdio>
#include <cstring>

#include "Log.h"

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
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  reconfigure();
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
  backoffMs_ = backoffMs_ == 0 ? kFirstBackoffMs : backoffMs_ * 2;
  if (backoffMs_ > kMaxBackoffMs) backoffMs_ = kMaxBackoffMs;
  nextAttemptAt_ = millis() + backoffMs_;
  state_ = State::Waiting;
  WiFi.disconnect(false, false);
  LOG_W("wifi: not connected, next attempt in %lu s", static_cast<unsigned long>(backoffMs_ / 1000));
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
  switch (state_) {
    case State::Off:
      std::snprintf(out, cap, "off, wifi.ssid is unset");
      return;
    case State::Connecting:
      std::snprintf(out, cap, "connecting to %s, attempt %u", ssid_, static_cast<unsigned>(attempts_));
      return;
    case State::Connected:
      std::snprintf(out, cap, "connected to %s, %s, rssi %d dBm (%u connects, %u drops)", ssid_,
                    WiFi.localIP().toString().c_str(), WiFi.RSSI(), static_cast<unsigned>(connects_),
                    static_cast<unsigned>(drops_));
      return;
    case State::Waiting: {
      const int32_t left = static_cast<int32_t>(nextAttemptAt_ - millis());
      std::snprintf(out, cap, "waiting to retry %s in %ld s (attempt %u failed, %u drops)", ssid_,
                    static_cast<long>(left > 0 ? left / 1000 : 0), static_cast<unsigned>(attempts_),
                    static_cast<unsigned>(drops_));
      return;
    }
  }
}

void WifiLink::oledLine(char* out, size_t cap) const {
  switch (state_) {
    case State::Off: std::snprintf(out, cap, "wifi: no ssid"); return;
    case State::Connecting: std::snprintf(out, cap, "wifi: connecting"); return;
    case State::Connected: std::snprintf(out, cap, "wifi %s", WiFi.localIP().toString().c_str()); return;
    case State::Waiting: std::snprintf(out, cap, "wifi: retry %lus", static_cast<unsigned long>(backoffMs_ / 1000)); return;
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
