// The partition tables this firmware carries as bytes, and which of them a board build compiles.
//
// Two per board (src/platform/PartitionTable.h for the three words):
//
//   ...OneSlot    what every merged image from v0.2.0 to v0.7.1 wrote, and what a board in the
//                 field still holds. The firmware writes over that sector and over no other.
//   ...TwoSlots   the table with the second application slot - the one this build's own merged
//                 image carries at 0x8000, so a board told `update slot` and a board flashed over
//                 USB end up with the same table.
//
// The arrays themselves are generated into TableBytes.cpp from boards/partitions/*.csv by
// tools/check-table-bytes.py, with the ESP-IDF's own gen_esp32part.py, and CI holds them against
// those files and against the merged image on every build of every board (task p1.15-57).

#pragma once

#include "PartitionTable.h"

// A board build carries its own board's two tables and no more - 0xC00 of flash each. The host
// build has no board, so it carries all four: test/test_partition_table reads both boards'.
#if defined(HELTEC_LORA_V3)
#define MM_TABLES_8MB 1
#elif defined(HELTEC_LORA_V4)
#define MM_TABLES_16MB 1
#else
#define MM_TABLES_EVERY_BOARD 1
#endif

namespace mm {

#if defined(MM_TABLES_8MB) || defined(MM_TABLES_EVERY_BOARD)
extern const unsigned char kTable8MBOneSlot[kTableBytesLen];
extern const unsigned char kTable8MBTwoSlots[kTableBytesLen];
#endif
#if defined(MM_TABLES_16MB) || defined(MM_TABLES_EVERY_BOARD)
extern const unsigned char kTable16MBOneSlot[kTableBytesLen];
extern const unsigned char kTable16MBTwoSlots[kTableBytesLen];
#endif

struct TablePair {
  const unsigned char* oneSlot;
  const unsigned char* twoSlots;
};

// This board's pair. A board whose flash size this firmware has no table for has none, and the
// console's `update` will not compile for it - which is the right way to find out.
#if defined(MM_TABLES_8MB)
inline TablePair tablesForThisBoard() { return {kTable8MBOneSlot, kTable8MBTwoSlots}; }
#elif defined(MM_TABLES_16MB)
inline TablePair tablesForThisBoard() { return {kTable16MBOneSlot, kTable16MBTwoSlots}; }
#endif

}  // namespace mm
