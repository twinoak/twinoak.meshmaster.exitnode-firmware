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

// One erase, one write and a read-back of the whole sector, which is what writing this table means
// - and the only place that asks for either. True when the sector holds `want` byte for byte with
// 0xFF above it *and* parses as `expect`; false, and nothing claimed, at the first thing that did
// not go. A caller tries again rather than reading a reason: there is nothing else to do with one.
// (task p1.16-63)
bool writeTheTable(TableFlash& flash, const unsigned char* want, const unsigned char* oneSlotTable, TableKind expect,
                   TableReading* reading = nullptr) {
  if (!flash.eraseSector()) return false;
  if (!flash.write(0, want, kTableBytesLen)) return false;

  unsigned char buf[kChunk];
  for (size_t at = 0; at < kTableSectorLen; at += kChunk) {
    if (!flash.read(at, buf, kChunk)) return false;
    for (size_t i = 0; i < kChunk; i++) {
      const unsigned char should = (at + i < kTableBytesLen) ? want[at + i] : 0xFF;
      if (buf[i] != should) return false;
    }
  }

  const TableReading after = readTable(flash, oneSlotTable);
  if (!after.read || after.kind != expect) return false;
  if (expect == TableKind::Two && !after.app1.present) return false;
  if (reading != nullptr) *reading = after;
  return true;
}

// What a person must read while the sector holds no table, wherever they come to read it.
void sayWhatIsTrue(char* line, size_t cap, int tries) {
  std::snprintf(line, cap,
                "DO NOT CUT THE POWER: the partition table's sector holds no table and the old one is being put back "
                "once a minute (%d more tr%s so far). A board that reboots first needs the merged image over USB",
                tries, tries == 1 ? "y" : "ies");
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

const char* addSlotEndingWord(AddSlotEnding ending) {
  switch (ending) {
    case AddSlotEnding::Added:
      return "added";
    case AddSlotEnding::AsItWas:
      return "as-it-was";
    case AddSlotEnding::NoTable:
      break;
  }
  return "no-table";
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

  // The new table, up to kTableTries times. A write that fails once - a sector that did not erase,
  // a byte that read back wrong - may hold the next time, and while this firmware runs it can
  // still ask. Nothing is given up on until every try is spent.
  for (int t = 1; t <= kTableTries; t++) {
    out.tries = t;
    TableReading after;
    if (writeTheTable(flash, twoSlotTable, oneSlotTable, TableKind::Two, &after)) {
      out.ending = AddSlotEnding::Added;
      out.added = true;
      if (t == 1) {
        std::snprintf(out.line, sizeof out.line,
                      "added: app1 at 0x%06lX, 0x%06lX bytes. Type `reboot`; the table is read at the next boot",
                      static_cast<unsigned long>(after.app1.offset), static_cast<unsigned long>(after.app1.size));
      } else {
        std::snprintf(out.line, sizeof out.line,
                      "added after %d tries: app1 at 0x%06lX, 0x%06lX bytes. Type `reboot`; the table is read at "
                      "the next boot",
                      t, static_cast<unsigned long>(after.app1.offset), static_cast<unsigned long>(after.app1.size));
      }
      return out;
    }
  }

  // It would not hold. An erase that never took leaves the old table where it was, and that board
  // boots: there is nothing to put back and nothing should be written over it.
  const TableReading now = readTable(flash, oneSlotTable);
  if (now.read && now.kind == TableKind::One) {
    out.ending = AddSlotEnding::AsItWas;
    std::snprintf(out.line, sizeof out.line,
                  "the slot was NOT added: the sector would not take the new table in %d tries and nothing was "
                  "written over the old one, so the board is as it was",
                  kTableTries);
    return out;
  }

  // The old table back, the same way and as many times. A board whose table is the one it had is a
  // board that boots, and that is worth more than the second slot.
  for (int t = 1; t <= kTableTries; t++) {
    out.triesBack = t;
    if (writeTheTable(flash, oneSlotTable, oneSlotTable, TableKind::One)) {
      out.ending = AddSlotEnding::AsItWas;
      std::snprintf(out.line, sizeof out.line,
                    "the slot was NOT added: the new table would not hold in %d tries and the old one is back in "
                    "the sector after %d, so the board is as it was",
                    kTableTries, t);
      return out;
    }
  }

  // Neither holds. This is the one ending that leaves a board which does not boot, and the sentence
  // says so: the firmware goes on trying (TableKeeper) for as long as it runs.
  out.ending = AddSlotEnding::NoTable;
  std::snprintf(out.line, sizeof out.line,
                "the slot was NOT added and the sector holds NO table: do NOT cut the power - the old table is put "
                "back once a minute from now. A board that reboots first needs the merged image over USB");
  return out;
}

void TableKeeper::note(AddSlotEnding ending, uint32_t nowMs) {
  if (ending != AddSlotEnding::NoTable) {
    trying_ = false;
    tries_ = 0;
    line_[0] = '\0';
    return;
  }
  trying_ = true;
  tries_ = 0;
  lastTryMs_ = nowMs;  // addSecondSlot has just spent its own tries; the next one is a minute away
  sayWhatIsTrue(line_, sizeof line_, tries_);
}

KeeperStep TableKeeper::step(TableFlash& flash, const unsigned char* oneSlotTable, uint32_t nowMs) {
  if (!trying_) return KeeperStep::Nothing;
  if (nowMs - lastTryMs_ < kPutTheTableBackEveryMs) return KeeperStep::Nothing;  // unsigned: a wrap is one minute
  lastTryMs_ = nowMs;
  tries_++;
  if (writeTheTable(flash, oneSlotTable, oneSlotTable, TableKind::One)) {
    trying_ = false;
    std::snprintf(line_, sizeof line_,
                  "the old partition table is back in the sector after %d more tr%s; the board boots again, and the "
                  "second slot was not added",
                  tries_, tries_ == 1 ? "y" : "ies");
    return KeeperStep::Back;
  }
  sayWhatIsTrue(line_, sizeof line_, tries_);
  return KeeperStep::Tried;
}

}  // namespace mm
