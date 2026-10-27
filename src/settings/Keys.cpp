#include "Keys.h"

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "observer/Presets.h"

namespace mm {

// The transmit power a board starts at, which is the one setting whose fallback is the board's
// and not the channel's. A Heltec V3 drives its antenna from the SX1262 alone, so its 22 dBm is
// 22 dBm at the socket; a Heltec V4 has an external PA that is on for every transmission
// (MeshCore's variants/heltec_v4/LoRaFEMControl.cpp, setTxModeEnable), and upstream's own note
// beside `LORA_TX_POWER=10` in variants/heltec_v4/platformio.ini reads "if it is configured as
// 10 here, the final output will be 22 dbm". So the V4 environment defines this as 10 and a
// board out of the box is legal on both. The *range* is the same on both - MeshCore's own
// -9..MAX_LORA_TX_POWER, and MAX_LORA_TX_POWER is 22 for each - so only the fallback moves.
#ifndef MESHMASTER_TX_DBM_DEFAULT
#define MESHMASTER_TX_DBM_DEFAULT "22"
#endif

// The Danish MeshCore channel is the fallback for the radio: 869.618 MHz, 62.5 kHz, SF 8, CR 8,
// which is what the field repeaters run and what the heartbeat fixture carries
// (tests/fixtures/contracts/AgentHeartbeat.json). The bench sets them like it sets a repeater's.
// The ranges are MeshCore's own (examples/companion_radio/MyMesh.cpp, begin(): 150..2500 MHz,
// 7.8..500 kHz, SF 5..12, CR 5..8, -9..MAX_LORA_TX_POWER dBm, which is 22 on the Heltec V3).
const KeyDef kKeys[kKeyCount] = {
    {"wifi.ssid", "wifi_ssid", ValueKind::Text, nullptr, 0, 0, 1, 32, "the network to join"},
    {"wifi.pass", "wifi_pass", ValueKind::Secret, nullptr, 0, 0, 8, 63, "its WPA passphrase, 8..63 characters; never printed"},
    {"meshmaster.url", "mm_url", ValueKind::Url, "https://meshmaster.dk", 0, 0, 9, kMaxValueLen, "the server; https:// required; unset means https://meshmaster.dk"},
    {"meshmaster.token", "mm_token", ValueKind::Token, nullptr, 0, 0, 12, kMaxValueLen, "the agent token from /exit-nodes/new; never printed"},
    {"radio.freq", "radio_freq", ValueKind::Float, "869.618", 150.0f, 2500.0f, 0, 0, "MHz"},
    {"radio.bw", "radio_bw", ValueKind::Float, "62.5", 7.8f, 500.0f, 0, 0, "kHz"},
    {"radio.sf", "radio_sf", ValueKind::Int, "8", 5, 12, 0, 0, "spreading factor, 5..12"},
    {"radio.cr", "radio_cr", ValueKind::Int, "8", 5, 8, 0, 0, "coding rate, 5..8"},
    {"radio.tx", "radio_tx", ValueKind::Int, MESHMASTER_TX_DBM_DEFAULT, -9, 22, 0, 0, "transmit power in dBm, -9..22"},
    {"log.level", "log_level", ValueKind::Level, "info", 0, 0, 0, 0, "error | warn | info | debug"},
    {"log.frames", "log_frames", ValueKind::OnOff, "off", 0, 0, 0, 0, "on | off: one line per received frame, header only, never the payload"},
    {"display.timeout", "disp_timeout", ValueKind::Int, "0", 0, 86400, 0, 0, "seconds the OLED stays lit after a button press, 0..86400; 0 never turns it off, 300 is what this firmware did before"},
    // The observer (task p1.7-26, src/observer/): the names are the observer firmware's, the sizes
    // its MQTTPrefs buffers' (MQTTPrefsStorage.h:68-95 at d664d89d: origin 32, iata 8, owner 65,
    // email 64, host 64, username 32, password 64, topic 96, audience 64), less one for the NUL.
    // mqtt.origin falls back to "MeshMaster-" and the key's first four characters, which the
    // table cannot say because it depends on the identity (src/observer/Observer.cpp).
    {"mqtt.origin", "mqtt_origin", ValueKind::Text, nullptr, 0, 0, 1, 31, "the observer's name in what it publishes; unset is MeshMaster-<first four of the key>"},
    {"mqtt.iata", "mqtt_iata", ValueKind::Iata, nullptr, 0, 0, 3, 3, "three letters, the region the brokers file this node under: AAL, AAR, BLL, ODE, CPH or RNN in Denmark"},
    {"mqtt.owner", "mqtt_owner", ValueKind::Hex64, nullptr, 0, 0, 64, 64, "the owner's public key, 64 hex digits, named in the sign-in token"},
    {"mqtt.email", "mqtt_email", ValueKind::Email, nullptr, 0, 0, 3, 63, "the owner's email, named in the sign-in token"},
    {"mqtt1.preset", "mqtt1_preset", ValueKind::Preset, "none", 0, 0, 4, 10, "none | meshview | meshmapper | custom (get mqtt.presets)"},
    {"mqtt1.server", "mqtt1_server", ValueKind::BrokerUrl, nullptr, 0, 0, 9, 63, "custom: wss://host:port/path or mqtts://host:port"},
    {"mqtt1.audience", "mqtt1_audience", ValueKind::Host, nullptr, 0, 0, 1, 63, "custom: the broker's name in the token; set means token sign-in"},
    {"mqtt1.topic", "mqtt1_topic", ValueKind::Topic, nullptr, 0, 0, 1, 95, "custom: a template with {iata} {device} {type}; unset is meshcore/{iata}/{device}/{type}"},
    {"mqtt1.username", "mqtt1_user", ValueKind::Text, nullptr, 0, 0, 1, 31, "custom: for a broker that wants a username instead of a token"},
    {"mqtt1.password", "mqtt1_pass", ValueKind::Secret, nullptr, 0, 0, 1, 63, "custom: its password; never printed"},
    {"mqtt2.preset", "mqtt2_preset", ValueKind::Preset, "none", 0, 0, 4, 10, "the second slot, as mqtt1.preset"},
    {"mqtt2.server", "mqtt2_server", ValueKind::BrokerUrl, nullptr, 0, 0, 9, 63, "as mqtt1.server"},
    {"mqtt2.audience", "mqtt2_audience", ValueKind::Host, nullptr, 0, 0, 1, 63, "as mqtt1.audience"},
    {"mqtt2.topic", "mqtt2_topic", ValueKind::Topic, nullptr, 0, 0, 1, 95, "as mqtt1.topic"},
    {"mqtt2.username", "mqtt2_user", ValueKind::Text, nullptr, 0, 0, 1, 31, "as mqtt1.username"},
    {"mqtt2.password", "mqtt2_pass", ValueKind::Secret, nullptr, 0, 0, 1, 63, "as mqtt1.password; never printed"},
};

const KeyDef* findKey(const char* name) {
  if (name == nullptr) return nullptr;
  // The observer firmware's name for the passphrase (CommonCLI_Observer.cpp:444-448 at d664d89d):
  // the same key, so it is redacted on echo and in `get` exactly as wifi.pass is.
  if (std::strcmp(name, "wifi.pwd") == 0) name = "wifi.pass";
  for (const KeyDef& k : kKeys) {
    if (std::strcmp(k.name, name) == 0) return &k;
  }
  return nullptr;
}

const char* explainKey(const char* name) {
  if (name == nullptr) return nullptr;
  // The Danish observer guide's two lines this firmware has nothing to store for: its clock is
  // UTC from the server or SNTP and what it publishes is stamped in UTC, as the observer
  // firmware's is whatever its timezone says (MQTT_IMPLEMENTATION.md, "Timezone": "Published
  // timestamps are always UTC"); and an exit node publishes what it receives and forwards
  // nothing, so there is no source to choose (its bridge.source is rx|tx for other bridges).
  if (std::strcmp(name, "timezone") == 0) {
    return "timezone: nothing to set; this firmware keeps UTC from the server or SNTP, and what it publishes is stamped in UTC";
  }
  if (std::strcmp(name, "bridge.source") == 0) {
    return "bridge.source: nothing to set; an exit node publishes what it receives (rx) and forwards nothing";
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

bool isHostChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-'; }

bool isHexChar(char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }

bool isLetter(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }

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

    case ValueKind::Iata:
      // The observer firmware takes three letters or digits (MQTTObserverValidation.h:15-24 at
      // d664d89d); the task asks for letters, which every code in its help is. Any case typed,
      // uppercase stored (normalizeValue).
      if (len == 3 && isLetter(value[0]) && isLetter(value[1]) && isLetter(value[2])) return true;
      std::snprintf(msg, sizeof msg, "%s: three letters, e.g. AAR", key.name);
      say(err, errCap, msg);
      return false;

    case ValueKind::Hex64: {
      // MQTTObserverValidation.h:27-36: exactly 64 hex characters, any case.
      bool hex = len == 64;
      for (const char* p = value; hex && *p; p++) hex = isHexChar(*p);
      if (hex) return true;
      std::snprintf(msg, sizeof msg, "%s: a public key is 64 hex digits", key.name);
      say(err, errCap, msg);
      return false;
    }

    case ValueKind::Email: {
      const char* at = std::strchr(value, '@');
      if (len >= key.minLen && len <= key.maxLen && allPrintableAscii(value) && std::strchr(value, ' ') == nullptr &&
          at != nullptr && at != value && at[1] != '\0' && std::strchr(at + 1, '@') == nullptr) {
        return true;
      }
      std::snprintf(msg, sizeof msg, "%s: name@host, at most %u characters", key.name, key.maxLen);
      say(err, errCap, msg);
      return false;
    }

    case ValueKind::Host: {
      // The observer firmware's own shape for a host name (MQTTObserverValidation.h:40-53).
      bool ok = len >= key.minLen && len <= key.maxLen && value[0] != '.' && value[len - 1] != '.';
      for (const char* p = value; ok && *p; p++) ok = isHostChar(*p);
      if (ok) return true;
      std::snprintf(msg, sizeof msg, "%s: a host name, letters, digits, . and -, at most %u characters", key.name, key.maxLen);
      say(err, errCap, msg);
      return false;
    }

    case ValueKind::BrokerUrl: {
      // The two TLS schemes the observer firmware speaks (MQTT_IMPLEMENTATION.md, "Broker
      // Presets": wss:// is WebSocket over TLS, mqtts:// is MQTT over TLS). Its ws:// and mqtt://
      // are not taken: TLS is verified on every connection this firmware makes (README.md).
      static const char kWss[] = "wss://";
      static const char kMqtts[] = "mqtts://";
      size_t schemeLen = 0;
      if (std::strncmp(value, kWss, sizeof kWss - 1) == 0) schemeLen = sizeof kWss - 1;
      if (std::strncmp(value, kMqtts, sizeof kMqtts - 1) == 0) schemeLen = sizeof kMqtts - 1;
      if (schemeLen == 0) {
        std::snprintf(msg, sizeof msg, "%s: the scheme must be wss:// or mqtts:// (TLS is verified; ws:// and mqtt:// are not taken)",
                      key.name);
        say(err, errCap, msg);
        return false;
      }
      if (len > key.maxLen || value[schemeLen] == '\0' || value[schemeLen] == '/' || value[schemeLen] == ':') {
        std::snprintf(msg, sizeof msg, "%s: wss://host[:port][/path] or mqtts://host[:port], at most %u characters", key.name,
                      key.maxLen);
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

    case ValueKind::Topic: {
      // An MQTT topic a publisher may use: no wildcard (+, #), no space, printable ASCII, and the
      // observer firmware's 95 characters. The placeholders are src/observer/Topics.h's.
      bool ok = len >= key.minLen && len <= key.maxLen && allPrintableAscii(value);
      for (const char* p = value; ok && *p; p++) ok = *p != ' ' && *p != '+' && *p != '#';
      if (ok) return true;
      std::snprintf(msg, sizeof msg, "%s: a topic template of printable characters, no space, + or #, at most %u characters",
                    key.name, key.maxLen);
      say(err, errCap, msg);
      return false;
    }

    case ValueKind::Preset:
      if (observer::isPresetName(value)) return true;
      std::snprintf(msg, sizeof msg, "%s: none | meshview | meshmapper | custom (get mqtt.presets)", key.name);
      say(err, errCap, msg);
      return false;
  }
  say(err, errCap, "unknown value kind");
  return false;
}

void normalizeValue(const KeyDef& key, char* value) {
  if (value == nullptr || key.kind != ValueKind::Iata) return;
  for (char* p = value; *p; p++) *p = static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
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

double numericValueDouble(const char* value) {
  if (value == nullptr || *value == 0) return 0;
  char* end = nullptr;
  errno = 0;
  const double v = std::strtod(value, &end);
  if (errno != 0 || end == value || *end != 0 || std::isnan(v) || std::isinf(v)) return 0;
  return v;
}

}  // namespace mm
