#include "mesh/TransportScope.h"

#include <cstring>

#include <agent/FloodScope.h>

// rweather/Crypto's SHA-256, pinned in platformio.ini's [libs]: the one MeshCore's
// TransportKeyStore.cpp includes, so that the key and the code this board computes are the ones a
// repeater computes. Compiled for the host build by tools/pio/meshcore_private_libs.py.
#include <SHA256.h>

namespace mm {

namespace {

// RegionMap::is_name_char at the pin: `-`, `$`, `#`, a digit, or any byte from 'A' upwards.
bool isMeshCoreNameChar(unsigned char c) {
  return c == '-' || c == '$' || c == '#' || (c >= '0' && c <= '9') || c >= 'A';
}

}  // namespace

bool isFloodScopeValue(const char* value) {
  if (value == nullptr) return false;
  if (floodScopeIsUnscoped(value)) return true;
  const char* name = floodScopeBareName(value);
  const size_t len = std::strlen(name);
  if (len == 0 || len > kRegionNameCap) return false;
  for (const char* p = name; *p != '\0'; p++) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (!isMeshCoreNameChar(c)) return false;
    // `#` only as the one leading character this takes, and never `$`: a private region's key is
    // in the node's own keystore and cannot be derived from its name.
    if (c == '#' || c == '$') return false;
  }
  return true;
}

const char* floodScopeBareName(const char* value) {
  if (value == nullptr) return "";
  return value[0] == '#' ? value + 1 : value;
}

void floodScopeKey(const char* bareName, uint8_t key[kTransportKeyBytes]) {
  std::memset(key, 0, kTransportKeyBytes);
  if (bareName == nullptr) return;
  // `#` and the name, hashed whole, and the first sixteen bytes kept (getAutoKeyFor).
  SHA256 sha;
  const char hash = '#';
  sha.update(&hash, 1);
  sha.update(bareName, std::strlen(bareName));
  sha.finalize(key, kTransportKeyBytes);
}

uint16_t reserveTransportCode(uint16_t code) {
  if (code == 0) return 1;
  if (code == 0xFFFF) return 0xFFFE;
  return code;
}

uint16_t transportCodeFor(const uint8_t key[kTransportKeyBytes], uint8_t payloadType, const uint8_t* payload,
                          size_t payloadLen) {
  SHA256 sha;
  sha.resetHMAC(key, kTransportKeyBytes);
  sha.update(&payloadType, 1);
  if (payload != nullptr && payloadLen > 0) sha.update(payload, payloadLen);
  // MeshCore finalises straight into a `uint16_t`, which on both ends of this protocol - an
  // ESP32-S3 and the hosts these tests run on - is the first two bytes of the HMAC little-endian.
  // Taking the bytes and assembling them says the same thing without depending on the endianness
  // of whatever compiles this.
  uint8_t mac[2] = {0, 0};
  sha.finalizeHMAC(key, kTransportKeyBytes, mac, sizeof mac);
  return reserveTransportCode(static_cast<uint16_t>(mac[0] | (static_cast<uint16_t>(mac[1]) << 8)));
}

uint16_t exampleTransportCode(const uint8_t key[kTransportKeyBytes]) {
  uint8_t payload[kExamplePayloadLen];
  for (size_t i = 0; i < kExamplePayloadLen; i++) payload[i] = static_cast<uint8_t>(i);
  return transportCodeFor(key, kExamplePayloadType, payload, sizeof payload);
}

}  // namespace mm
