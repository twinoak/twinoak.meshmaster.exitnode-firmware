// A hashtag region's transport key, and the transport code a packet carries in it.
//
// This is MeshCore's own arithmetic, read at the pin (`81dbae59`) and written here because our
// floods are built here and not by a companion on a cable:
//
//   the key    `TransportKeyStore::getAutoKeyFor` (src/helpers/TransportKeyStore.cpp): a region's
//              key is the first sixteen bytes of SHA-256 over its name **with a leading `#`**.
//              `RegionMap::getTransportKeysFor` is what adds the `#` for a name stored without
//              one, and the companion's own default scope does the same
//              (`temp.getAutoKeyFor(0, "#" DEFAULT_FLOOD_SCOPE_NAME, key)`,
//              examples/companion_radio/MyMesh.cpp ~L969-976). So a public region's key is public
//              by construction - which is why `get flood.scope` may print it, and why a private
//              region (`$name`, whose key lives in the node's own keystore) is out of scope here
//   the code   `TransportKey::calcTransportCode` (same file): HMAC-SHA256 with that key over the
//              packet's payload-type byte and its payload, truncated to the first two bytes,
//              with 0000 and FFFF reserved (a code of 0 becomes 1 and FFFF becomes FFFE). Since
//              MeshCore 1.18 (`01f9293b`, #3568) a repeater reads a transport code of 0 as
//              **un-scoped** - `RegionMap::findMatch` answers its wildcard for it - so the
//              reservation is what keeps a scoped flood of ours from being forwarded as an
//              unscoped one: `reserveTransportCode` below never lets a 0 out (task p1.8-34)
//   the name   `RegionMap::is_name_char` (src/helpers/RegionMap.cpp): `-`, `$`, `#`, `0`..`9` and
//              every byte from `A` upwards - "all alpha-num or accented characters, but exclude
//              most punctuation chars" - in a `RegionEntry::name` of `char[31]`, so thirty
//              characters at most
//
// **The SHA-256 is rweather/Crypto's**, the same library MeshCore hashes with: the two ends have
// to agree on the code byte for byte, and the surest way to agree is to run the same code. The
// native build compiles three of its files for this (`platformio.ini`'s `[env:native]` and
// `tools/pio/meshcore_private_libs.py`), which is why this file is host-portable and includes
// nothing of MeshCore: `test/test_flood_scope` walks it on a host against published HMAC-SHA256
// vectors and against an HMAC built in the test from the library's plain SHA-256.
//
// `docs/upstream.md` lists all three above among the citations a pin bump re-reads.

#pragma once

#include <cstddef>
#include <cstdint>

namespace mm {

// `RegionEntry::name` is `char[31]` at the pin: thirty characters and the NUL.
constexpr size_t kRegionNameCap = 30;
// `TransportKey::key` is sixteen bytes.
constexpr size_t kTransportKeyBytes = 16;

// Is this a region name this firmware will take? `*` (every flood unscoped) is accepted; so is a
// single leading `#`, which `normalizeValue` drops. A name of a **private** region (`$name`) is
// refused: its key is not derivable, and the setting takes names and never keys.
bool isFloodScopeValue(const char* value);

// The name with a leading `#` dropped. Never nullptr for a value `isFloodScopeValue` took.
const char* floodScopeBareName(const char* value);

// A public hashtag region's transport key: the first sixteen bytes of SHA-256 over `#` and
// `bareName` (`getAutoKeyFor`). `bareName` is the name without the `#`.
void floodScopeKey(const char* bareName, uint8_t key[kTransportKeyBytes]);

// 0000 and FFFF are reserved (`calcTransportCode`): a code of 0 becomes 1 and one of FFFF becomes
// FFFE. Its own function so that the rule can be walked without hunting for a payload that hashes
// to either.
uint16_t reserveTransportCode(uint16_t code);

// The transport code of a packet in that scope (`calcTransportCode`): HMAC-SHA256 with the key
// over the payload-type byte and the payload, its first two bytes read little-endian as MeshCore
// reads them, 0000 and FFFF reserved.
uint16_t transportCodeFor(const uint8_t key[kTransportKeyBytes], uint8_t payloadType, const uint8_t* payload,
                          size_t payloadLen);

// The example payload `get flood.scope` prints a code for, so that two boards given the same
// scope can be compared without a repeater in the middle: payload type 0 (`PAYLOAD_TYPE_REQ`) and
// the eight bytes 00..07. Documented in docs/console.md.
constexpr uint8_t kExamplePayloadType = 0;
constexpr size_t kExamplePayloadLen = 8;
uint16_t exampleTransportCode(const uint8_t key[kTransportKeyBytes]);

}  // namespace mm
