#include "Keys.h"

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace mm {

// The Danish MeshCore channel is the fallback for the radio: 869.618 MHz, 62.5 kHz, SF 8, CR 8,
// which is what the field repeaters run and what the heartbeat fixture carries
// (tests/fixtures/contracts/AgentHeartbeat.json). The bench sets them like it sets a repeater's.
// The ranges are MeshCore's own (examples/companion_radio/MyMesh.cpp, begin(): 150..2500 MHz,
// 7.8..500 kHz, SF 5..12, CR 5..8, -9..MAX_LORA_TX_POWER dBm, which is 22 on the Heltec V3).
const KeyDef kKeys[kKeyCount] = {
    {"wifi.ssid", "wifi_ssid", ValueKind::Text, nullptr, 0, 0, 1, 32, "the network to join"},
    {"wifi.pass", "wifi_pass", ValueKind::Secret, nullptr, 0, 0, 8, 63, "its WPA passphrase, 8..63 characters; never printed"},
    {"meshmaster.url", "mm_url", ValueKind::Url, nullptr, 0, 0, 9, kMaxValueLen, "the server, https:// required (https://meshmaster.dk)"},
    {"meshmaster.token", "mm_token", ValueKind::Token, nullptr, 0, 0, 12, kMaxValueLen, "the agent token from /exit-nodes/new; never printed"},
    {"radio.freq", "radio_freq", ValueKind::Float, "869.618", 150.0f, 2500.0f, 0, 0, "MHz"},
    {"radio.bw", "radio_bw", ValueKind::Float, "62.5", 7.8f, 500.0f, 0, 0, "kHz"},
    {"radio.sf", "radio_sf", ValueKind::Int, "8", 5, 12, 0, 0, "spreading factor, 5..12"},
    {"radio.cr", "radio_cr", ValueKind::Int, "8", 5, 8, 0, 0, "coding rate, 5..8"},
    {"radio.tx", "radio_tx", ValueKind::Int, "22", -9, 22, 0, 0, "transmit power in dBm, -9..22"},
    {"log.level", "log_level", ValueKind::Level, "info", 0, 0, 0, 0, "error | warn | info | debug"},
    {"log.frames", "log_frames", ValueKind::OnOff, "off", 0, 0, 0, 0, "on | off: one line per received frame, header only, never the payload"},
};

const KeyDef* findKey(const char* name) {
  if (name == nullptr) return nullptr;
  for (const KeyDef& k : kKeys) {
    if (std::strcmp(k.name, name) == 0) return &k;
  }
  return nullptr;
}

bool isSecret(const KeyDef& key) { return key.kind == ValueKind::Secret || key.kind == ValueKind::Token; }

namespace {

void say(char* err, size_t cap, const char* text) {
  if (err == nullptr || cap == 0) return;
  std::snprintf(err, cap, "%s", text);
}

bool allPrintableAscii(const char* s) {
  for (; *s; s++) {
    const unsigned char c = static_cast<unsigned char>(*s);
    if (c < 0x20 || c > 0x7e) return false;
  }
  return true;
}

bool parseFloat(const char* s, float& out) {
  if (*s == '\0') return false;
  char* end = nullptr;
  errno = 0;
  const double v = std::strtod(s, &end);
  if (errno != 0 || end == s || *end != '\0' || std::isnan(v) || std::isinf(v)) return false;
  out = static_cast<float>(v);
  return true;
}

bool parseInt(const char* s, long& out) {
  if (*s == '\0') return false;
  char* end = nullptr;
  errno = 0;
  const long v = std::strtol(s, &end, 10);
  if (errno != 0 || end == s || *end != '\0') return false;
  out = v;
  return true;
}

bool isUrlChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == ':' || c == '/' || c == '_';
}

bool isBase64UrlChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_'; }

}  // namespace

