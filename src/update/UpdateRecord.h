// The record of an update, and its journal: what outlives the restarts (task p1.15-60,
// deliverable 5).
//
// Task 59 wrote down what the *other slot* holds (SlotRecord.h). This is the record of the
// *update itself*: what ran before, what was switched to, when, and the steps so far. It has to
// outlive two restarts - the one into the new image and, when the trial fails, the one back - so
// it lives in the settings store beside task 59's record, under keys of this task's own, and
// **the boot after the trial, whichever image it is, is what finishes it**.
//
// Nine keys, in the one NVS namespace the settings live in (src/settings/Store.h). They are not
// settings: nobody types them, `get` does not print them and `help` does not list them. `erase
// confirm` forgets them with everything else, which is right - a board whose settings are gone is
// about to be provisioned again.
//
//   fw.up.state    `switching`, `trial` or `done`; absent means no update has ever been made
//   fw.up.from     the version that was running when the switch was made
//   fw.up.to       the version switched to
//   fw.up.sha      the SHA-256 of the image switched to, 64 lowercase hex characters (empty for
//                  an image no release named: one found in the first slot after a flash over USB,
//                  or whatever `update back` went back to)
//   fw.up.at       UNIX seconds of the switch, in decimal; 0 when the board had no clock
//   fw.up.slot     the application slot switched to, `0` or `1`
//   fw.up.end      `committed`, `rolledBack` or `stuck`, once the record is finished
//   fw.up.why      for a rollback: what was missing, in a sentence
//   fw.up.jr       the journal, one line an entry (see below)
//
// Nothing here is a secret and nothing here is overheard: versions, a hash, a slot number, a
// time and sentences this firmware wrote itself. `update` prints the whole record.
//
// **The journal is meshmaster.dk's `FirmwareJournalEntry.json`**: `at`, `step`, `note`, oldest
// first, at most 40 entries and 2 000 characters of step and note in all
// (`FirmwareJournalBounds`). Task 61 is what posts it; this task is what writes it. It is kept
// as the one text it is stored as - `<at>|<step>|<note>` a line - rather than as 40 structs,
// because 40 structs of a bounded step and note are four kilobytes of .bss on a board that has
// 60 % of its RAM in use, and the text is under three.
//
// Portable and host-tested (test/test_trial): the store is behind task 59's RecordStore, which
// the board implements over NVS (src/update/FetchSeams.h) and a test implements with a map.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "SlotRecord.h"

namespace mm {
namespace update {

// The nine keys, so that the board's store and a test's name the same things.
extern const char kUpdateKeyState[];
extern const char kUpdateKeyFrom[];
extern const char kUpdateKeyTo[];
extern const char kUpdateKeySha[];
extern const char kUpdateKeyAt[];
extern const char kUpdateKeySlot[];
extern const char kUpdateKeyEnd[];
extern const char kUpdateKeyWhy[];
extern const char kUpdateKeyJournal[];

// Where an update stands. Anything `fw.up.state` holds that is not one of the three words reads
// as *nothing in hand*, so a record half written by a power cut is not a record.
enum class Phase : uint8_t {
  None = 0,       // no update has ever been made, or the last one is forgotten
  Switching = 1,  // the boot choice has been set and the restart has not happened yet
  OnTrial = 2,    // the new image is running and has its ten minutes
  Ended = 3,      // `fw.up.end` says how it ended
};

// How an update ended, in meshmaster.dk's words (`FirmwareApplyOutcomes`). Task 61 is what turns
// a refusal into `refused`.
//
// **`rolledBack` is said only of a board that runs the old image again** (task p1.16-64). A trial
// that failed and could be sent back - by the bootloader, or by this firmware setting the boot
// choice back - is `rolledBack`; one that could not be sent back at all is `stuck`, which is the
// contract's word for *the board cannot say what happened*, and the record then carries the
// version that is running, which is the one that failed.
enum class Ended : uint8_t { None = 0, Committed = 1, RolledBack = 2, Stuck = 3 };

const char* phaseWord(Phase phase);
const char* endedWord(Ended ended);

// The journal's bounds, which are meshmaster.dk's `FirmwareJournalBounds` and not this
// firmware's to choose.
static const size_t kJournalMaxEntries = 40;
static const size_t kJournalMaxCharacters = 2000;

// What one entry may be. A step is one of the words the contract names - `switching`, `proving`,
// `committing`, `rolling-back`, `restored` - and a note is a phrase for a person.
static const size_t kMaxStepLen = 20;
static const size_t kMaxNoteLen = 96;

struct JournalEntry {
  uint32_t at = 0;  // UNIX seconds, 0 when the board had no clock at the step
  char step[kMaxStepLen + 1] = {0};
  char note[kMaxNoteLen + 1] = {0};
};

// The journal as the store holds it: `<at>|<step>|<note>`, one line an entry, oldest first.
// `|` and the line ends are the separators, so neither may appear in a step or a note and `add`
// turns either into a space.
class Journal {
 public:
  void clear() {
    text_[0] = '\0';
    len_ = 0;
  }

