// What the other application slot holds, written down so a restart knows (task p1.15-59,
// deliverable 3).
//
// A fetch judges an image and then the board goes on running the firmware it was running. The
// only thing left of the fetch is the bytes in the other slot - and bytes alone cannot say which
// release they are, so the version, the SHA-256, the size and the release's `base` are kept in the
// settings store under keys of this task's own. **Task 60 switches by this record**: without it a
// slot full of some image is a slot the firmware will not boot.
//
// Five keys, in the one NVS namespace the settings live in (src/settings/Store.h). They are not
// settings: nobody types them, `get` does not print them and `help` does not list them. `erase
// confirm` forgets them with everything else, which is right - a board whose settings are gone is
// a board that is about to be provisioned again, and the slot's contents are then nobody's.
//
//   fw.slot.state    `judged` when the slot holds an image that passed checks 1 to 5, else absent
//   fw.slot.ver      the release's version, as the manifest and the descriptor both say it
//   fw.slot.sha      the file's SHA-256, 64 lowercase hex characters
//   fw.slot.len      the file's size in bytes, in decimal
//   fw.slot.base     the release's base path, `/agent/v1/firmware/<id>`
//
// Nothing here is a secret and nothing here is overheard: a version, a hash, a length and a path
// the server published. The record is printed by `update` in full.
//
// Portable and host-tested (test/test_fetch): the store is behind RecordStore, which the board
// implements over NVS (src/update/FetchSeams.h) and a test implements with a map.

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace mm {
namespace update {

// The five keys, so that the board's store and a test's name the same things.
extern const char kRecordKeyState[];
extern const char kRecordKeyVersion[];
extern const char kRecordKeySha[];
extern const char kRecordKeyLength[];
extern const char kRecordKeyBase[];

// The one value `fw.slot.state` ever holds. Anything else is read as *nothing judged*, so a record
// half written by a power cut is not a record.
extern const char kRecordJudged[];

struct SlotRecord {
  bool judged = false;
  char version[33] = {0};
  char sha256hex[2 * 32 + 1] = {0};
  uint32_t size = 0;
  char base[65] = {0};
};

// Where the record lives. Keys are the five above; a value is text, NUL-terminated.
class RecordStore {
 public:
  virtual ~RecordStore() {}
  // The value of `key` into `out` (cap bytes): its length, or 0 and "" when it is not there.
  virtual size_t get(const char* key, char* out, size_t cap) = 0;
  // `value` under `key`; nullptr or "" forgets it. False when the store refused.
  virtual bool put(const char* key, const char* value) = 0;
};

// The record as the store holds it. A record missing any of its five parts, or holding a state
// that is not `judged`, reads as *nothing judged* - and then nothing else of it is believed.
SlotRecord loadRecord(RecordStore& store);

// The record, whole. `rec.judged == false` forgets every one of the five keys, which is what
// *the slot holds nothing judged* is written as. The state is written **last** on the way in and
// cleared **first** on the way out, so a power cut in the middle of either leaves *nothing
// judged* and never a record that half agrees with the slot.
bool saveRecord(RecordStore& store, const SlotRecord& rec);

// The line `update` prints for the other slot: *holds 0.8.0+1a2b3c4, 1446640 bytes, sha 4f2a…,
// fetched from /agent/v1/firmware/12*, or *holds nothing this firmware will boot*. One line,
// always, into `out` (cap bytes).
void renderRecordLine(const SlotRecord& rec, char* out, size_t cap);

}  // namespace update
}  // namespace mm
