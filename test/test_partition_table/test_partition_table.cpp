// Which partition table a board runs on, and adding the second application slot to it
// (task p1.15-57, src/platform/PartitionTable.h).
//
// No test here can write a board's table, so the flash is scripted: a 4 kB sector this suite
// fills, and a TableFlash over it that counts every read, erase and write and can be told to fail
// any of them or to read back a byte other than the one written. What is held:
//
//   the word    `one` for the table boards in the field hold - this board's, byte for byte -
//               `two` for the new table and for the framework's own default (which has an app1
//               wherever it puts it), and `other` for everything else, a single changed byte
//               included. The other board's old table is `other` too: a V3 does not write a V4's.
//   the write   on `one`: one erase and one write of that sector and no other call that changes
//               it, and the sector afterwards is the new table byte for byte with 0xFF above it.
//               On `two` and on `other`: nothing erased, nothing written.
//   the honesty a sector that reads back one byte other than what was written is reported with
//               the byte and the offset, and the function does not say *added*.
//   the files   the two csv files of a board differ by the app1 line and by nothing else, and the
//               slot the firmware's own bytes carry is at the offset and size the task names.
//
// The tables themselves are the committed arrays of src/platform/TableBytes.cpp, which
// tools/check-table-bytes.py holds against boards/partitions/*.csv and against the merged image
// in CI - so what this suite drives is the bytes a board really gets.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "platform/TableBytes.h"

#ifndef PROJECT_DIR
#error "PROJECT_DIR must be defined; platformio.ini's [env:native] does it"
#endif

namespace {

namespace fs = std::filesystem;

using mm::kTableBytesLen;
using mm::kTableEntryLen;
using mm::kTableSectorLen;
using mm::TableKind;

// A 4 kB sector and nothing else. Every call is counted; `corruptAt` is a flash that takes a
// write and does not keep it.
class ScriptedSector final : public mm::TableFlash {
 public:
  unsigned char sector[kTableSectorLen];
  int reads = 0;
  int erases = 0;
  int writes = 0;
  bool readFails = false;
  bool eraseFails = false;
  bool writeFails = false;
  long corruptAt = -1;

  ScriptedSector() { std::memset(sector, 0xFF, sizeof sector); }

  // The sector as a merged image leaves it: the table, then 0xFF to the end.
  void hold(const unsigned char* table) {
    std::memcpy(sector, table, kTableBytesLen);
    std::memset(sector + kTableBytesLen, 0xFF, kTableSectorLen - kTableBytesLen);
  }

  bool read(size_t at, unsigned char* into, size_t len) override {
    reads++;
    if (readFails || at + len > kTableSectorLen) return false;
    std::memcpy(into, sector + at, len);
    return true;
  }

  bool eraseSector() override {
    erases++;
    if (eraseFails) return false;
    std::memset(sector, 0xFF, sizeof sector);
    return true;
  }

