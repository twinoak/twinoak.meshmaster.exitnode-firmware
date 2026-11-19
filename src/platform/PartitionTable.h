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
// A write that fails is tried again (task p1.16-63). The one sector a board boots by is worth
// more than the second slot, so `addSecondSlot` tries the new table up to kTableTries times, puts
// the old one back as many times when it will not hold, and only then says the sector holds
// neither - and `TableKeeper` goes on putting the old one back, once a minute, for as long as the
// firmware runs. A board left with no table does not boot and needs a merged image over USB, which
// costs it its key, its token and its WiFi; while this firmware still runs it can still write.
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

// How a write of the table ended, and there is no fourth (task p1.16-63). Two of the three leave a
// board that boots; the third is the one that must not lose power.
enum class AddSlotEnding : unsigned char {
  Added = 0,    // the sector holds the new table, and the board has a second slot at the next boot
  AsItWas = 1,  // the slot was not added and the sector holds the table the board came with
  NoTable = 2,  // the slot was not added and the sector holds neither table
};

const char* addSlotEndingWord(AddSlotEnding ending);

// How many times the new table, and then the old one, is tried before either is given up on. A
// write that fails once may hold the next time; one that fails three times will not.
constexpr int kTableTries = 3;

struct AddSlotResult {
  AddSlotEnding ending = AddSlotEnding::AsItWas;
  bool added = false;          // ending == Added, which is what every caller asks first
  int tries = 0;               // tries of the new table, 0 when nothing was written at all
  int triesBack = 0;           // tries of the old one afterwards, 0 when it was never needed
  char line[200] = {0};        // what the console prints, always one line
};

// The one function that adds the slot, and with TableKeeper the only caller of
// eraseSector()/write(). It acts only when the word is `one`: erase the sector, write
// `twoSlotTable`, read the sector back and compare it with what should now be there - and it does
// that up to kTableTries times, because a board that is still running can still write. When the
// new table will not hold it puts `oneSlotTable` back, as many times, and reads that back too: a
// board whose table is the one it had is a board that boots. Only when neither holds does it end
// `NoTable`, and then the sector holds nothing and TableKeeper takes over. It restarts nothing -
// the new table is read at the next boot.
AddSlotResult addSecondSlot(TableFlash& flash, const unsigned char* oneSlotTable, const unsigned char* twoSlotTable);

// What happens after an `NoTable`: the old table, once a minute, for as long as the firmware runs
// (task p1.16-63, deliverable 4). While the firmware runs it holds both tables byte for byte and
// can still write, so a sector that takes no write this minute may take one the next; the first
// that holds ends it, and the board boots again. Nothing here restarts anything.
constexpr uint32_t kPutTheTableBackEveryMs = 60u * 1000u;

enum class KeeperStep : unsigned char {
  Nothing = 0,  // the sector holds a table, or this minute's try has already been made
  Tried = 1,    // the old table was tried again and the sector still holds none
  Back = 2,     // it holds: the board boots again, and nothing more is tried
};

class TableKeeper {
 public:
  // What addSecondSlot ended with. `NoTable` sets this going and every other ending stops it.
  void note(AddSlotEnding ending, uint32_t nowMs);
  bool theSectorHoldsNoTable() const { return trying_; }
  int tries() const { return tries_; }
  // One pass of the caller's loop: at most one erase and one write a minute, and nothing at all
  // while the sector holds a table.
  KeeperStep step(TableFlash& flash, const unsigned char* oneSlotTable, uint32_t nowMs);
  // The one line - what is true now and what must not be done - or "" when there is nothing to say.
  const char* line() const { return line_; }

 private:
  bool trying_ = false;
  int tries_ = 0;
  uint32_t lastTryMs_ = 0;
  char line_[200] = {0};
};

}  // namespace mm
