// The settings a board takes over its serial console, as one table.
//
// This file is host-portable: no Arduino, no NVS. It is what the console parser validates
// against, what the store reads and writes by, and what `get` prints from - so that the
// native tests (test/test_console_parser) cover the same rules the board applies. The NVS
// side is src/settings/Store.h, which is board-only.
//
// Four keys are secrets: wifi.pass, meshmaster.token, mqtt1.password and mqtt2.password. They
// are typed once, kept in NVS, and printed by no command - `get` says "set" or "unset" for them
// and never the value (README.md, "Rails that hold in every task").
//
// Since task p1.7-26 the table also holds the observer's keys, under the names the observer
// firmware (agessaman/MeshCore, observer-firmware) gives them where it has one: mqtt.iata,
// mqtt.owner, mqtt.email, mqtt.origin and two slots of mqttN.preset, .server, .audience, .topic,
// .username, .password. `wifi.pwd` is that firmware's name for wifi.pass and is accepted as
// another name of it (findKey). What the keys mean is src/observer/Config.h.

#pragma once

#include <cstddef>
#include <cstdint>

namespace mm {

enum class ValueKind : uint8_t {
  Text,    // free text, bounded
  Secret,  // free text, bounded, never printed
  Url,     // https://host[:port][/path], bounded
  Token,   // a meshmaster.dk agent token: "mma_" and base64url, never printed
  Float,   // a decimal number within [min, max]
  Int,     // an integer within [min, max]
  Level,   // error | warn | info | debug
  OnOff,   // on | off
  Iata,    // three letters A-Z, any case typed, stored uppercase (the observer firmware's mqtt.iata)
  Hex64,   // 64 hex digits, any case: a public key (mqtt.owner)
  Email,   // one @ with something on both sides, no spaces, bounded (mqtt.email)
  Host,    // a host name: letters, digits, . and -, bounded (mqttN.audience)
  BrokerUrl,  // wss://host[:port][/path] or mqtts://host[:port] - TLS only, bounded (mqttN.server)
  Topic,   // an MQTT topic template: printable ASCII, no space, no + or # wildcard, bounded (mqttN.topic)
  Preset,  // none | meshview | meshmapper | custom (src/observer/Presets.h)
};

struct KeyDef {
  const char* name;      // what the console calls it, e.g. "wifi.ssid"
  const char* nvs;       // the NVS key (15 characters at most - NVS's limit, not ours)
  ValueKind kind;
  const char* fallback;  // what the board uses while the key is unset; nullptr = nothing
  float min;             // numeric range, Float and Int only
  float max;
  uint16_t minLen;       // text length limits, Text/Secret/Url/Token only
  uint16_t maxLen;
  const char* help;      // one line for `help`
};

// The longest value any key accepts, and the buffer size a caller needs for one.
constexpr size_t kMaxValueLen = 128;

constexpr int kKeyCount = 28;
extern const KeyDef kKeys[kKeyCount];

// The key called `name`, or nullptr. Exact, case-sensitive match - and one alias: `wifi.pwd`, the
// observer firmware's name for the WPA passphrase, finds wifi.pass.
const KeyDef* findKey(const char* name);

// A name the observer firmware has a setting for and this firmware answers with one line
// instead (`timezone`, `bridge.source`): that line, or nullptr for any other name.
const char* explainKey(const char* name);

// True for the keys whose value is never printed.
bool isSecret(const KeyDef& key);

// Validate `value` for `key`. On refusal, `err` (errCap bytes) gets one line saying why.
bool validateValue(const KeyDef& key, const char* value, char* err, size_t errCap);

// What is stored of a value that passed validateValue: the same bytes, except an Iata, which is
// stored uppercase as the observer firmware stores it (CommonCLI_Observer.cpp:335-337).
void normalizeValue(const KeyDef& key, char* value);

// What `get <key>` prints: "<key> <value>", or for a secret "<key> set" / "<key> unset", or for
// an unset non-secret "<key> unset (<fallback>)". `stored` is nullptr or "" when the key is unset.
void describeValue(const KeyDef& key, const char* stored, char* out, size_t cap);

// What `set <key> <value>` answers on success: "ok <key>" - the value is never echoed back,
// which is what makes the reply the same for a secret and for anything else.
void replyForSet(const KeyDef& key, char* out, size_t cap);

// The parsed numeric value of a Float/Int key (the value has passed validateValue).
float numericValue(const char* value);

// The same at double precision, which is what the heartbeat's radio block needs: the record's
// fields are doubles on the .NET side, and 869.618 carried through a float is 869.617981.
double numericValueDouble(const char* value);

}  // namespace mm
