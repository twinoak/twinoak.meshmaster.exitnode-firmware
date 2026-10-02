#include "HostScenario.h"

#include <ArduinoJson.h>

#include <cstdio>
#include <cstring>

namespace mm {

namespace {

bool sameKey(const char* a, const char* b) {
  if (a == nullptr || b == nullptr) return false;
  for (;; a++, b++) {
    const char x = (*a >= 'A' && *a <= 'F') ? static_cast<char>(*a - 'A' + 'a') : *a;
    const char y = (*b >= 'A' && *b <= 'F') ? static_cast<char>(*b - 'A' + 'a') : *b;
    if (x != y) return false;
    if (x == '\0') return true;
  }
}

void copyInto(char* out, size_t cap, const char* text) {
  if (text == nullptr) {
    out[0] = '\0';
    return;
  }
  std::snprintf(out, cap, "%s", text);
}

// The Cayenne LPP type codes this fake writes, by the names a scenario spells. The table is
// CayenneLPP's own and mesh/ReplyCodec.cpp decodes the same numbers.
struct LppType {
  const char* name;
  uint8_t code;
};
constexpr LppType kLppTypes[] = {
    {"voltage", 116}, {"current", 117}, {"temperature", 103}, {"humidity", 104},
    {"pressure", 115}, {"power", 128},  {"energy", 131},      {"altitude", 121},
};

uint8_t lppCodeFor(const char* name) {
  for (const LppType& t : kLppTypes) {
    if (name != nullptr && std::strcmp(t.name, name) == 0) return t.code;
  }
  return 0;
}

}  // namespace

const char* HostRepeaterSpec::pref(const char* key) const {
  for (int i = 0; i < prefCount; i++) {
    if (std::strcmp(prefs[i].key, key) == 0) return prefs[i].value;
  }
  return nullptr;
}

bool HostRepeaterSpec::setPref(const char* key, const char* value) {
  for (int i = 0; i < prefCount; i++) {
    if (std::strcmp(prefs[i].key, key) == 0) {
      copyInto(prefs[i].value, sizeof prefs[i].value, value);
      return true;
    }
  }
  if (prefCount >= kMaxHostRepeaterPrefs) return false;
  HostPref& p = prefs[prefCount++];
  copyInto(p.key, sizeof p.key, key);
  copyInto(p.value, sizeof p.value, value);
  return true;
}

bool HostRepeaterSpec::drops(const char* what) const {
  for (int i = 0; i < dropCount; i++) {
    if (std::strcmp(drop[i], what) == 0) return true;
  }
  return false;
}

bool HostRepeaterSpec::refuses(const char* key) const {
  for (int i = 0; i < refuseCount; i++) {
    if (std::strcmp(refuse[i], key) == 0) return true;
  }
  return false;
}

const HostAclEntry* HostRepeaterSpec::aclEntry(const char* key) const {
  for (int i = 0; i < aclCount; i++) {
    if (sameKey(acl[i].key, key)) return &acl[i];
  }
  return nullptr;
}

HostRepeaterSpec* HostScenario::find(const char* key) {
  for (int i = 0; i < repeaterCount; i++) {
    if (sameKey(repeaters[i].key, key)) return &repeaters[i];
  }
  return nullptr;
}

void defaultScenario(HostScenario& out, const char* exitNodeKey) {
  out = HostScenario();
  copyInto(out.exitNodeKey, sizeof out.exitNodeKey, exitNodeKey);

  out.repeaterCount = 1;
  HostRepeaterSpec& r = out.repeaters[0];
  copyInto(r.key, sizeof r.key, "0f1ba04635a8ca8ca4699225f3f3bbe816cc955c97a49d07e0c6967e79985c78");
  copyInto(r.name, sizeof r.name, "Host Harness #1");
  copyInto(r.ownerInfo, sizeof r.ownerInfo, "the host harness");
  copyInto(r.firmware, sizeof r.firmware, "v1.17.1");
  copyInto(r.build, sizeof r.build, "27 Sep 2025");
  copyInto(r.board, sizeof r.board, "Heltec V3");
  copyInto(r.role, sizeof r.role, "repeater");
  copyInto(r.adminPassword, sizeof r.adminPassword, "harness-admin");
  // The guest password stays empty, which is what a repeater out of the box has: see HostMesh.cpp.
  r.hops = 1;

  // The exit node holds a read-only grant, which is what an association leaves behind.
  r.aclCount = 1;
  copyInto(r.acl[0].key, sizeof r.acl[0].key, exitNodeKey);
  r.acl[0].permissions = kPermReadOnly;

  // The default `get` list of src/agent/ConfigRead.cpp, with the values a repeater of ours has.
  r.setPref("name", "Host Harness #1");
  r.setPref("lat", "56.41");
  r.setPref("lon", "10.88");
  r.setPref("advert.interval", "240");
  r.setPref("flood.advert.interval", "12");
  r.setPref("repeat", "on");
  r.setPref("dutycycle", "10.0");
  r.setPref("flood.max", "3");
  r.setPref("flood.max.unscoped", "0");
  r.setPref("tx", "22");
  r.setPref("path.hash.mode", "1");
  r.setPref("multi.acks", "0");
  r.setPref("allow.read.only", "on");
  r.setPref("guest.password", "");

  copyInto(r.regionListing, sizeof r.regionListing, "*\n dk F\n  jylland F\n  sjaelland F\n");
  copyInto(r.regionDefault, sizeof r.regionDefault, "dk");
  copyInto(r.regionHome, sizeof r.regionHome, "jylland");

  r.neighbourCount = 2;
  copyInto(r.neighbours[0].key, sizeof r.neighbours[0].key,
           "aa48c001d3209e30f71a1317b899410483985b50732d633d98a26df381b12631");
  r.neighbours[0].heardSecondsAgo = 120;
  r.neighbours[0].snr = 6.25;
  copyInto(r.neighbours[1].key, sizeof r.neighbours[1].key,
           "6edf8cc90b04623d4cb05f2805a9ed845005486eaa2aeb7a280b949ce21388c3");
  r.neighbours[1].heardSecondsAgo = 900;
  r.neighbours[1].snr = -2.5;

  r.telemetryCount = 2;
  r.telemetry[0] = {1, lppCodeFor("voltage"), 4.012};
  r.telemetry[1] = {1, lppCodeFor("temperature"), 17.4};
}

bool parseScenario(char* json, size_t len, HostScenario& out, char* err, size_t errCap) {
  err[0] = '\0';
  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, json, len);
  if (error) {
    std::snprintf(err, errCap, "the scenario is not JSON: %s", error.c_str());
    return false;
  }
  const char* exitKey = doc["exitNode"]["publicKey"] | out.exitNodeKey;
  const HostScenario before = out;
  out = HostScenario();
  copyInto(out.exitNodeKey, sizeof out.exitNodeKey, exitKey != nullptr && exitKey[0] != '\0' ? exitKey : before.exitNodeKey);