  // One entry, newest last. The **oldest** entries are dropped until the new one fits both
  // bounds: what an entry at the front says is also in the record's own `from`, `to` and `at`
  // fields, so the oldest line is the one line that is said twice - and the end of a journal is
  // what an outcome is read against. A note longer than kMaxNoteLen is cut.
  void add(uint32_t at, const char* step, const char* note);

  size_t count() const;
  // Entry `i`, 0 being the oldest. False past the end.
  bool entry(size_t i, JournalEntry& out) const;
  // The characters of step and note in all, which is the bound meshmaster.dk states.
  size_t characters() const;

  const char* text() const { return text_; }
  size_t length() const { return len_; }
  // The stored text, as it was read back. Anything that is not a line of the shape above is
  // dropped, so a truncated or corrupt value reads as the entries that are whole.
  void load(const char* text);
  // The same, read **straight out of the store into this object's own buffer**: a record is read
  // at every boot and the value is exactly as long as this buffer, so a second buffer for the
  // read would be two and a half kilobytes of .bss for nothing and the same on the stack would
  // be worse. The lines that are whole and inside the bounds are kept, in place.
  void readFrom(RecordStore& store, const char* key);

  bool empty() const { return len_ == 0; }

 private:
  void dropOldest();
  // Drop every line that is not of the shape above, and every one past the two bounds, moving
  // what is kept leftwards in the one buffer. Nothing is moved at all until something is dropped.
  void compact();

  // 40 entries of 10 digits and three separators, plus the 2 000 characters the bound allows.
  static constexpr size_t kCapacity = kJournalMaxEntries * 13 + kJournalMaxCharacters;
  char text_[kCapacity + 1] = {0};
  size_t len_ = 0;
};

struct UpdateRecord {
  Phase phase = Phase::None;
  char from[33] = {0};
  char to[33] = {0};
  char sha256hex[2 * 32 + 1] = {0};
  uint32_t at = 0;
  uint8_t slot = 0;
  Ended ended = Ended::None;
  char why[kMaxNoteLen + 1] = {0};
  Journal journal;

  bool inHand() const { return phase == Phase::Switching || phase == Phase::OnTrial; }
};

// The record as the store holds it. A state that is not one of the three words, or a `done` with
// no ending, reads as *nothing in hand* and then nothing else of it is believed.
UpdateRecord loadUpdate(RecordStore& store);

// The record, whole. `rec.phase == Phase::None` forgets every one of the nine keys. The state is
// written **last** on the way in and cleared **first** on the way out, so a power cut in the
// middle of either leaves a record that is not half believed - the same rule task 59's record
// is written by.
bool saveUpdate(RecordStore& store, const UpdateRecord& rec);

}  // namespace update
}  // namespace mm
