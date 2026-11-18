#include "PartitionTable.h"

#include <cstdio>
#include <cstring>

namespace mm {

namespace {

// The entry, as components/partition_table/gen_esp32part.py writes it (ESP-IDF 4.4.7):
//   0  magic 0xAA 0x50     2  type     3  subtype     4  offset (LE32)
//   8  size (LE32)        12  label (16)             28  flags (LE32)
// The table ends at the first entry whose magic is not the partition magic - the MD5 entry
// (0xEB 0xEB) or the 0xFF of the erased remainder.
constexpr unsigned char kMagic0 = 0xAA;
constexpr unsigned char kMagic1 = 0x50;
constexpr unsigned char kTypeApp = 0x00;
constexpr unsigned char kSubtypeOta0 = 0x10;
constexpr unsigned char kSubtypeOta1 = 0x11;
constexpr unsigned char kSubtypeFactory = 0x00;

// Read in chunks rather than into a 4 kB buffer: this runs on the loop's own stack.
constexpr size_t kChunk = 256;  // a whole number of entries, so none straddles two chunks
static_assert(kChunk % kTableEntryLen == 0, "a chunk must be a whole number of entries");
static_assert(kTableBytesLen % kChunk == 0, "the entries must end on a chunk boundary");
static_assert(kTableSectorLen % kChunk == 0, "the sector must end on a chunk boundary");

uint32_t le32(const unsigned char* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

const char* tableKindWord(TableKind kind) {
  switch (kind) {
    case TableKind::One:
      return "one";
    case TableKind::Two:
      return "two";
    case TableKind::Other:
      break;
  }
  return "other";
}

TableReading readTable(TableFlash& flash, const unsigned char* oneSlotTable) {
  TableReading r;
  unsigned char buf[kChunk];

  bool entriesEnded = false;  // the terminator has been seen
  bool anyEntry = false;
  bool sameAsOneSlot = true;

  for (size_t at = 0; at < kTableSectorLen; at += kChunk) {
    if (!flash.read(at, buf, kChunk)) return r;  // read == false: nothing is claimed about it

    if (at < kTableBytesLen) {
      if (sameAsOneSlot && std::memcmp(buf, oneSlotTable + at, kChunk) != 0) sameAsOneSlot = false;
      for (size_t off = 0; off < kChunk && !entriesEnded; off += kTableEntryLen) {
        const unsigned char* e = buf + off;
        if (e[0] != kMagic0 || e[1] != kMagic1) {
          entriesEnded = true;
          break;
        }
        anyEntry = true;
        if (e[2] == kTypeApp) {
          SlotInfo* slot = nullptr;
          if (e[3] == kSubtypeOta0 || e[3] == kSubtypeFactory) {
            slot = &r.app0;
          } else if (e[3] == kSubtypeOta1) {
            slot = &r.app1;
          }
          if (slot != nullptr && !slot->present) {
            slot->present = true;
            slot->offset = le32(e + 4);
            slot->size = le32(e + 8);
          }
        }
      }
    } else if (sameAsOneSlot) {
      // Above the entries a merged image leaves 0xFF, and so does an erase. A board whose sector
      // carries anything else there is not one this firmware wrote, whatever the entries say.
      for (size_t i = 0; i < kChunk; i++) {
        if (buf[i] != 0xFF) {
          sameAsOneSlot = false;
          break;
        }
      }
    }
  }

  r.read = true;
  if (anyEntry && r.app1.present) {
    r.kind = TableKind::Two;
  } else if (sameAsOneSlot) {
    r.kind = TableKind::One;
  } else {
    r.kind = TableKind::Other;
  }
  return r;
}

AddSlotResult addSecondSlot(TableFlash& flash, const unsigned char* oneSlotTable, const unsigned char* twoSlotTable) {
  AddSlotResult out;
  const TableReading before = readTable(flash, oneSlotTable);

  if (!before.read) {
    std::snprintf(out.line, sizeof out.line, "the partition table could not be read; nothing was written");
    return out;
  }
  if (before.kind == TableKind::Two) {
    // `two` is decided by app1 being in the table, so it is there to be named.
    std::snprintf(out.line, sizeof out.line, "the second slot is already there: app1 at 0x%06lX, 0x%06lX bytes; nothing written",
                  static_cast<unsigned long>(before.app1.offset), static_cast<unsigned long>(before.app1.size));
    return out;
  }
  if (before.kind != TableKind::One) {
    std::snprintf(out.line, sizeof out.line,
                  "this board's partition table is not one this firmware knows, so it will not be written over; "
                  "flash the merged image of a release over USB to get the new table");
    return out;
  }

  if (!flash.eraseSector()) {
    std::snprintf(out.line, sizeof out.line,
                  "the partition table's sector could not be erased; the table may now be unreadable - "
                  "flash the merged image of a release over USB before rebooting");
    return out;
  }
  if (!flash.write(0, twoSlotTable, kTableBytesLen)) {
    std::snprintf(out.line, sizeof out.line,
                  "the new partition table could not be written; the sector is erased - "
                  "flash the merged image of a release over USB before rebooting");
    return out;
  }

  // What is there now, read back from the flash and compared with what should be: the entries
  // against the new table, and the sector above them against the 0xFF an erase leaves.
  unsigned char buf[kChunk];
  for (size_t at = 0; at < kTableSectorLen; at += kChunk) {
    if (!flash.read(at, buf, kChunk)) {
      std::snprintf(out.line, sizeof out.line,
                    "the new partition table was written but could not be read back at 0x%03lX; "
                    "check it with `update` before rebooting",
                    static_cast<unsigned long>(at));
      return out;
    }
    for (size_t i = 0; i < kChunk; i++) {
      const unsigned char want = (at + i < kTableBytesLen) ? twoSlotTable[at + i] : 0xFF;
      if (buf[i] != want) {
        std::snprintf(out.line, sizeof out.line,
                      "the sector reads 0x%02X at 0x%03lX where the new table has 0x%02X; the slot was NOT added - "
                      "flash the merged image of a release over USB before rebooting",
                      buf[i], static_cast<unsigned long>(at + i), want);
        return out;
      }
    }
  }

  const TableReading after = readTable(flash, oneSlotTable);
  if (after.kind != TableKind::Two || !after.app1.present) {
    std::snprintf(out.line, sizeof out.line,
                  "the sector was written and reads back as it was written, but the table in it has no second slot - "
                  "flash the merged image of a release over USB before rebooting");
    return out;
  }
  out.added = true;
  std::snprintf(out.line, sizeof out.line, "added: app1 at 0x%06lX, 0x%06lX bytes. Type `reboot`; the table is read at the next boot",
                static_cast<unsigned long>(after.app1.offset), static_cast<unsigned long>(after.app1.size));
  return out;
}

}  // namespace mm