bool validateValue(const KeyDef& key, const char* value, char* err, size_t errCap) {
  if (value == nullptr) value = "";
  const size_t len = std::strlen(value);
  char msg[160];

  if (len > kMaxValueLen) {
    std::snprintf(msg, sizeof msg, "%s: longer than %u characters", key.name, static_cast<unsigned>(kMaxValueLen));
    say(err, errCap, msg);
    return false;
  }

  switch (key.kind) {
    case ValueKind::Text:
    case ValueKind::Secret:
      if (len < key.minLen || len > key.maxLen) {
        std::snprintf(msg, sizeof msg, "%s: %u..%u characters", key.name, key.minLen, key.maxLen);
        say(err, errCap, msg);
        return false;
      }
      if (!allPrintableAscii(value)) {
        std::snprintf(msg, sizeof msg, "%s: printable ASCII only", key.name);
        say(err, errCap, msg);
        return false;
      }
      return true;

    case ValueKind::Url: {
      static const char kScheme[] = "https://";
      const size_t schemeLen = sizeof kScheme - 1;
      if (std::strncmp(value, kScheme, schemeLen) != 0) {
        std::snprintf(msg, sizeof msg, "%s: the scheme must be https://", key.name);
        say(err, errCap, msg);
        return false;
      }
      if (len < key.minLen || len > key.maxLen || value[schemeLen] == '\0' || value[schemeLen] == '/') {
        std::snprintf(msg, sizeof msg, "%s: https://host[:port][/path], at most %u characters", key.name, key.maxLen);
        say(err, errCap, msg);
        return false;
      }
      for (const char* p = value; *p; p++) {
        if (!isUrlChar(*p)) {
          std::snprintf(msg, sizeof msg, "%s: '%c' is not allowed in a URL here", key.name, *p);
          say(err, errCap, msg);
          return false;
        }
      }
      return true;
    }

    case ValueKind::Token: {
      static const char kPrefix[] = "mma_";
      const size_t prefixLen = sizeof kPrefix - 1;
      if (std::strncmp(value, kPrefix, prefixLen) != 0) {
        std::snprintf(msg, sizeof msg, "%s: a meshmaster.dk agent token starts with mma_", key.name);
        say(err, errCap, msg);
        return false;
      }
      if (len < key.minLen || len > key.maxLen) {
        std::snprintf(msg, sizeof msg, "%s: %u..%u characters", key.name, key.minLen, key.maxLen);
        say(err, errCap, msg);
        return false;
      }
      for (const char* p = value + prefixLen; *p; p++) {
        if (!isBase64UrlChar(*p)) {
          std::snprintf(msg, sizeof msg, "%s: letters, digits, - and _ after mma_", key.name);
          say(err, errCap, msg);
          return false;
        }
      }
      return true;
    }

    case ValueKind::Float: {
      float v = 0;
      if (!parseFloat(value, v)) {
        std::snprintf(msg, sizeof msg, "%s: not a number", key.name);
        say(err, errCap, msg);
        return false;
      }
      if (v < key.min || v > key.max) {
        std::snprintf(msg, sizeof msg, "%s: out of range, %g..%g", key.name, static_cast<double>(key.min),
                      static_cast<double>(key.max));
        say(err, errCap, msg);
        return false;
      }
      return true;
    }

    case ValueKind::Int: {
      long v = 0;
      if (!parseInt(value, v)) {
        std::snprintf(msg, sizeof msg, "%s: not a whole number", key.name);
        say(err, errCap, msg);
        return false;
      }
      if (v < static_cast<long>(key.min) || v > static_cast<long>(key.max)) {
        std::snprintf(msg, sizeof msg, "%s: out of range, %ld..%ld", key.name, static_cast<long>(key.min),
                      static_cast<long>(key.max));
        say(err, errCap, msg);
        return false;
      }
      return true;
    }

    case ValueKind::Level:
      if (std::strcmp(value, "error") == 0 || std::strcmp(value, "warn") == 0 || std::strcmp(value, "info") == 0 ||
          std::strcmp(value, "debug") == 0) {
        return true;
      }
      std::snprintf(msg, sizeof msg, "%s: error | warn | info | debug", key.name);
      say(err, errCap, msg);
      return false;

    case ValueKind::OnOff:
      if (std::strcmp(value, "on") == 0 || std::strcmp(value, "off") == 0) return true;
      std::snprintf(msg, sizeof msg, "%s: on | off", key.name);
      say(err, errCap, msg);
      return false;
  }
  say(err, errCap, "unknown value kind");
  return false;
}

void describeValue(const KeyDef& key, const char* stored, char* out, size_t cap) {
  const bool unset = (stored == nullptr || stored[0] == '\0');
  if (isSecret(key)) {
    std::snprintf(out, cap, "%s %s", key.name, unset ? "unset" : "set");
    return;
  }
  if (unset) {
    if (key.fallback != nullptr) {
      std::snprintf(out, cap, "%s unset (%s)", key.name, key.fallback);
    } else {
      std::snprintf(out, cap, "%s unset", key.name);
    }
    return;
  }
  std::snprintf(out, cap, "%s %s", key.name, stored);
}

void replyForSet(const KeyDef& key, char* out, size_t cap) { std::snprintf(out, cap, "ok %s", key.name); }

float numericValue(const char* value) {
  float v = 0;
  if (value != nullptr && parseFloat(value, v)) return v;
  return 0;
}

}  // namespace mm
