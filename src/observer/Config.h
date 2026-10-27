// What the observer's settings mean: the two slots the console's keys configure, resolved into
// what a connection needs - a URL, an audience or a username and a password, a topic template,
// retain and keepalive - and whether the observer is on at all. The settings themselves are
// src/settings/Keys.cpp's (mqtt.iata, mqtt.owner, mqtt.email, mqtt.origin, mqtt1.* and mqtt2.*);
// this is the reading of them, the observer firmware's (agessaman/MeshCore, observer-firmware,
// d664d89df66bdacc1319900a2e2d152fa31b9dc9):
//
//   a preset name       the row, and the default topic                     MQTTBridge.cpp:3066-3094 (buildTopicForSlot)
//   custom              mqttN.server is required; mqttN.audience set means   MQTT_IMPLEMENTATION.md "Custom Brokers";
//                       the token (JWT), else mqttN.username and             MQTTBridge.cpp:2043-2064, 2948-2954
//                       mqttN.password mean those, else the slot cannot
//                       connect; mqttN.topic set replaces the default
//   none, or unset      the slot is off                                     MQTTPrefsSerializer.h:213-216 (an empty preset is "none")
//   mqtt.iata           needed by every slot on the default topic           MQTTTopicRouter.h:67-71
//
// Two slots (the README of pile 1.7: "Two slots"): the observer firmware runs six on a board
// with PSRAM and at most two TLS connections on one without, which a Heltec V3 is.
//
// Host-portable: strings in, a struct of pointers out, nothing stored.

#pragma once

#include <cstddef>
#include <cstdint>

#include "Presets.h"

namespace mm {
namespace observer {

constexpr int kSlots = 2;

// A slot's six settings as the store holds them ("" when unset).
struct SlotSettings {
  const char* preset = "";
  const char* server = "";
  const char* audience = "";
  const char* topic = "";
  const char* username = "";
  const char* password = "";
};

struct ObserverSettings {
  const char* iata = "";
  const char* owner = "";
  const char* email = "";
  const char* origin = "";
  SlotSettings slot[kSlots];
};

// What a slot resolves to. The pointers point into the settings or the preset table.
struct ResolvedSlot {
  bool active = false;         // configured well enough to connect
  const char* why = "";        // when !active: one phrase saying what is missing
  const char* name = "none";   // the preset's name, or "custom"
  const char* serverUrl = "";
  const char* audience = "";   // Auth::Jwt only
  const char* topic = nullptr; // a custom template, or nullptr for the default layout
  Auth auth = Auth::None;
  const char* username = "";   // Auth::UserPass only
  const char* password = "";   // Auth::UserPass only; never printed
  uint32_t tokenLifetime = 0;  // 0 = the default
  bool allowRetain = false;
  uint16_t keepalive = 0;      // 0 = the client's default
};

ResolvedSlot resolveSlot(const SlotSettings& s);

// True when at least one slot is active and mqtt.iata is usable (or every active slot has its
// own template that does not need it); `why` (whyCap bytes) says what is missing otherwise.
bool observerEnabled(const ObserverSettings& s, char* why, size_t whyCap);

}  // namespace observer
}  // namespace mm
