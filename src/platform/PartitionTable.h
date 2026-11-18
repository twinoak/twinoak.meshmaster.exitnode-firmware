// Which partition table this board runs on, and how a second application slot is added to it.
//
// A board gets its table from a merged image, and the web flasher erases the whole board before
// it writes one - the key, the token and the WiFi with it. So no board in the field is asked for
// a merged image: it takes a release as the plain `.bin`, which keeps everything and keeps the
// table it already has, and the firmware writes the one sector of the table itself when a person
// asks for it on the console (`update slot`). Task p1.15-57; boards/README.md is the page.
//
// Three words, and no fourth:
//
//   two    the table has a second application slot (app/ota_1). The new table of
//          boards/partitions/meshmaster_<size>.csv is one, and so is the framework's own default,
//          which a board provisioned under v0.1.1 or earlier and never flashed with a merged
//          image since still holds. Nothing to do.
//   one    the sector holds, byte for byte, this board's meshmaster_<size>_one_slot.csv - the
//          table every merged image from v0.2.0 to v0.7.1 wrote. `update slot` may replace it.
//   other  anything else. The firmware will not write a table it does not recognise.
//
// Everything here is portable and host-tested (test/test_partition_table): the flash itself is
// behind TableFlash, which src/platform/TableFlash.cpp implements for the board and which the
// tests implement with a scripted sector. **TableFlash.cpp is the only file of src/ and lib/
// that may erase or write raw flash**, and test/test_flash_writes greps the tree for it.

#pragma once

#include <cstddef>
#include <cstdint>

namespace mm {

// The table is one sector of 4 kB. gen_esp32part.py writes 0xC00 of entries (96 of 32 bytes, the
// MD5 entry among them) and pads that to 0xC00 with 0xFF; the 0x400 above it is 0xFF too, because
// a merged image fills every gap with 0xFF and nothing is written there. The sector's own address
// is TableFlash.cpp's business and is not here.
constexpr size_t kTableSectorLen = 0x1000;
constexpr size_t kTableBytesLen = 0xC00;
constexpr size_t kTableEntryLen = 32;

// What the firmware may do to that sector. Offsets are inside it, so a caller cannot name an
// address at all; the one implementation that touches flash bounds every call to the sector.
class TableFlash {
 public:
  virtual ~TableFlash() = default;
  virtual bool read(size_t at, unsigned char* into, size_t len) = 0;
  virtual bool eraseSector() = 0;
  virtual bool write(size_t at, const unsigned char* from, size_t len) = 0;
};

enum class TableKind : unsigned char { One, Two, Other };

const char* tableKindWord(TableKind kind);

struct SlotInfo {
  bool present = false;
  uint32_t offset = 0;
  uint32_t size = 0;
};

struct TableReading {
  bool read = false;  // the sector could be read at all
  TableKind kind = TableKind::Other;
  SlotInfo app0;
  SlotInfo app1;
};

// One pass over the sector, in chunks: the word, and the two application slots when the table
// parses. `oneSlotTable` is this board's kTableBytesLen of "the table boards hold today".
TableReading readTable(TableFlash& flash, const unsigned char* oneSlotTable);

struct AddSlotResult {
  bool added = false;
  char line[200] = {0};  // what the console prints, always one line
};

// The one function that adds the slot, and the only caller of eraseSector()/write(). It acts only
// when the word is `one`: erase the sector, write `twoSlotTable`, read the sector back and compare
// it with what should now be there. It restarts nothing - the new table is read at the next boot.
AddSlotResult addSecondSlot(TableFlash& flash, const unsigned char* oneSlotTable, const unsigned char* twoSlotTable);

}  // namespace mm