  bool write(size_t at, const unsigned char* from, size_t len) override {
    writes++;
    if (writeFails || at + len > kTableSectorLen) return false;
    std::memcpy(sector + at, from, len);
    if (corruptAt >= 0 && static_cast<size_t>(corruptAt) < kTableSectorLen) sector[corruptAt] ^= 0x01;
    return true;
  }
};

// A table built here, entry by entry, as gen_esp32part.py lays them out - used for the
// framework's own default, which this repository does not ship as a file.
void addEntry(std::vector<unsigned char>& t, const char* name, unsigned char type, unsigned char subtype, uint32_t offset,
              uint32_t size) {
  const size_t at = t.size();
  t.resize(at + kTableEntryLen, 0);
  t[at] = 0xAA;
  t[at + 1] = 0x50;
  t[at + 2] = type;
  t[at + 3] = subtype;
  for (int i = 0; i < 4; i++) t[at + 4 + static_cast<size_t>(i)] = static_cast<unsigned char>((offset >> (8 * i)) & 0xFF);
  for (int i = 0; i < 4; i++) t[at + 8 + static_cast<size_t>(i)] = static_cast<unsigned char>((size >> (8 * i)) & 0xFF);
  std::memcpy(&t[at + 12], name, std::min<size_t>(std::strlen(name), 16));
}

// The framework's default_8MB.csv, which a board provisioned under v0.1.1 or earlier still holds:
// nvs at 0x9000, and an app1 of its own. tools/check-partitions.py carries the same numbers.
std::vector<unsigned char> frameworkDefault8MB() {
  std::vector<unsigned char> t;
  addEntry(t, "nvs", 0x01, 0x02, 0x9000, 0x5000);
  addEntry(t, "otadata", 0x01, 0x00, 0xE000, 0x2000);
  addEntry(t, "app0", 0x00, 0x10, 0x10000, 0x330000);
  addEntry(t, "app1", 0x00, 0x11, 0x340000, 0x330000);
  addEntry(t, "spiffs", 0x01, 0x82, 0x670000, 0x180000);
  addEntry(t, "coredump", 0x01, 0x03, 0x7F0000, 0x10000);
  t.resize(kTableBytesLen, 0xFF);
  return t;
}

TableKind wordFor(const unsigned char* held, const unsigned char* oneSlot) {
  ScriptedSector flash;
  flash.hold(held);
  return mm::readTable(flash, oneSlot).kind;
}

// The data lines of a csv: comments and blank lines dropped, whitespace squeezed, so that two
// files can be compared by what they say rather than by how they are laid out.
std::vector<std::string> dataLines(const fs::path& csv) {
  std::ifstream in(csv);
  EXPECT_TRUE(in.is_open()) << csv.generic_string();
  std::vector<std::string> out;
  std::string line;
  while (std::getline(in, line)) {
    const size_t hash = line.find('#');
    if (hash != std::string::npos) line.erase(hash);
    std::string squeezed;
    for (char c : line) {
      if (c != ' ' && c != '\t' && c != '\r') squeezed.push_back(c);
    }
    if (!squeezed.empty()) out.push_back(squeezed);
  }
  return out;
}

}  // namespace

// ---- the word ---------------------------------------------------------------------------------

TEST(TheWord, TheTableBoardsHoldTodayIsOne) {
  EXPECT_EQ(TableKind::One, wordFor(mm::kTable8MBOneSlot, mm::kTable8MBOneSlot));
  EXPECT_EQ(TableKind::One, wordFor(mm::kTable16MBOneSlot, mm::kTable16MBOneSlot));
}

TEST(TheWord, TheNewTableIsTwo) {
  EXPECT_EQ(TableKind::Two, wordFor(mm::kTable8MBTwoSlots, mm::kTable8MBOneSlot));
  EXPECT_EQ(TableKind::Two, wordFor(mm::kTable16MBTwoSlots, mm::kTable16MBOneSlot));
}

TEST(TheWord, TheFrameworksDefaultIsTwo) {
  // It has a second application slot - somewhere else, but it has one - so there is nothing for
  // this firmware to add. A board on it is updated like any other (phase 1.15's FOLLOWUPS).
  EXPECT_EQ(TableKind::Two, wordFor(frameworkDefault8MB().data(), mm::kTable8MBOneSlot));
}

TEST(TheWord, OneByteChangedIsOther) {
  for (size_t at : {size_t{0}, size_t{0x20}, kTableBytesLen - 1}) {
    std::vector<unsigned char> changed(mm::kTable8MBOneSlot, mm::kTable8MBOneSlot + kTableBytesLen);
    changed[at] ^= 0x01;
    EXPECT_EQ(TableKind::Other, wordFor(changed.data(), mm::kTable8MBOneSlot)) << "at 0x" << std::hex << at;
  }
}

TEST(TheWord, AByteAboveTheEntriesIsOtherToo) {
  // The 0x400 above the table is 0xFF on every board a merged image or an erase ever touched.
  ScriptedSector flash;
  flash.hold(mm::kTable8MBOneSlot);
  flash.sector[kTableSectorLen - 1] = 0x00;
  EXPECT_EQ(TableKind::Other, mm::readTable(flash, mm::kTable8MBOneSlot).kind);
}

