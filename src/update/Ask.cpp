#include "Ask.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <agent/Iso8601.h>
#include <agent/TextOut.h>

namespace mm {
namespace update {

const char kStepCheck[] = "check";
const char kStepAddingSlot[] = "adding-slot";
const char kStepFetching[] = "fetching";

const char kAskKeyStep[] = "fw.ask.step";
const char kAskKeyId[] = "fw.ask.id";
const char kAskKeyRelease[] = "fw.ask.rel";
const char kAskKeyVersion[] = "fw.ask.ver";
const char kAskKeyBase[] = "fw.ask.base";
const char kAskKeySha[] = "fw.ask.sha";
const char kAskKeyLength[] = "fw.ask.len";
const char kAskKeyFrom[] = "fw.ask.from";
const char kAskKeyTo[] = "fw.ask.to";
const char kAskKeyOutcome[] = "fw.ask.out";
const char kAskKeyRefusal[] = "fw.ask.ref";
const char kAskKeyCheck[] = "fw.ask.chk";
const char kAskKeyPosted[] = "fw.ask.post";
const char kAskKeyJournal[] = "fw.ask.jr";

namespace {

const char kDone[] = "done";
const char kWaiting[] = "waiting";
const char kTaken[] = "taken";
const char kGone[] = "gone";

// The two table words this firmware will act on. The third, `other`, is the refusal.
const char kTableOne[] = "one";
const char kTableTwo[] = "two";

bool readKey(RecordStore& store, const char* key, char* out, size_t cap) {
  out[0] = '\0';
  return store.get(key, out, cap) > 0;
}

}  // namespace

const char* askStepWord(AskStep step) {
  switch (step) {
    case AskStep::Check:
      return kStepCheck;
    case AskStep::AddingSlot:
      return kStepAddingSlot;
    case AskStep::Fetching:
      return kStepFetching;
    case AskStep::Switching:
      return kStepSwitching;
    case AskStep::Proving:
      return kStepProving;
    case AskStep::Ended:
      return kDone;
    case AskStep::None:
      break;
  }
  return "";
}

const char* askOutcomeWord(AskOutcome outcome) {
  switch (outcome) {
    case AskOutcome::Committed:
      return "committed";
    case AskOutcome::RolledBack:
      return "rolledBack";
    case AskOutcome::Refused:
      return "refused";
    case AskOutcome::Stuck:
      return "stuck";
    case AskOutcome::None:
      break;
  }
  return "";
}

const char* askRefusalWord(AskRefusal refusal) {
  switch (refusal) {
    case AskRefusal::InProgress:
      return "inProgress";
    case AskRefusal::Table:
      return "table";
    case AskRefusal::Current:
      return "current";
    case AskRefusal::Fetch:
      return "fetch";
    case AskRefusal::Check:
      return "check";
    case AskRefusal::None:
      break;
  }
  return "";
}

const char* askPostedWord(AskPosted posted) {
  switch (posted) {
    case AskPosted::Waiting:
      return kWaiting;
    case AskPosted::Taken:
      return kTaken;
    case AskPosted::Gone:
      return kGone;
    case AskPosted::None:
      break;
  }
  return "";
}

bool sameVersion(const char* a, const char* b) {
  if (a == nullptr || b == nullptr || a[0] == '\0' || b[0] == '\0') return false;
  const char* plusA = strchr(a, '+');
  const char* plusB = strchr(b, '+');
  const size_t lenA = plusA != nullptr ? static_cast<size_t>(plusA - a) : strlen(a);
  const size_t lenB = plusB != nullptr ? static_cast<size_t>(plusB - b) : strlen(b);
  if (lenA != lenB || strncmp(a, b, lenA) != 0) return false;
  // The build metadata only counts when both sides carry it: the server's catalogue may hold
  // `0.8.0` for the release whose image says `0.8.0+1a2b3c4`, and those are one release.
  if (plusA != nullptr && plusB != nullptr) return strcmp(plusA, plusB) == 0;
  return true;
}

// -- the record --------------------------------------------------------------------------------

void AskRecord::clear() {
  step = AskStep::None;
  id = 0;
  releaseId = 0;
  version[0] = '\0';
  base[0] = '\0';
  sha256hex[0] = '\0';
  size = 0;
  from[0] = '\0';
  to[0] = '\0';
  outcome = AskOutcome::None;
  refusal = AskRefusal::None;
  check = 0;
  posted = AskPosted::None;
  journal.clear();
}

void loadAsk(RecordStore& store, AskRecord& rec) {
  // The record that is given is emptied first and filled in place: nothing of this size is built
  // on the stack, and a give-up below leaves it empty whatever it held when it came in.
  rec.clear();
  char step[16] = {0};
  if (!readKey(store, kAskKeyStep, step, sizeof(step))) return;
  if (strcmp(step, kStepCheck) == 0) {
    rec.step = AskStep::Check;
  } else if (strcmp(step, kStepAddingSlot) == 0) {
    rec.step = AskStep::AddingSlot;
  } else if (strcmp(step, kStepFetching) == 0) {
    rec.step = AskStep::Fetching;
  } else if (strcmp(step, kStepSwitching) == 0) {
    rec.step = AskStep::Switching;
  } else if (strcmp(step, kStepProving) == 0) {
    rec.step = AskStep::Proving;
  } else if (strcmp(step, kDone) == 0) {
    rec.step = AskStep::Ended;
  } else {
    rec.clear();  // not a step word this firmware wrote
    return;
  }

  char number[24] = {0};
  if (readKey(store, kAskKeyId, number, sizeof(number))) rec.id = strtoll(number, nullptr, 10);
  if (readKey(store, kAskKeyRelease, number, sizeof(number))) rec.releaseId = strtoll(number, nullptr, 10);
  if (readKey(store, kAskKeyLength, number, sizeof(number))) {
    rec.size = static_cast<uint32_t>(strtoul(number, nullptr, 10));
  }
  readKey(store, kAskKeyVersion, rec.version, sizeof(rec.version));
  readKey(store, kAskKeyBase, rec.base, sizeof(rec.base));
  readKey(store, kAskKeySha, rec.sha256hex, sizeof(rec.sha256hex));
  readKey(store, kAskKeyFrom, rec.from, sizeof(rec.from));
  readKey(store, kAskKeyTo, rec.to, sizeof(rec.to));
  char word[16] = {0};
  if (readKey(store, kAskKeyOutcome, word, sizeof(word))) {
    if (strcmp(word, "committed") == 0) rec.outcome = AskOutcome::Committed;
    if (strcmp(word, "rolledBack") == 0) rec.outcome = AskOutcome::RolledBack;
    if (strcmp(word, "refused") == 0) rec.outcome = AskOutcome::Refused;
    if (strcmp(word, "stuck") == 0) rec.outcome = AskOutcome::Stuck;
  }
  if (readKey(store, kAskKeyRefusal, word, sizeof(word))) {
    if (strcmp(word, "inProgress") == 0) rec.refusal = AskRefusal::InProgress;
    if (strcmp(word, "table") == 0) rec.refusal = AskRefusal::Table;
    if (strcmp(word, "current") == 0) rec.refusal = AskRefusal::Current;
    if (strcmp(word, "fetch") == 0) rec.refusal = AskRefusal::Fetch;
    if (strcmp(word, "check") == 0) rec.refusal = AskRefusal::Check;
  }
  if (readKey(store, kAskKeyCheck, number, sizeof(number))) {
    const unsigned long n = strtoul(number, nullptr, 10);
    rec.check = n <= 5 ? static_cast<uint8_t>(n) : 0;
  }
  if (readKey(store, kAskKeyPosted, word, sizeof(word))) {
    if (strcmp(word, kWaiting) == 0) rec.posted = AskPosted::Waiting;
    if (strcmp(word, kTaken) == 0) rec.posted = AskPosted::Taken;
    if (strcmp(word, kGone) == 0) rec.posted = AskPosted::Gone;
  }
  // Straight into the journal's own buffer: no second one anywhere (Journal::readFrom).
  rec.journal.readFrom(store, kAskKeyJournal);

  // The half-written shapes a power cut can leave, none of which is an ask: an ask with no id or
  // no version to fetch, a `done` with no outcome, and an ended one with no result to account for.
  if (rec.id == 0 || rec.version[0] == '\0') {
    rec.clear();
    return;
  }
  if (rec.step == AskStep::Ended && (rec.outcome == AskOutcome::None || rec.posted == AskPosted::None)) {
    rec.clear();
  }
}

bool saveAsk(RecordStore& store, const AskRecord& rec) {
  if (rec.step == AskStep::None) {
    // The step goes first: a power cut after it leaves no ask at all rather than one that half
    // agrees with the slots.
    bool ok = store.put(kAskKeyStep, nullptr);
    ok = store.put(kAskKeyId, nullptr) && ok;
    ok = store.put(kAskKeyRelease, nullptr) && ok;
    ok = store.put(kAskKeyVersion, nullptr) && ok;
    ok = store.put(kAskKeyBase, nullptr) && ok;
    ok = store.put(kAskKeySha, nullptr) && ok;
    ok = store.put(kAskKeyLength, nullptr) && ok;
    ok = store.put(kAskKeyFrom, nullptr) && ok;
    ok = store.put(kAskKeyTo, nullptr) && ok;
    ok = store.put(kAskKeyOutcome, nullptr) && ok;
    ok = store.put(kAskKeyRefusal, nullptr) && ok;
    ok = store.put(kAskKeyCheck, nullptr) && ok;
    ok = store.put(kAskKeyPosted, nullptr) && ok;
    ok = store.put(kAskKeyJournal, nullptr) && ok;
    return ok;
  }

  char number[24];
  snprintf(number, sizeof(number), "%lld", rec.id);
  bool ok = store.put(kAskKeyId, number);
  snprintf(number, sizeof(number), "%lld", rec.releaseId);
  ok = store.put(kAskKeyRelease, number) && ok;
  snprintf(number, sizeof(number), "%lu", static_cast<unsigned long>(rec.size));
  ok = store.put(kAskKeyLength, number) && ok;
  ok = store.put(kAskKeyVersion, rec.version) && ok;
  ok = store.put(kAskKeyBase, rec.base) && ok;
  ok = store.put(kAskKeySha, rec.sha256hex) && ok;
  ok = store.put(kAskKeyFrom, rec.from) && ok;
  ok = store.put(kAskKeyTo, rec.to) && ok;
  ok = store.put(kAskKeyOutcome, askOutcomeWord(rec.outcome)) && ok;
  ok = store.put(kAskKeyRefusal, askRefusalWord(rec.refusal)) && ok;
  snprintf(number, sizeof(number), "%u", static_cast<unsigned>(rec.check));
  ok = store.put(kAskKeyCheck, rec.check != 0 ? number : nullptr) && ok;
  ok = store.put(kAskKeyPosted, askPostedWord(rec.posted)) && ok;
  ok = store.put(kAskKeyJournal, rec.journal.text()) && ok;
  // And the step last, so that everything it refers to is already there.
  ok = store.put(kAskKeyStep, askStepWord(rec.step)) && ok;
  return ok;
}

// -- the journal, as the library's writer reads one --------------------------------------------

int JournalLines::count() const {
  const size_t all = journal_.count();
  return all > skip_ ? static_cast<int>(all - skip_) : 0;
}

bool JournalLines::at(int i, uint32_t& when, const char*& step, const char*& note) const {
  if (i < 0) return false;
  if (!journal_.entry(static_cast<size_t>(i) + skip_, one_)) return false;
  when = one_.at;
  step = one_.step;
  note = one_.note;
  return true;
}

// -- the machine -------------------------------------------------------------------------------

void Asked::begin(const Seams& seams, const char* runningVersion, bool canBeUpdated, TextOut* out) {
  seams_ = seams;
  version_ = runningVersion != nullptr ? runningVersion : "";
  canBeUpdated_ = canBeUpdated;
  out_ = out;
  // And a fresh machine for this boot, for the reason `Updater::begin` has one: the record is no
  // longer copyable or assignable, so `*this = Asked()` is gone (task p1.17-65). `waiting_` is
  // not touched: nothing reads it unless `pending_` says an ask is waiting for `step()`.
  record_.clear();
  begun_ = false;
  pending_ = false;
  provedByHeartbeat_ = false;
  step_[0] = '\0';
  body_[0] = '\0';
}

void Asked::say(const char* fmt, ...) {
  if (out_ == nullptr) return;
  char line[256];
  va_list args;
  va_start(args, fmt);
  const int n = vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  if (n <= 0) return;
  out_->printf("update: %s", line);
  out_->println();
}

void Asked::journal(const char* step, const char* fmt, ...) {
  char note[kMaxNoteLen + 1];
  va_list args;
  va_start(args, fmt);
  vsnprintf(note, sizeof(note), fmt, args);
  va_end(args);
  record_.journal.add(seams_.clock != nullptr ? seams_.clock->epoch() : 0, step, note);
  if (note[0] != '\0') {
    say("%s: %s", step, note);
  } else {
    say("%s", step);
  }
}

bool Asked::save() {
  if (seams_.store == nullptr) return false;
  const bool ok = saveAsk(*seams_.store, record_);
  if (!ok) say("the record of this ask could not be written");
  return ok;
}

bool Asked::recordIsOurs() const {
  if (record_.step != AskStep::Switching && record_.step != AskStep::Proving && record_.step != AskStep::Ended) {
    return false;
  }
  const UpdateRecord& r = seams_.trial->record();
  if (r.phase == Phase::None) return false;
  return sameVersion(r.to, record_.version) && strcmp(r.from, record_.from) == 0;
}

void Asked::newestStep(char* out, size_t cap) const {
  out[0] = '\0';
  JournalEntry entry;
  // Once the switch has been made the steps are task 60's record's: `switching`, `proving`,
  // `committing`, `rolling-back`, `restored`.
  if (recordIsOurs()) {
    const Journal& theirs = seams_.trial->record().journal;
    for (size_t i = 0; theirs.entry(i, entry); i++) snprintf(out, cap, "%s", entry.step);
    if (out[0] != '\0') return;
  }
  for (size_t i = 0; record_.journal.entry(i, entry); i++) snprintf(out, cap, "%s", entry.step);
}

void Asked::atBoot() {
  begun_ = true;
  loadAsk(*seams_.store, record_);
  if (!record_.present()) return;
  if (record_.step == AskStep::Ended) {
    say("update %lld ended %s; its result %s", record_.id, askOutcomeWord(record_.outcome),
        record_.posted == AskPosted::Waiting ? "has not been taken yet and goes out again"
                                             : "is with the server");
    return;
  }
  say("update %lld is in hand at `%s` and goes on from there", record_.id, askStepWord(record_.step));
  switch (record_.step) {
    case AskStep::Check:
      decide();
      return;
    case AskStep::AddingSlot:
      afterTheSlot();
      return;
    case AskStep::Fetching:
      // A fetch does not go on over a restart (tasks/phase-1.15/FOLLOWUPS.md): one that a restart
      // interrupted begins again, and over WiFi a file is seconds.
      beginFetching(true);
      return;
    case AskStep::Switching:
    case AskStep::Proving:
      watchTheTrial();
      return;
    default:
      return;
  }
}

void Asked::step() {
  if (!begun_) return;
  if (pending_) {
    pending_ = false;
    const UpdateAsk ask = waiting_;
    take(ask);
    return;  // taking an ask can write flash and restart the board: nothing else this pass
  }
  if (!record_.inHand()) return;
  goOn();
}

void Asked::goOn() {
  switch (record_.step) {
    case AskStep::Check:
      decide();
      return;
    case AskStep::AddingSlot:
      afterTheSlot();
      return;
    case AskStep::Fetching:
      watchTheFetch();
      return;
    case AskStep::Switching:
    case AskStep::Proving:
      watchTheTrial();
      return;
    default:
      return;
  }
}

void Asked::take(const UpdateAsk& ask) {
  if (record_.present() && record_.id == ask.id) {
    if (record_.inHand()) {
      say("update %lld is the ask already in hand at `%s`; nothing changes", ask.id, askStepWord(record_.step));
      return;
    }
    if (record_.posted == AskPosted::Taken) {
      // The server asked again for an update whose result it has already taken: it goes out once
      // more, and nothing else happens.
      record_.posted = AskPosted::Waiting;
      save();
      say("update %lld is asked for again and its result was taken: the same result goes out once more", ask.id);
    }
    return;  // waiting: it is already going out. gone: the server does not know this id
  }
  if (record_.inHand() || record_.posted == AskPosted::Waiting) {
    // One ask at a time, and one result at a time. The server asks again in every reply until it
    // has the result, so there is nothing to remember here and nothing is lost.
    say("update %lld is not taken yet: %s", ask.id,
        record_.inHand() ? "another ask is in hand" : "the result of the one before it has not been taken");
    return;
  }

  record_.clear();
  record_.id = ask.id;
  record_.releaseId = ask.releaseId;
  snprintf(record_.version, sizeof(record_.version), "%s", ask.version);
  snprintf(record_.base, sizeof(record_.base), "%s", ask.base);
  snprintf(record_.sha256hex, sizeof(record_.sha256hex), "%s", ask.expectedSha256);
  record_.size = ask.size;
  snprintf(record_.from, sizeof(record_.from), "%s", version_);
  record_.step = AskStep::Check;
  provedByHeartbeat_ = false;
  journal(kStepCheck, "table %s, %s running", seams_.table->word(), version_);
  if (!save()) {
    end(AskOutcome::Stuck, AskRefusal::None, 0, "the record of the ask could not be written");
    return;
  }
  decide();
}

void Asked::decide() {
  // The three refusals, in the order the task names them.
  if (seams_.trial->onTrial() || seams_.trial->record().inHand() || seams_.fetch->running()) {
    end(AskOutcome::Refused, AskRefusal::InProgress, 0, "an update is already in hand on this board");
    return;
  }
  const char* word = seams_.table->word();
  if (strcmp(word, kTableOne) != 0 && strcmp(word, kTableTwo) != 0) {
    end(AskOutcome::Refused, AskRefusal::Table, 0, "the table is `%.12s`, which this firmware will not write", word);
    return;
  }
  if (sameVersion(record_.version, version_)) {
    end(AskOutcome::Refused, AskRefusal::Current, 0, "%.24s is the version this board runs", record_.version);
    return;
  }

  if (strcmp(word, kTableOne) == 0) {
    // One sector, at the partition table's own address, and then a restart: the ask goes on at the
    // boot after it (task p1.15-57). A power cut in that moment leaves a board that needs a merged
    // image, and it is the only moment of this pile that can.
    journal(kStepAddingSlot, "%s", "");
    record_.step = AskStep::AddingSlot;
    if (!save()) {
      end(AskOutcome::Stuck, AskRefusal::None, 0, "the record of the ask could not be written");
      return;
    }
    char line[200] = {0};
    bool holds = true;
    const bool added = seams_.table->addSecondSlot(line, sizeof(line), &holds);
    say("adding-slot: %s", line);
    if (!added) {
      // Nobody is at the console to read the sentence, so the journal says which of the two
      // endings it was in words the server's page can show as they are (task p1.16-63): a board
      // that is as it was and boots, or one whose sector holds no table at all. Nothing restarts
      // a board in either case - a restart is what the second one does not survive.
      end(AskOutcome::Stuck, AskRefusal::None, 0, "%s; %.55s",
          holds ? "the table is the one the board had" : "the sector holds no partition table", line);
      return;
    }
    seams_.restart->note("a second application slot was added for an update the server asked for");
    seams_.restart->restart();  // does not return on a board
    return;
  }
  beginFetching(false);
}

void Asked::afterTheSlot() {
  const char* word = seams_.table->word();
  if (strcmp(word, kTableTwo) != 0) {
    end(AskOutcome::Stuck, AskRefusal::None, 0, "the table is `%.12s` after the slot was added", word);
    return;
  }
  beginFetching(false);
}

void Asked::beginFetching(bool again) {
  if (again) {
    journal(kStepFetching, "%.24s, %lu bytes, from the beginning after a restart", record_.version,
            static_cast<unsigned long>(record_.size));
  } else {
    journal(kStepFetching, "%.24s, %lu bytes", record_.version, static_cast<unsigned long>(record_.size));
  }
  record_.step = AskStep::Fetching;
  if (!save()) {
    end(AskOutcome::Stuck, AskRefusal::None, 0, "the record of the ask could not be written");
    return;
  }
  startTheFetch();
}

void Asked::startTheFetch() {
  // **Not until the server can be reached.** A fetch begun with the link down would spend its
  // three tries in three passes of the loop and end the ask for nothing, and a board resuming an
  // interrupted fetch at its own boot is exactly that: the WiFi has not joined yet when this runs.
  // So it waits here, and `goOn()` brings it back every pass until it can ask
  // (tasks/phase-1.15/FOLLOWUPS.md, *a fetch does not go on over a restart*).
  if (!seams_.fetch->ready()) return;
  if (seams_.fetch->start(record_.base)) return;
  // It did not start. A fetch that is running is somebody else's and is not this ask's to read,
  // and a fetcher with no ending at all is one that would not begin: either way nothing of a
  // release is in the slot, and `watchTheFetch` below never calls back into this.
  if (seams_.fetch->running()) return;
  if (seams_.fetch->result().ending == Ending::Running) {
    end(AskOutcome::Stuck, AskRefusal::None, 0, "the fetch of %.24s would not begin", record_.version);
    return;
  }
  watchTheFetch();
}

void Asked::watchTheFetch() {
  if (seams_.fetch->running()) return;
  const FetchResult& r = seams_.fetch->result();
  switch (r.ending) {
    case Ending::Fetched:
      beginSwitching();
      return;
    case Ending::Check:
      end(AskOutcome::Refused, AskRefusal::Check, r.check, "check %u: %.80s", static_cast<unsigned>(r.check), r.why);
      return;
    case Ending::Fetch:
      end(AskOutcome::Refused, AskRefusal::Fetch, 0, "%.90s", r.why);
      return;
    case Ending::Slot:
      end(AskOutcome::Stuck, AskRefusal::None, 0, "%.90s", r.why);
      return;
    case Ending::Running:
      // Nothing has been started and nothing was refused: the request is still waiting for the
      // link, or for the board to finish starting up.
      startTheFetch();
      return;
  }
}

void Asked::beginSwitching() {
  // Task 60 writes the `switching` entry of the journal itself, before the boot choice is set,
  // and every entry after it: the result's journal is this ask's entries and then that record's.
  record_.step = AskStep::Switching;
  if (!save()) {
    end(AskOutcome::Stuck, AskRefusal::None, 0, "the record of the ask could not be written");
    return;
  }
  char why[224] = {0};
  if (seams_.trial->apply(why, sizeof(why))) return;  // does not return on a board
  end(AskOutcome::Stuck, AskRefusal::None, 0, "%.90s", why);
}

void Asked::watchTheTrial() {
  const bool ours = recordIsOurs();
  if (ours && seams_.trial->onTrial()) {
    if (record_.step != AskStep::Proving) {
      record_.step = AskStep::Proving;
      save();
    }
    return;
  }
  if (ours && seams_.trial->record().phase == Phase::Ended) {
    // The record's ending, in the contract's words. Its third - `stuck` - is a firmware that
    // failed its trial and could not be sent back at all (task p1.16-64, deliverable 5): nothing
    // was rolled back, so the result does not say so, and `toVersion` below is the version that
    // runs, which is the one that failed. No outcome is added: `stuck` is the contract's own word
    // for a board that cannot say what happened, and its journal's last entry says what that was.
    const Ended ended = seams_.trial->record().ended;
    AskOutcome outcome = AskOutcome::Stuck;
    if (ended == Ended::Committed) outcome = AskOutcome::Committed;
    if (ended == Ended::RolledBack) outcome = AskOutcome::RolledBack;
    end(outcome, AskRefusal::None, 0, "%s", "");
    return;
  }
  if (ours) return;  // still `switching`: the restart into the new image has not happened yet
  // The record of this ask's switch is not there at all, which only a write that was cut off or
  // another switch made on top of it can do. The board cannot say what happened.
  end(AskOutcome::Stuck, AskRefusal::None, 0, "nothing was left of the switch to %.24s", record_.version);
}

void Asked::end(AskOutcome outcome, AskRefusal refusal, uint8_t check, const char* fmt, ...) {
  char why[kMaxNoteLen + 1];
  va_list args;
  va_start(args, fmt);
  vsnprintf(why, sizeof(why), fmt, args);
  va_end(args);

  // The journal of the switch and the trial is task 60's record's, and it goes after this ask's
  // own entries - which is the order the contract's own sample has: `check`, `adding-slot`,
  // `fetching`, then `switching`, `proving` and the ending. The bounds are applied by `add`.
  if (recordIsOurs()) {
    const Journal& theirs = seams_.trial->record().journal;
    JournalEntry entry;
    for (size_t i = 0; theirs.entry(i, entry); i++) record_.journal.add(entry.at, entry.step, entry.note);
  }
  // An ending the journal does not already say: a refusal, and a board that cannot say what
  // happened. A commit and a rollback are task 60's own last entry and need no second one.
  if (why[0] != '\0') journal(askOutcomeWord(outcome), "%s", why);

  snprintf(record_.to, sizeof(record_.to), "%s", version_);
  record_.step = AskStep::Ended;
  record_.outcome = outcome;
  record_.refusal = refusal;
  record_.check = check;
  record_.posted = AskPosted::Waiting;
  save();
  say("update %lld ended %s%s%s: the result goes out at the next heartbeat", record_.id, askOutcomeWord(outcome),
      refusal != AskRefusal::None ? ", refused " : "", refusal != AskRefusal::None ? askRefusalWord(refusal) : "");
}

// -- the heartbeat, the ask, the result ---------------------------------------------------------

bool Asked::describeUpdate(HeartbeatUpdate& out) {
  // A build that trusts no release key could not believe a signature, so it takes no update at
  // all and the server is not told otherwise: no member (task p1.15-58, src/update/Trust.h).
  if (!canBeUpdated_ || seams_.table == nullptr) return false;
  out.table = seams_.table->word();
  out.hasAsk = record_.inHand();
  if (out.hasAsk) {
    out.id = record_.id;
    newestStep(step_, sizeof(step_));
    out.step = step_;
  }
  return true;
}

void Asked::onUpdateAsk(const UpdateAsk& ask) {
  if (!canBeUpdated_) return;
  // Kept for `step()`, which runs from the loop and never from the pump inside a job: taking an
  // ask can write flash and restart the board, and a job that was running when the ask came is
  // finished first (deliverable 7).
  waiting_ = ask;
  pending_ = true;
}

void Asked::onHeartbeatAnswered(const HeartbeatUpdate& posted) {
  // Deliverable 5: a heartbeat that carried `step` `proving` and was answered 2xx is the third
  // condition of the trial - the new firmware has reached the server over TLS with its token.
  if (posted.hasAsk && posted.step != nullptr && strcmp(posted.step, kStepProving) == 0) {
    provedByHeartbeat_ = true;
  }
}

bool Asked::heartbeatProvesTheTrial(bool everHeartbeated) const {
  if (record_.inHand()) return provedByHeartbeat_;
  return everHeartbeated;
}

size_t Asked::buildResult(size_t skip) {
  JournalLines lines(record_.journal, skip);
  UpdateResult result;
  result.outcome = askOutcomeWord(record_.outcome);
  result.fromVersion = record_.from;
  result.toVersion = record_.to;
  if (record_.refusal != AskRefusal::None) result.refusal = askRefusalWord(record_.refusal);
  result.hasCheck = record_.check != 0;
  result.check = static_cast<int>(record_.check);
  result.journal = &lines;
  return writeUpdateResult(result, body_, sizeof(body_));
}

bool Asked::resultToPost(long long& id, const char*& body, size_t& len) {
  if (record_.step != AskStep::Ended || record_.posted != AskPosted::Waiting) return false;
  const size_t entries = record_.journal.count();
  for (size_t skip = 0; skip <= entries; skip++) {
    const size_t n = buildResult(skip);
    if (n == 0) continue;
    if (skip > 0) {
      say("the result's journal was shortened by its %u oldest entries to fit the body", static_cast<unsigned>(skip));
    }
    id = record_.id;
    body = body_;
    len = n;
    return true;
  }
  say("the result of update %lld does not fit in %u bytes and is not posted", record_.id,
      static_cast<unsigned>(sizeof(body_)));
  return false;
}

void Asked::onResultPosted(long long id, bool taken, bool notFound) {
  if (record_.step != AskStep::Ended || record_.id != id) return;
  if (taken) {
    record_.posted = AskPosted::Taken;
    save();
    return;
  }
  if (notFound) {
    record_.posted = AskPosted::Gone;
    save();
    return;
  }
  // Neither: it waits for the next heartbeat's turn.
}

// -- what a person reads -----------------------------------------------------------------------

const char* Asked::whyNotNow() const {
  if (!record_.inHand()) return nullptr;
  return "an update meshmaster.dk asked for is in hand; `update` says which step it stands at";
}

void Asked::report(TextOut& out) const {
  if (!canBeUpdated_) {
    out.println("asked     this build trusts no release key, so meshmaster.dk is not offered this board");
    return;
  }
  if (!record_.present()) {
    out.println("asked     nothing: no update has been asked for from meshmaster.dk");
    return;
  }
  if (record_.inHand()) {
    char step[kMaxStepLen + 1];
    newestStep(step, sizeof(step));
    out.printf("asked     update %lld in hand at `%s`: release %lld, %s, %lu bytes\r\n", record_.id,
               step[0] != '\0' ? step : askStepWord(record_.step), record_.releaseId, record_.version,
               static_cast<unsigned long>(record_.size));
    out.printf("          no job is claimed until it has ended; %s is what was running\r\n", record_.from);
    return;
  }
  out.printf("asked     update %lld ended %s%s%s: %s to %s\r\n", record_.id, askOutcomeWord(record_.outcome),
             record_.refusal != AskRefusal::None ? ", refused " : "",
             record_.refusal != AskRefusal::None ? askRefusalWord(record_.refusal) : "", record_.from, record_.to);
  if (record_.check != 0) {
    out.printf("          check %u of five is what refused the release\r\n", static_cast<unsigned>(record_.check));
  }
  switch (record_.posted) {
    case AskPosted::Waiting:
      out.println("          its result goes out at the next heartbeat's turn");
      break;
    case AskPosted::Taken:
      out.println("          its result is with the server");
      break;
    case AskPosted::Gone:
      out.println("          the server does not know this update, so its result is not posted again");
      break;
    case AskPosted::None:
      break;
  }
}

}  // namespace update
}  // namespace mm
