// The broker presets: a table like the observer firmware's MQTTPresetDef (agessaman/MeshCore,
// branch observer-firmware, d664d89df66bdacc1319900a2e2d152fa31b9dc9, src/helpers/MQTTPresets.h:32-44)
// with only what this firmware needs - two rows, where that firmware has thirty-nine.
//
//   meshmapper   MQTTPresets.h:141, the row as written there: wss://mqtt.meshmapper.net:443/mqtt,
//                audience mqtt.meshmapper.net, JWT, the default token lifetime, retain allowed,
//                keepalive 55 s. (Its root is ISRG Root X1 in that row; which root this firmware
//                carries for the host is task 27's, read off the live chain.)
//   meshview     the Danish guide's four `custom` lines as one name: wss://mqtt.meshview.dk:443/mqtt,
//                audience mqtt.meshview.dk, JWT, the default topic. The rest of the row is what
//                a custom slot gets in the observer firmware, because that is what those lines
//                configured: no retain (MQTTBridge.cpp:3195, "Custom slots default to
//                non-retained"), the client's default keepalive (MQTTBridge.cpp:2008-2012: a
//                preset keepalive of 0 leaves the default).
//   custom       the slot's own mqttN.server, mqttN.audience, mqttN.topic - and mqttN.username
//                and mqttN.password for a broker that wants those instead of a token
//                (MQTT_IMPLEMENTATION.md, "Custom Brokers"; MQTTPresets.h:201-202 for the two names)
//   none         the slot is off
//
// No preset ships a shared secret: the observer firmware's rows that carry a username and a
// password (tennmesh, nashmesh, ctmesh, marwoj) are not here, and a custom slot's are the owner's.
//
// Host-portable.

#pragma once

#include <cstddef>
#include <cstdint>

namespace mm {
namespace observer {

enum class Auth : uint8_t {
  None,      // the broker takes anyone (not a preset here; a custom slot with nothing set is off)
  UserPass,  // mqttN.username and mqttN.password
  Jwt,       // the Ed25519 token (Token.h)
};

struct Preset {
  const char* name;
  const char* serverUrl;    // wss://host:port/path
  const char* audience;     // the token's "aud"
  Auth auth;
  uint32_t tokenLifetime;   // seconds; 0 = kDefaultTokenLifetime
  bool allowRetain;         // whether the broker takes the retain flag on status
  uint16_t keepalive;       // seconds; 0 = the client's default
};

constexpr int kPresetCount = 2;
extern const Preset kPresets[kPresetCount];

constexpr const char* kPresetNone = "none";
constexpr const char* kPresetCustom = "custom";

// The row called `name`, or nullptr ("custom" and "none" are not rows).
const Preset* findPreset(const char* name);

// True for a row's name, "custom" or "none": what `set mqttN.preset` accepts.
bool isPresetName(const char* name);

// One line per preset for `get mqtt.presets`, plus custom and none: "<name>  <url>  <auth>", with
// what a custom slot needs. `line` is called once per line with `ctx`.
void describePresets(void (*line)(void* ctx, const char* text), void* ctx);

}  // namespace observer
}  // namespace mm