TEST(TheWord, TheOtherBoardsOldTableIsOther) {
  EXPECT_EQ(TableKind::Other, wordFor(mm::kTable16MBOneSlot, mm::kTable8MBOneSlot));
  EXPECT_EQ(TableKind::Other, wordFor(mm::kTable8MBOneSlot, mm::kTable16MBOneSlot));
}

TEST(TheWord, AnErasedSectorIsOther) {
  ScriptedSector flash;  // all 0xFF
  const mm::TableReading r = mm::readTable(flash, mm::kTable8MBOneSlot);
  EXPECT_TRUE(r.read);
  EXPECT_EQ(TableKind::Other, r.kind);
  EXPECT_FALSE(r.app0.present);
}

TEST(TheWord, AFlashThatCannotBeReadClaimsNothing) {
  ScriptedSector flash;
  flash.hold(mm::kTable8MBTwoSlots);
  flash.readFails = true;
  const mm::TableReading r = mm::readTable(flash, mm::kTable8MBOneSlot);
  EXPECT_FALSE(r.read);
  EXPECT_EQ(TableKind::Other, r.kind);
}

// ---- the slots the word carries ----------------------------------------------------------------

TEST(TheSlots, TheNewTableCarriesTheSlotTheTaskNames) {
  ScriptedSector v3;
  v3.hold(mm::kTable8MBTwoSlots);
  const mm::TableReading a = mm::readTable(v3, mm::kTable8MBOneSlot);
  EXPECT_TRUE(a.app0.present);
  EXPECT_EQ(0x10000u, a.app0.offset);
  EXPECT_EQ(0x330000u, a.app0.size);
  EXPECT_TRUE(a.app1.present);
  EXPECT_EQ(0x350000u, a.app1.offset);
  EXPECT_EQ(0x320000u, a.app1.size);

  ScriptedSector v4;
  v4.hold(mm::kTable16MBTwoSlots);
  const mm::TableReading b = mm::readTable(v4, mm::kTable16MBOneSlot);
  EXPECT_EQ(0x10000u, b.app0.offset);
  EXPECT_EQ(0x640000u, b.app0.size);
  EXPECT_TRUE(b.app1.present);
  EXPECT_EQ(0x660000u, b.app1.offset);
  EXPECT_EQ(0x630000u, b.app1.size);
}

TEST(TheSlots, TheTableBoardsHoldTodayHasOneSlotAndTheWordSaysSo) {
  ScriptedSector flash;
  flash.hold(mm::kTable8MBOneSlot);
  const mm::TableReading r = mm::readTable(flash, mm::kTable8MBOneSlot);
  EXPECT_EQ(TableKind::One, r.kind);
  EXPECT_TRUE(r.app0.present);
  EXPECT_FALSE(r.app1.present);
}

// ---- adding the slot ----------------------------------------------------------------------------

TEST(AddingTheSlot, OnOneTheSectorBecomesTheNewTableWithOneEraseAndOneWrite) {
  ScriptedSector flash;
  flash.hold(mm::kTable8MBOneSlot);
  const mm::AddSlotResult r = mm::addSecondSlot(flash, mm::kTable8MBOneSlot, mm::kTable8MBTwoSlots);

  EXPECT_TRUE(r.added) << r.line;
  EXPECT_NE(std::string(r.line).find("added"), std::string::npos) << r.line;
  EXPECT_NE(std::string(r.line).find("0x350000"), std::string::npos) << r.line;
  EXPECT_NE(std::string(r.line).find("reboot"), std::string::npos) << r.line;

  EXPECT_EQ(1, flash.erases);
  EXPECT_EQ(1, flash.writes);
  EXPECT_EQ(0, std::memcmp(flash.sector, mm::kTable8MBTwoSlots, kTableBytesLen)) << "the entries";
  for (size_t at = kTableBytesLen; at < kTableSectorLen; at++) {
    ASSERT_EQ(0xFF, flash.sector[at]) << "0x" << std::hex << at << " above the entries";
  }
  EXPECT_EQ(TableKind::Two, mm::readTable(flash, mm::kTable8MBOneSlot).kind) << "and it reads as two now";
}

