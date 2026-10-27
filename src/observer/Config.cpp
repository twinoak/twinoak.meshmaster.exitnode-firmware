#include "Config.h"

#include <cstdio>
#include <cstring>

#include "Topics.h"

namespace mm {
namespace observer {

namespace {

bool set(const char* s) { return s != nullptr && s[0] != '\0'; }

}  // namespace

ResolvedSlot resolveSlot(const SlotSettings& s) {
  ResolvedSlot r;
  if (!set(s.preset) || std::strcmp(s.preset, kPresetNone) == 0) {
    r.why = "preset is none";
    return r;
  }
  if (const Preset* p = findPreset(s.preset)) {
    r.active = true;
    r.name = p->name;
    r.serverUrl = p->serverUrl;
    r.audience = p->audience;
    r.auth = p->auth;
    r.tokenLifetime = p->tokenLifetime;
    r.allowRetain = p->allowRetain;
    r.keepalive = p->keepalive;
    return r;
  }
  if (std::strcmp(s.preset, kPresetCustom) != 0) {
    r.why = "unknown preset";
    return r;
  }
  r.name = kPresetCustom;
  if (!set(s.server)) {
    r.why = "custom needs mqttN.server";
    return r;
  }
  r.serverUrl = s.server;
  r.topic = set(s.topic) ? s.topic : nullptr;
  if (set(s.audience)) {
    r.auth = Auth::Jwt;
    r.audience = s.audience;
  } else if (set(s.username) && set(s.password)) {
    r.auth = Auth::UserPass;
    r.username = s.username;
    r.password = s.password;
  } else {
    r.why = "custom needs mqttN.audience, or mqttN.username and mqttN.password";
    return r;
  }
  r.active = true;
  return r;
}

bool observerEnabled(const ObserverSettings& s, char* why, size_t whyCap) {
  int active = 0;
  int needIata = 0;
  for (int i = 0; i < kSlots; i++) {
    const ResolvedSlot r = resolveSlot(s.slot[i]);
    if (!r.active) continue;
    active++;
    if (r.topic == nullptr) needIata++;
  }
  if (active == 0) {
    if (why != nullptr && whyCap > 0) std::snprintf(why, whyCap, "no slot configured (set mqtt1.preset ...)");
    return false;
  }
  if (needIata > 0 && !iataUsable(s.iata)) {
    if (why != nullptr && whyCap > 0) std::snprintf(why, whyCap, "mqtt.iata is %s", set(s.iata) ? "not a usable code" : "unset");
    return false;
  }
  if (why != nullptr && whyCap > 0) why[0] = '\0';
  return true;
}

}  // namespace observer
}  // namespace mm
