#include "UpdateRecord.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace mm {
namespace update {

const char kUpdateKeyState[] = "fw.up.state";
const char kUpdateKeyFrom[] = "fw.up.from";
const char kUpdateKeyTo[] = "fw.up.to";
const char kUpdateKeySha[] = "fw.up.sha";
const char kUpdateKeyAt[] = "fw.up.at";
const char kUpdateKeySlot[] = "fw.up.slot";
const char kUpdateKeyEnd[] = "fw.up.end";
const char kUpdateKeyWhy[] = "fw.up.why";
const char kUpdateKeyJournal[] = "fw.up.jr";

namespace {

const char kSwitching[] = "switching";
const char kTrial[] = "trial";
const char kDone[] = "done";
const char kCommitted[] = "committed";
const char kRolledBack[] = "rolledBack";
const char kStuck[] = "stuck";

// One field of a journal line into `out`, with `|`, CR and LF turned into spaces and the length
// bounded. Returns what was written.
size_t copyField(char* out, size_t cap, const char* from) {
  size_t n = 0;
  if (cap == 0) return 0;
  for (; from != nullptr && from[n] != '\0' && n + 1 < cap; n++) {
    const char c = from[n];
    out[n] = (c == '|' || c == '\r' || c == '\n') ? ' ' : c;
  }
  out[n] = '\0';
  return n;
}

// The line that begins at `from`, up to and including its newline; the length without it.
size_t lineLength(const char* from) {
  size_t n = 0;
  while (from[n] != '\0' && from[n] != '\n') n++;
  return n;
}

}  // namespace

const char* phaseWord(Phase phase) {
  switch (phase) {
    case Phase::Switching:
      return kSwitching;
    case Phase::OnTrial:
      return kTrial;
    case Phase::Ended:
      return kDone;
    case Phase::None:
      break;
  }
  return "";
}

const char* endedWord(Ended ended) {
  switch (ended) {
    case Ended::Committed:
      return kCommitted;
    case Ended::RolledBack:
      return kRolledBack;
    case Ended::Stuck:
      return kStuck;
    case Ended::None:
      break;
  }
  return "";
}

// -- the journal -------------------------------------------------------------------------------

size_t Journal::count() const {
  size_t n = 0;
  for (size_t i = 0; i < len_;) {
    i += lineLength(text_ + i);
    if (i < len_) i++;  // the newline
    n++;
  }
  return n;
}

size_t Journal::characters() const {
  size_t chars = 0;
  for (size_t i = 0; i < len_;) {
    const size_t line = lineLength(text_ + i);
    // The line is `<at>|<step>|<note>`: its characters of step and note are what is left when
    // the three fields' separators and the digits of `at` are taken off.
    const char* first = static_cast<const char*>(memchr(text_ + i, '|', line));
    if (first != nullptr) {
      const size_t digits = static_cast<size_t>(first - (text_ + i));
      chars += line - digits - 2;  // the two `|`
    }
    i += line;
    if (i < len_) i++;
  }
  return chars;
}

bool Journal::entry(size_t i, JournalEntry& out) const {
  size_t at = 0;
  for (size_t n = 0; at < len_; n++) {
    const size_t line = lineLength(text_ + at);
    if (n == i) {
      out = JournalEntry();
      const char* p = text_ + at;
      const char* first = static_cast<const char*>(memchr(p, '|', line));
      if (first == nullptr) return false;
      const size_t restLen = line - static_cast<size_t>(first + 1 - p);
      const char* second = static_cast<const char*>(memchr(first + 1, '|', restLen));
      if (second == nullptr) return false;
      char digits[12] = {0};
      const size_t dn = static_cast<size_t>(first - p) < sizeof(digits) - 1
                            ? static_cast<size_t>(first - p)
                            : sizeof(digits) - 1;
      memcpy(digits, p, dn);
      out.at = static_cast<uint32_t>(strtoul(digits, nullptr, 10));
      const size_t stepLen = static_cast<size_t>(second - (first + 1));
      const size_t sn = stepLen < sizeof(out.step) - 1 ? stepLen : sizeof(out.step) - 1;
      memcpy(out.step, first + 1, sn);
      const size_t noteLen = line - static_cast<size_t>(second + 1 - p);
      const size_t nn = noteLen < sizeof(out.note) - 1 ? noteLen : sizeof(out.note) - 1;
      memcpy(out.note, second + 1, nn);
      return true;
    }
    at += line;
    if (at < len_) at++;
  }
  return false;
}

void Journal::dropOldest() {
  if (len_ == 0) return;
  size_t cut = lineLength(text_);
  if (cut < len_) cut++;  // the newline goes with the line
  memmove(text_, text_ + cut, len_ - cut);
  len_ -= cut;
  text_[len_] = '\0';
}

void Journal::add(uint32_t at, const char* step, const char* note) {
  char stepText[kMaxStepLen + 1];
  char noteText[kMaxNoteLen + 1];
  const size_t stepLen = copyField(stepText, sizeof(stepText), step);
  const size_t noteLen = copyField(noteText, sizeof(noteText), note);
  if (stepLen == 0) return;  // an entry with no step is not an entry

  char line[12 + kMaxStepLen + kMaxNoteLen + 4];
  const int n = snprintf(line, sizeof(line), "%lu|%s|%s", static_cast<unsigned long>(at), stepText, noteText);
  if (n <= 0) return;
  const size_t want = static_cast<size_t>(n);

  // Room for this line, and for both bounds with it: the oldest go until there is.
  while (len_ != 0 && (count() + 1 > kJournalMaxEntries || characters() + stepLen + noteLen > kJournalMaxCharacters ||
                       len_ + 1 + want > kCapacity)) {
    dropOldest();
  }
  if (len_ + (len_ == 0 ? 0 : 1) + want > kCapacity) return;  // one line that cannot fit at all
  if (len_ != 0) text_[len_++] = '\n';
  memcpy(text_ + len_, line, want);
  len_ += want;
  text_[len_] = '\0';
}

void Journal::compact() {
  size_t out = 0;
  size_t in = 0;
  size_t kept = 0;
  size_t chars = 0;
  bool dropped = false;
  while (in < len_) {
    const size_t line = lineLength(text_ + in);
    size_t lineChars = 0;
    bool whole = false;
    const char* first = static_cast<const char*>(memchr(text_ + in, '|', line));
    if (first != nullptr) {
      const size_t restLen = line - static_cast<size_t>(first + 1 - (text_ + in));
      if (memchr(first + 1, '|', restLen) != nullptr) {
        whole = true;
        lineChars = line - static_cast<size_t>(first - (text_ + in)) - 2;
      }
    }
    const bool keep = whole && kept < kJournalMaxEntries && chars + lineChars <= kJournalMaxCharacters;
    if (keep) {
      if (!dropped) {
        out = in + line;  // nothing has moved yet, so this line is already where it belongs
      } else {
        // Something was dropped, so `out` is strictly behind `in` and the move is safe.
        if (out != 0) text_[out++] = '\n';
        memmove(text_ + out, text_ + in, line);
        out += line;
      }
      kept++;
      chars += lineChars;
    } else {
      dropped = true;
    }
    in += line;
    if (in < len_) in++;
  }
  len_ = out;
  text_[len_] = '\0';
}

void Journal::readFrom(RecordStore& store, const char* key) {
  clear();
  const size_t got = store.get(key, text_, sizeof(text_));
  len_ = got < kCapacity ? got : kCapacity;
  text_[len_] = '\0';
  compact();
}

void Journal::load(const char* text) {
  clear();
  if (text == nullptr) return;
  for (size_t i = 0; text[i] != '\0';) {
    const size_t line = lineLength(text + i);
    // Only a line of the shape `<at>|<step>|<note>` is kept: a value cut short by a store, or
    // one nothing here wrote, loses the lines that are not whole and keeps the ones that are.
    const char* first = static_cast<const char*>(memchr(text + i, '|', line));
    const char* second = nullptr;
    if (first != nullptr) {
      const size_t restLen = line - static_cast<size_t>(first + 1 - (text + i));
      second = static_cast<const char*>(memchr(first + 1, '|', restLen));
    }
    if (second != nullptr && line != 0) {
      char digits[12] = {0};
      const size_t dn = static_cast<size_t>(first - (text + i)) < sizeof(digits) - 1
                            ? static_cast<size_t>(first - (text + i))
                            : sizeof(digits) - 1;
      memcpy(digits, text + i, dn);
      char step[kMaxStepLen + 1] = {0};
      char note[kMaxNoteLen + 1] = {0};
      const size_t stepLen = static_cast<size_t>(second - (first + 1));
      memcpy(step, first + 1, stepLen < sizeof(step) - 1 ? stepLen : sizeof(step) - 1);
      const size_t noteLen = line - static_cast<size_t>(second + 1 - (text + i));
      memcpy(note, second + 1, noteLen < sizeof(note) - 1 ? noteLen : sizeof(note) - 1);
      add(static_cast<uint32_t>(strtoul(digits, nullptr, 10)), step, note);
    }
    i += line;
    if (text[i] == '\n') i++;
  }
}

// -- the record --------------------------------------------------------------------------------

namespace {

// `key` into `out` (cap bytes), and whether anything was there.
bool readKey(RecordStore& store, const char* key, char* out, size_t cap) {
  out[0] = '\0';
  return store.get(key, out, cap) > 0;
}

}  // namespace

void UpdateRecord::clear() {
  phase = Phase::None;
  from[0] = '\0';
  to[0] = '\0';
  sha256hex[0] = '\0';
  at = 0;
  slot = 0;
  ended = Ended::None;
  why[0] = '\0';
  journal.clear();
}

void loadUpdate(RecordStore& store, UpdateRecord& rec) {
  // The record that is given is emptied first and filled in place: nothing of this size is built
  // on the stack, and a give-up below leaves it empty whatever it held when it came in.
  rec.clear();
  char state[16] = {0};
  if (!readKey(store, kUpdateKeyState, state, sizeof(state))) return;
  if (strcmp(state, kSwitching) == 0) {
    rec.phase = Phase::Switching;
  } else if (strcmp(state, kTrial) == 0) {
    rec.phase = Phase::OnTrial;
  } else if (strcmp(state, kDone) == 0) {
    rec.phase = Phase::Ended;
  } else {
    rec.clear();  // not a state this firmware wrote
    return;
  }

  readKey(store, kUpdateKeyFrom, rec.from, sizeof(rec.from));
  readKey(store, kUpdateKeyTo, rec.to, sizeof(rec.to));
  readKey(store, kUpdateKeySha, rec.sha256hex, sizeof(rec.sha256hex));
  char at[16] = {0};
  if (readKey(store, kUpdateKeyAt, at, sizeof(at))) rec.at = static_cast<uint32_t>(strtoul(at, nullptr, 10));
  char slot[8] = {0};
  if (readKey(store, kUpdateKeySlot, slot, sizeof(slot))) rec.slot = slot[0] == '1' ? 1 : 0;
  char end[16] = {0};
  if (readKey(store, kUpdateKeyEnd, end, sizeof(end))) {
    if (strcmp(end, kCommitted) == 0) rec.ended = Ended::Committed;
    if (strcmp(end, kRolledBack) == 0) rec.ended = Ended::RolledBack;
    if (strcmp(end, kStuck) == 0) rec.ended = Ended::Stuck;
  }
  readKey(store, kUpdateKeyWhy, rec.why, sizeof(rec.why));
  // Straight into the journal's own buffer: no second one anywhere (Journal::readFrom).
  rec.journal.readFrom(store, kUpdateKeyJournal);

  // A `done` with no ending is not an ending, and neither phase nor ending alone is a record:
  // the one state a power cut can leave half written is not believed (the same rule as task
  // 59's record).
  if (rec.phase == Phase::Ended && rec.ended == Ended::None) {
    rec.clear();
    return;
  }
  if (rec.inHand() && rec.to[0] == '\0') rec.clear();
}

bool saveUpdate(RecordStore& store, const UpdateRecord& rec) {
  if (rec.phase == Phase::None) {
    // The state goes first: a power cut after it leaves no record at all rather than one that
    // half agrees with the slots.
    bool ok = store.put(kUpdateKeyState, nullptr);
    ok = store.put(kUpdateKeyFrom, nullptr) && ok;
    ok = store.put(kUpdateKeyTo, nullptr) && ok;
    ok = store.put(kUpdateKeySha, nullptr) && ok;
    ok = store.put(kUpdateKeyAt, nullptr) && ok;
    ok = store.put(kUpdateKeySlot, nullptr) && ok;
    ok = store.put(kUpdateKeyEnd, nullptr) && ok;
    ok = store.put(kUpdateKeyWhy, nullptr) && ok;
    ok = store.put(kUpdateKeyJournal, nullptr) && ok;
    return ok;
  }

  char at[16];
  snprintf(at, sizeof(at), "%lu", static_cast<unsigned long>(rec.at));
  bool ok = store.put(kUpdateKeyFrom, rec.from);
  ok = store.put(kUpdateKeyTo, rec.to) && ok;
  ok = store.put(kUpdateKeySha, rec.sha256hex) && ok;
  ok = store.put(kUpdateKeyAt, at) && ok;
  ok = store.put(kUpdateKeySlot, rec.slot == 1 ? "1" : "0") && ok;
  ok = store.put(kUpdateKeyEnd, endedWord(rec.ended)) && ok;
  ok = store.put(kUpdateKeyWhy, rec.why) && ok;
  ok = store.put(kUpdateKeyJournal, rec.journal.text()) && ok;
  // And the state last, so that everything it refers to is already there.
  ok = store.put(kUpdateKeyState, phaseWord(rec.phase)) && ok;
  return ok;
}

}  // namespace update
}  // namespace mm