  JsonArray repeaters = doc["repeaters"].as<JsonArray>();
  if (repeaters.isNull() || repeaters.size() == 0) {
    std::snprintf(err, errCap, "the scenario names no repeaters");
    return false;
  }
  for (JsonObject spec : repeaters) {
    if (out.repeaterCount >= kMaxHostRepeaters) {
      std::snprintf(err, errCap, "the scenario names more than %d repeaters", kMaxHostRepeaters);
      return false;
    }
    HostRepeaterSpec& r = out.repeaters[out.repeaterCount];
    r = HostRepeaterSpec();
    copyInto(r.key, sizeof r.key, spec["publicKey"] | "");
    if (std::strlen(r.key) != kPublicKeyHexLen) {
      std::snprintf(err, errCap, "a repeater's publicKey is not %u hex characters",
                    static_cast<unsigned>(kPublicKeyHexLen));
      return false;
    }
    copyInto(r.name, sizeof r.name, spec["name"] | "repeater");
    copyInto(r.ownerInfo, sizeof r.ownerInfo, spec["ownerInfo"] | "");
    copyInto(r.firmware, sizeof r.firmware, spec["firmware"] | "v1.17.1");
    copyInto(r.build, sizeof r.build, spec["build"] | "27 Sep 2025");
    copyInto(r.board, sizeof r.board, spec["board"] | "Heltec V3");
    copyInto(r.role, sizeof r.role, spec["role"] | "repeater");
    copyInto(r.adminPassword, sizeof r.adminPassword, spec["adminPassword"] | "");
    copyInto(r.guestPassword, sizeof r.guestPassword, spec["guestPassword"] | "");
    r.hops = spec["hops"] | 1;
    r.latencyMs = spec["latencyMs"] | 60u;
    r.online = spec["online"] | true;
    r.clockOffsetS = spec["clockOffsetS"] | -120;
    r.acceptAnyPassword = spec["acceptAnyPassword"] | false;

    // Bound to a local: a range-for over the temporary ArduinoJson returns is a dangling
    // reference, which -Wdangling-reference rightly refuses.
    JsonArray aclEntries = spec["acl"].as<JsonArray>();
    for (JsonObject entry : aclEntries) {
      if (r.aclCount >= kMaxHostAclEntries) break;
      HostAclEntry& a = r.acl[r.aclCount++];
      copyInto(a.key, sizeof a.key, entry["key"] | "");
      a.permissions = static_cast<uint8_t>(entry["permissions"] | 0);
    }
    // `aclEntries` pads the list with synthetic keys, which is how "an ACL of 24" is written down:
    // 24 entries of seven bytes is 172, past what one RESPONSE datagram can carry, so the request
    // is answered with nothing at all (mesh/ReplyCodec.h, kAccessListMaxEntries).
    const int pad = spec["aclEntries"] | 0;
    for (int i = r.aclCount; i < pad && i < kMaxHostAclEntries; i++) {
      HostAclEntry& a = r.acl[r.aclCount++];
      std::snprintf(a.key, sizeof a.key, "%02x%062x", 0xE0 + i, i);
      a.permissions = kPermReadOnly;
    }

    JsonObject prefObject = spec["prefs"].as<JsonObject>();
    for (JsonPair kv : prefObject) {
      r.setPref(kv.key().c_str(), kv.value().as<const char*>());
    }

    JsonArray neighbourArray = spec["neighbours"].as<JsonArray>();
    for (JsonObject n : neighbourArray) {
      if (r.neighbourCount >= kMaxHostNeighbours) break;
      HostNeighbour& h = r.neighbours[r.neighbourCount++];
      copyInto(h.key, sizeof h.key, n["key"] | "");
      h.heardSecondsAgo = n["heardS"] | 0u;
      h.snr = n["snr"] | 0.0;
    }
    // `neighbourCount` synthesises a list, so "more than nine" - the page the firmware answers -
    // is one number in a scenario.
    const int neighbourPad = spec["neighbourCount"] | 0;
    for (int i = r.neighbourCount; i < neighbourPad && i < kMaxHostNeighbours; i++) {
      HostNeighbour& h = r.neighbours[r.neighbourCount++];
      std::snprintf(h.key, sizeof h.key, "%02x%062x", 0xA0 + i, i);
      h.heardSecondsAgo = static_cast<uint32_t>(60 * (i + 1));
      h.snr = 8.0 - 0.25 * i;
    }

    JsonArray telemetryArray = spec["telemetry"].as<JsonArray>();
    for (JsonObject t : telemetryArray) {
      if (r.telemetryCount >= kMaxHostTelemetry) break;
      HostTelemetryReading& reading = r.telemetry[r.telemetryCount++];
      reading.channel = t["channel"] | 1;
      reading.typeCode = lppCodeFor(t["type"] | "");
      reading.value = t["value"] | 0.0;
    }

    JsonObject status = spec["status"].as<JsonObject>();
    if (!status.isNull()) {
      r.status.batteryMv = status["batteryMv"] | r.status.batteryMv;
      r.status.txQueueLen = status["txQueueLen"] | r.status.txQueueLen;
      r.status.noiseFloorDbm = status["noiseFloorDbm"] | r.status.noiseFloorDbm;
      r.status.lastRssi = status["lastRssi"] | r.status.lastRssi;
      r.status.packetsRecv = status["packetsRecv"] | r.status.packetsRecv;
      r.status.packetsSent = status["packetsSent"] | r.status.packetsSent;
      r.status.airtimeS = status["airtimeS"] | r.status.airtimeS;
      r.status.uptimeS = status["uptimeS"] | r.status.uptimeS;
      r.status.sentFlood = status["sentFlood"] | r.status.sentFlood;
      r.status.sentDirect = status["sentDirect"] | r.status.sentDirect;
      r.status.recvFlood = status["recvFlood"] | r.status.recvFlood;
      r.status.recvDirect = status["recvDirect"] | r.status.recvDirect;
      r.status.errEvents = status["errEvents"] | r.status.errEvents;
      r.status.lastSnr = status["lastSnr"] | r.status.lastSnr;
      r.status.directDups = status["directDups"] | r.status.directDups;
      r.status.floodDups = status["floodDups"] | r.status.floodDups;
      r.status.rxAirtimeS = status["rxAirtimeS"] | r.status.rxAirtimeS;
      r.status.recvErrors = status["recvErrors"] | r.status.recvErrors;
      r.status.legacy = status["legacy"] | r.status.legacy;
    }

    JsonObject regions = spec["regions"].as<JsonObject>();
    if (!regions.isNull()) {
      copyInto(r.regionListing, sizeof r.regionListing, regions["listing"] | "*\n");
      copyInto(r.regionDefault, sizeof r.regionDefault, regions["default"] | "");
      copyInto(r.regionHome, sizeof r.regionHome, regions["home"] | "");
    } else {
      copyInto(r.regionListing, sizeof r.regionListing, "*\n");
    }

    JsonArray dropArray = spec["drop"].as<JsonArray>();
    for (JsonVariant v : dropArray) {
      if (r.dropCount >= kMaxHostKnobs) break;
      copyInto(r.drop[r.dropCount++], kMaxHostKnobLen + 1, v.as<const char*>());
    }
    JsonArray refuseArray = spec["refuse"].as<JsonArray>();
    for (JsonVariant v : refuseArray) {
      if (r.refuseCount >= kMaxHostKnobs) break;
      copyInto(r.refuse[r.refuseCount++], kMaxConfigKeyLen + 1, v.as<const char*>());
    }

    out.repeaterCount++;
  }
  return true;
}

}  // namespace mm
