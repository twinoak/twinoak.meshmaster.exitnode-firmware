// The 96 bytes an image carries about itself (task p1.15-58).
//
// FIRMWARE-MANIFEST.md section 4. A manifest describes files, and a file can be renamed or mixed
// up; so every image also says, inside itself, what it is. The release tooling checks it is there
// and agrees with the manifest before anything is published, and this firmware checks it again on
// the bytes it has actually written before it will boot them.
//
//   offset  size  field
//        0     8  magic, ASCII TWOKFWD1
//        8     2  length, 96, little-endian
//       10     1  role: 0 unknown, 1 repeater, 2 bridge, 3 scanner, 16 manager, 32 exit node
//       11     1  slot: 0xFF any
//       12     1  link_major
//       13     1  link_minor
//       14     2  flags, 0
//       16    32  version, ASCII, zero-padded, at least one terminating zero
//       48    24  board
//       72     8  band
//       80    12  reserved, zero
//       92     4  crc32 (IEEE 802.3, as zlib) over bytes 0-91, little-endian
//
// The role byte 32 and the name `exit-node` are this pile's, chosen on 18 November 2026 and not
// in the specification yet (tasks/phase-1.15/FOLLOWUPS.md). tools/release/manifest.py is the same
// table in Python, and makes the bytes the build puts in the image.
//
// Nothing here calls the ESP-IDF, so the native tests build all of it.

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace mm {
namespace update {

static const size_t kDescriptorLength = 96;
static const size_t kDescriptorMagicLength = 8;
static const uint8_t kSlotAny = 0xFF;
static const uint8_t kRoleExitNode = 32;

// "TWOKFWD1", without a terminator: eight bytes compared, never a string.
extern const uint8_t kDescriptorMagic[kDescriptorMagicLength];

struct Descriptor {
  uint8_t role;
  uint8_t slot;
  uint8_t linkMajor;
  uint8_t linkMinor;
  uint16_t flags;
  char version[33];
  char board[25];
  char band[9];
};

// CRC-32 (IEEE 802.3, as zlib) - the one the descriptor's last four bytes hold.
uint32_t crc32(const uint8_t* data, size_t len);

// The 96 bytes read, or false: the magic, the length, the crc, and every string a zero-terminated
// ASCII one. `blob` is exactly kDescriptorLength bytes.
bool parseDescriptor(const uint8_t* blob, Descriptor& out);

// The name of a role byte, for a line a person reads: "exit-node", "repeater", ... and "role-7"
// for one this firmware does not know.
const char* roleName(uint8_t role, char* out, size_t len);

}  // namespace update
}  // namespace mm