TEST(AddingTheSlot, OnTheOtherBoardToo) {
  ScriptedSector flash;
  flash.hold(mm::kTable16MBOneSlot);
  const mm::AddSlotResult r = mm::addSecondSlot(flash, mm::kTable16MBOneSlot, mm::kTable16MBTwoSlots);
  EXPECT_TRUE(r.added) << r.line;
  EXPECT_NE(std::string(r.line).find("0x660000"), std::string::npos) << r.line;
  EXPECT_EQ(1, flash.erases);
  EXPECT_EQ(1, flash.writes);
  EXPECT_EQ(0, std::memcmp(flash.sector, mm::kTable16MBTwoSlots, kTableBytesLen));
}

TEST(AddingTheSlot, OnTwoNothingIsErasedOrWritten) {
  ScriptedSector flash;
  flash.hold(mm::kTable8MBTwoSlots);
  const mm::AddSlotResult r = mm::addSecondSlot(flash, mm::kTable8MBOneSlot, mm::kTable8MBTwoSlots);
  EXPECT_FALSE(r.added);
  EXPECT_NE(std::string(r.line).find("already there"), std::string::npos) << r.line;
  EXPECT_NE(std::string(r.line).find("0x350000"), std::string::npos) << r.line;
  EXPECT_EQ(0, flash.erases);
  EXPECT_EQ(0, flash.writes);
  EXPECT_EQ(0, std::memcmp(flash.sector, mm::kTable8MBTwoSlots, kTableBytesLen)) << "untouched";
}

TEST(AddingTheSlot, OnOtherItRefusesInASentenceAndWritesNothing) {
  std::vector<unsigned char> changed(mm::kTable8MBOneSlot, mm::kTable8MBOneSlot + kTableBytesLen);
  changed[0x48] ^= 0x01;  // one byte of an entry's offset
  ScriptedSector flash;
  flash.hold(changed.data());
  const mm::AddSlotResult r = mm::addSecondSlot(flash, mm::kTable8MBOneSlot, mm::kTable8MBTwoSlots);
  EXPECT_FALSE(r.added);
  EXPECT_NE(std::string(r.line).find("not one this firmware knows"), std::string::npos) << r.line;
  EXPECT_EQ(0, flash.erases);
  EXPECT_EQ(0, flash.writes);
  EXPECT_EQ(0, std::memcmp(flash.sector, changed.data(), kTableBytesLen)) << "untouched";
}

TEST(AddingTheSlot, AnErasedSectorIsNotWrittenEither) {
  ScriptedSector flash;  // all 0xFF: a board caught between the erase and the write
  const mm::AddSlotResult r = mm::addSecondSlot(flash, mm::kTable8MBOneSlot, mm::kTable8MBTwoSlots);
  EXPECT_FALSE(r.added);
  EXPECT_EQ(0, flash.erases);
  EXPECT_EQ(0, flash.writes);
}

TEST(AddingTheSlot, AReadbackThatDiffersByOneByteSaysWhatItReadAndDoesNotSayAdded) {
  ScriptedSector flash;
  flash.hold(mm::kTable8MBOneSlot);
  flash.corruptAt = 0x44;  // inside an entry, so the byte is not 0xFF either way
  const unsigned char want = mm::kTable8MBTwoSlots[0x44];

  const mm::AddSlotResult r = mm::addSecondSlot(flash, mm::kTable8MBOneSlot, mm::kTable8MBTwoSlots);
  EXPECT_FALSE(r.added);
  const std::string line(r.line);
  EXPECT_EQ(line.find("added:"), std::string::npos) << line;
  EXPECT_NE(line.find("NOT added"), std::string::npos) << line;
  EXPECT_NE(line.find("0x044"), std::string::npos) << "the offset it read: " << line;

  char read_byte[8];
  char want_byte[8];
  std::snprintf(read_byte, sizeof read_byte, "0x%02X", static_cast<unsigned>(want ^ 0x01));
  std::snprintf(want_byte, sizeof want_byte, "0x%02X", static_cast<unsigned>(want));
  EXPECT_NE(line.find(read_byte), std::string::npos) << "the byte it read: " << line;
  EXPECT_NE(line.find(want_byte), std::string::npos) << "the byte it wanted: " << line;
}

