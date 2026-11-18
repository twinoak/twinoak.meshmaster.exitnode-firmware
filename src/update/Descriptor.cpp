// FIRMWARE-MANIFEST.md section 4, read.

#include "Descriptor.h"

#include <stdio.h>
#include <string.h>

namespace mm {
namespace update {

const uint8_t kDescriptorMagic[kDescriptorMagicLength] = {'T', 'W', 'O', 'K', 'F', 'W', 'D', '1'};

namespace {

// Zero-padded ASCII with at least one terminating zero (section 4), copied out as a C string.
// A byte outside printable ASCII, or no terminator in the field, is a descriptor that is not one:
// the strings end up in a line a person reads and in a comparison with the manifest.
bool readText(const uint8_t* field, size_t size, char* out, size_t outLen) {
  size_t at = 0;
  while (at < size && field[at] != 0) {
    const uint8_t c = field[at];
    if (c < 0x20 || c > 0x7E) return false;
    at++;
  }
  if (at == size) return false;  // no terminator, which section 4 asks for
  if (at + 1 > outLen) return false;
  memcpy(out, field, at);
  out[at] = '\0';
  return true;
}

uint16_t le16(const uint8_t* at) { return static_cast<uint16_t>(at[0] | (at[1] << 8)); }

uint32_t le32(const uint8_t* at) {
  return static_cast<uint32_t>(at[0]) | (static_cast<uint32_t>(at[1]) << 8) |
         (static_cast<uint32_t>(at[2]) << 16) | (static_cast<uint32_t>(at[3]) << 24);
}

}  // namespace

uint32_t crc32(const uint8_t* data, size_t len) {
  // Bitwise, so there is no 1 kB table in the image for 92 bytes a release.
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; bit++) {
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  return crc ^ 0xFFFFFFFFu;
}

bool parseDescriptor(const uint8_t* blob, Descriptor& out) {
  if (blob == nullptr) return false;
  if (memcmp(blob, kDescriptorMagic, kDescriptorMagicLength) != 0) return false;
  if (le16(blob + 8) != kDescriptorLength) return false;
  if (le32(blob + 92) != crc32(blob, kDescriptorLength - 4)) return false;
  // Bytes 80-91 are reserved and this reader does not look at them: the crc already covers them,
  // and a schema that gave them a meaning would be refused here for nothing.
  Descriptor read;
  read.role = blob[10];
  read.slot = blob[11];
  read.linkMajor = blob[12];
  read.linkMinor = blob[13];
  read.flags = le16(blob + 14);
  if (!readText(blob + 16, 32, read.version, sizeof(read.version))) return false;
  if (!readText(blob + 48, 24, read.board, sizeof(read.board))) return false;
  if (!readText(blob + 72, 8, read.band, sizeof(read.band))) return false;
  out = read;
  return true;
}

const char* roleName(uint8_t role, char* out, size_t len) {
  switch (role) {
    case 0: return "unknown";
    case 1: return "repeater";
    case 2: return "bridge";
    case 3: return "scanner";
    case 16: return "manager";
    case kRoleExitNode: return "exit-node";
    default: break;
  }
  if (out == nullptr || len == 0) return "role-?";
  snprintf(out, len, "role-%u", static_cast<unsigned>(role));
  return out;
}

}  // namespace update
}  // namespace mm