TEST(AddingTheSlot, AnEraseOrAWriteThatFailsSaysSoAndSaysWhatToDo) {
  {
    ScriptedSector flash;
    flash.hold(mm::kTable8MBOneSlot);
    flash.eraseFails = true;
    const mm::AddSlotResult r = mm::addSecondSlot(flash, mm::kTable8MBOneSlot, mm::kTable8MBTwoSlots);
    EXPECT_FALSE(r.added);
    EXPECT_NE(std::string(r.line).find("could not be erased"), std::string::npos) << r.line;
    EXPECT_NE(std::string(r.line).find("merged image"), std::string::npos) << r.line;
    EXPECT_EQ(0, flash.writes);
  }
  {
    ScriptedSector flash;
    flash.hold(mm::kTable8MBOneSlot);
    flash.writeFails = true;
    const mm::AddSlotResult r = mm::addSecondSlot(flash, mm::kTable8MBOneSlot, mm::kTable8MBTwoSlots);
    EXPECT_FALSE(r.added);
    EXPECT_NE(std::string(r.line).find("could not be written"), std::string::npos) << r.line;
    EXPECT_EQ(1, flash.erases);
    EXPECT_EQ(1, flash.writes);
  }
}

TEST(AddingTheSlot, TheLineIsAlwaysOneLine) {
  ScriptedSector flash;
  flash.hold(mm::kTable8MBOneSlot);
  const mm::AddSlotResult r = mm::addSecondSlot(flash, mm::kTable8MBOneSlot, mm::kTable8MBTwoSlots);
  const std::string line(r.line);
  EXPECT_FALSE(line.empty());
  EXPECT_EQ(line.find('\n'), std::string::npos) << line;
  EXPECT_EQ(line.find('\r'), std::string::npos) << line;
}

TEST(TheWords, AreTheThreeTheDocumentationSays) {
  EXPECT_STREQ("one", mm::tableKindWord(TableKind::One));
  EXPECT_STREQ("two", mm::tableKindWord(TableKind::Two));
  EXPECT_STREQ("other", mm::tableKindWord(TableKind::Other));
}

// ---- the files behind the bytes -----------------------------------------------------------------

TEST(TheFiles, TheOldTableDiffersFromTheNewByTheApp1LineAndByNothingElse) {
  const fs::path tables = fs::path(PROJECT_DIR) / "boards" / "partitions";
  for (const char* pair : {"meshmaster_8MB", "meshmaster_16MB"}) {
    const std::vector<std::string> old_lines = dataLines(tables / (std::string(pair) + "_one_slot.csv"));
    const std::vector<std::string> new_lines = dataLines(tables / (std::string(pair) + ".csv"));
    ASSERT_FALSE(old_lines.empty()) << pair;
    ASSERT_EQ(old_lines.size() + 1, new_lines.size()) << pair << ": one line more, and one only";

    std::vector<std::string> without_app1;
    int app1_lines = 0;
    for (const std::string& l : new_lines) {
      if (l.rfind("app1,", 0) == 0) {
        app1_lines++;
        continue;
      }
      without_app1.push_back(l);
    }
    EXPECT_EQ(1, app1_lines) << pair;
    EXPECT_EQ(old_lines, without_app1) << pair << ": every other line is the same, in the same order";
  }
}

TEST(TheFiles, TheBuiltTableIsTheNewOneAndTheOldOneIsNotBuiltWith) {
  // platformio.ini names the table with two slots for each board; the one-slot files are records
  // of what is on a board, not something to build with.
  std::ifstream in(fs::path(PROJECT_DIR) / "platformio.ini");
  ASSERT_TRUE(in.is_open());
  const std::string ini((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  EXPECT_NE(ini.find("board_build.partitions = boards/partitions/meshmaster_8MB.csv"), std::string::npos);
  EXPECT_NE(ini.find("board_build.partitions = boards/partitions/meshmaster_16MB.csv"), std::string::npos);
  EXPECT_EQ(ini.find("board_build.partitions = boards/partitions/meshmaster_8MB_one_slot.csv"), std::string::npos);
  EXPECT_EQ(ini.find("board_build.partitions = boards/partitions/meshmaster_16MB_one_slot.csv"), std::string::npos);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
