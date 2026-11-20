// The server asks, and the board does the steps (task p1.15-61).
//
// A person at the console can add the slot, fetch a release, switch to it and watch it proved or
// undone (tasks 57, 59, 60). This is the same thing started by a person on meshmaster.dk with one
// press, and **nothing else starts it**: not a release existing, not a schedule, not the server
// by itself.
//
// **It is not a job.** A job belongs to a repeater and this is about the exit node itself, so the
// server asks in the reply to this board's heartbeat (`agent/ConfigReply.h`, `UpdateAsk`), in
// every reply from the moment somebody pressed until the result is recorded. An exit node's
// heartbeat goes out every thirty seconds to two minutes, so an ask is heard within that. The
// board then does the steps in order by itself, across two restarts, and posts **one** result to
// `POST /agent/v1/updates/{id}/result`.
//
//   check         refused as `inProgress` when an update is in hand or a firmware is on trial, as
//                 `table` when this board's table is one this firmware will not write, as
//                 `current` when the version asked for is the one running;
//   adding-slot   when the table word is `one`: task 57's one function writes the sector, and the
//                 board restarts. The ask goes on after that restart, and a table that is not
//                 `two` then ends it `stuck`;
//   fetching      task 59, into the slot that is not running. Its ending is the refusal `fetch`,
//                 or `check` with the number of the check that refused it;
//   switching     task 60: the boot choice, the restart, and the ten minutes the new image then
//                 has to start the mesh, join the WiFi and reach the server. `committed` when it
//                 does, `rolledBack` when it does not.
//
// Each of the first three is one more entry of the journal, written **before** the step begins;
// `switching` and everything after it are task 60's own entries, and the result's journal is this
// ask's entries followed by that record's. The journal is meshmaster.dk's `FirmwareJournalEntry`
// and its bounds are its `FirmwareJournalBounds` (`update/UpdateRecord.h`).
//
// **What outlives the restarts** is in the settings store beside task 59's and task 60's records,
// under fourteen keys of this task's own (below). A boot that finds an ask in hand goes on with it
// without being asked again, and the same `id` in a later reply is nothing new.
//
// **No job while an update is in hand.** From the moment an ask is taken until it has ended the
// loop claims nothing (`agent/AgentLoop.h`, `IUpdateProvider::holdsJobs`); a job that was already
// running is run to its end and its result posted first, because a claim runs to the end before
// the loop looks at anything else.
//
// **One result, until it is taken.** It is posted at every heartbeat's turn until the server
// answers `200` - which it also answers for a result it already has, the first one standing - or
// the one `404`, which is an id that is not this exit node's. It survives a restart until then,
// and the same `id` in a later reply has it posted once more.
//
// Everything in this file is portable and host-tested (`test/test_update_ask`): the table is
// behind AskTable, the fetch behind AskFetch, the switch and the trial behind AskTrial, the store
// behind task 59's RecordStore, and the clock and the restart behind task 60's TrialClock and
// DeliberateRestart. The board's halves are `src/main.cpp` and nothing else.
//
// Nothing here is a secret and nothing here is overheard: versions, a release id, a path the
// server published, sizes, step words and sentences this firmware wrote itself. The token is not
// in this file and no line of it is a header.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <agent/AgentLoop.h>
#include <agent/ConfigReply.h>
#include <agent/Envelopes.h>

#include "Fetch.h"
#include "Trial.h"
#include "UpdateRecord.h"

namespace mm {
class TextOut;
}

namespace mm {
namespace update {

// The three step words of this task. The five after them are task 60's record's.
extern const char kStepCheck[];
extern const char kStepAddingSlot[];
extern const char kStepFetching[];

// The fourteen keys, so that the board's store and a test's name the same things. They are not
// settings: nobody types them, `get` does not print them and `help` does not list them, and
// `erase confirm` forgets them with everything else.
//
//   fw.ask.step   `check`, `adding-slot`, `fetching`, `switching`, `proving` or `done`; absent
//                 means no ask has ever been taken
//   fw.ask.id     the ask's id, in decimal: what the result is posted under
//   fw.ask.rel    the release's id
//   fw.ask.ver    the version asked for
//   fw.ask.base   the release's base path, `/agent/v1/firmware/<id>`
//   fw.ask.sha    the SHA-256 the server expects the chosen file to have, 64 lowercase hex
//   fw.ask.len    the size the server expects, in decimal
//   fw.ask.from   the version that was running when the ask was taken
//   fw.ask.to     the version running when the result was written
//   fw.ask.out    `committed`, `rolledBack`, `refused` or `stuck`, once the ask has ended
//   fw.ask.ref    for a refusal: which one
//   fw.ask.chk    for the refusal `check`: its number, 1 to 5
//   fw.ask.post   `waiting` while the result has not been taken, `taken` after the 200, `gone`
//                 after the one 404
//   fw.ask.jr     the journal, one line an entry (update/UpdateRecord.h)
extern const char kAskKeyStep[];
extern const char kAskKeyId[];
extern const char kAskKeyRelease[];
extern const char kAskKeyVersion[];
extern const char kAskKeyBase[];
extern const char kAskKeySha[];
extern const char kAskKeyLength[];
extern const char kAskKeyFrom[];
extern const char kAskKeyTo[];
extern const char kAskKeyOutcome[];
extern const char kAskKeyRefusal[];
extern const char kAskKeyCheck[];
extern const char kAskKeyPosted[];
extern const char kAskKeyJournal[];

// Where an ask stands. Anything `fw.ask.step` holds that is not one of these words reads as *no
// ask*, so a record half written by a power cut is not a record.
enum class AskStep : uint8_t {
  None = 0,
  Check = 1,       // the three refusals are being decided
  AddingSlot = 2,  // the sector is being written and the board is about to restart
  Fetching = 3,    // task 59 is running, or is about to begin again after a restart
  Switching = 4,   // task 60 has the boot choice and the board is about to restart into it
  Proving = 5,     // the new image is running and has its ten minutes
  Ended = 6,       // `fw.ask.out` says how it ended
};

// How an ask ended, in meshmaster.dk's words (its task 244's `UpdateOutcomes`).
enum class AskOutcome : uint8_t {
  None = 0,
  Committed = 1,   // the new firmware runs and has proved itself
  RolledBack = 2,  // it ran, did not prove itself, and the old one runs again
  Refused = 3,     // nothing was switched; the refusal says why
  Stuck = 4,       // the board cannot say what happened
};

// Why an ask was refused.
enum class AskRefusal : uint8_t {
  None = 0,
  InProgress = 1,  // an update is in hand, or a firmware is on trial
  Table = 2,       // the board's table is `other`
  Current = 3,     // the version asked for is the one running
  Fetch = 4,       // the release could not be fetched
  Check = 5,       // a check failed, and `check` is its number
};

// Where the result's post stands.
enum class AskPosted : uint8_t {
  None = 0,     // the ask has not ended: there is no result yet
  Waiting = 1,  // it goes out at the next heartbeat's turn
  Taken = 2,    // the server answered 200. The same `id` again has it posted once more
  Gone = 3,     // the server answered the one 404. It is never posted again
};

const char* askStepWord(AskStep step);
const char* askOutcomeWord(AskOutcome outcome);
const char* askRefusalWord(AskRefusal refusal);
const char* askPostedWord(AskPosted posted);

// -- the seams ---------------------------------------------------------------------------------

// This board's partition table, and the one function that adds the second application slot
// (task p1.15-57; src/platform/PartitionTable.h). One word, and no fourth.
class AskTable {
 public:
  virtual ~AskTable() {}
  virtual const char* word() const = 0;  // "one" | "two" | "other"
  // Write the sector. True when the slot is there afterwards; `line` is the one sentence the
  // function answered, which goes into the journal either way. `theSectorHoldsATable` says, when
  // the slot was not added, which of the two endings it was: the table the board had is still
  // there and the board boots, or the sector holds neither and it does not (task p1.16-63).
  // An ask reads it because nobody is at the console to read the sentence.
  virtual bool addSecondSlot(char* line, size_t cap, bool* theSectorHoldsATable) = 0;
};

// The fetch of a release into the slot that is not running (task p1.15-59).
class AskFetch {
 public:
  virtual ~AskFetch() {}
  // Whether a fetch could reach the server at all now: the board started up, the server is
  // configured and the link is up. A fetch begun without it would spend its three tries in three
  // passes of the loop and end the ask for nothing, which is exactly what a board resuming an
  // interrupted fetch at its own boot would do.
  virtual bool ready() const = 0;
  // Start a fetch of `base`. False when it did not start at all, and then `result()` says why.
  virtual bool start(const char* base) = 0;
  virtual bool running() const = 0;
  virtual const FetchResult& result() const = 0;
};

// The switch to a judged image and the ten minutes it then has (task p1.15-60).
class AskTrial {
 public:
  virtual ~AskTrial() {}
  // `update apply`'s own call: false and one sentence in `why`; true does not return on a board.
  virtual bool apply(char* why, size_t cap) = 0;
  virtual bool onTrial() const = 0;
  virtual const UpdateRecord& record() const = 0;
};

// -- what outlives the restarts ----------------------------------------------------------------

struct AskRecord {
  AskRecord() = default;
  // **Neither copied nor moved** (task p1.17-65), for the reason `UpdateRecord` is not: 2 700-odd
  // bytes, most of them the journal, and `loadAsk` used to return one by value into the member
  // that keeps it - the second half of what made 0.7.3 restart for ever.
  AskRecord(const AskRecord&) = delete;
  AskRecord& operator=(const AskRecord&) = delete;
  AskRecord(AskRecord&&) = delete;
  AskRecord& operator=(AskRecord&&) = delete;

  AskStep step = AskStep::None;
  long long id = 0;
  long long releaseId = 0;
  char version[33] = {0};
  char base[kMaxBaseLen + 1] = {0};
  char sha256hex[2 * 32 + 1] = {0};
  uint32_t size = 0;
  char from[33] = {0};
  char to[33] = {0};
  AskOutcome outcome = AskOutcome::None;
  AskRefusal refusal = AskRefusal::None;
  uint8_t check = 0;
  AskPosted posted = AskPosted::None;
  Journal journal;

  // An ask this board still knows about at all: in hand, or ended with a result that may yet be
  // asked for again.
  bool present() const { return step != AskStep::None; }
  // An ask that has not ended: the steps are still being taken, and no job is claimed.
  bool inHand() const { return step != AskStep::None && step != AskStep::Ended; }

  // Empty again, in place: every field as a fresh record has it, journal and all. This is what
  // `= AskRecord()` used to be, without the record built beside the one being emptied.
  void clear();
};

// The record as the store holds it, read **into the record it is given** - the object that keeps
// it, so that nothing of this size is ever on the stack. A step word that is not one of the six,
// or a `done` with no outcome, reads as *no ask* - and then nothing else of it is believed: every
// way out but the one that read a whole ask leaves `rec` empty, whatever it held before.
void loadAsk(RecordStore& store, AskRecord& rec);

// The record, whole. `rec.step == AskStep::None` forgets every one of the fourteen keys. The step
// is written **last** on the way in and cleared **first** on the way out, so a power cut in the
// middle of either leaves a record that is not half believed - the rule tasks 59 and 60 write
// their records by.
bool saveAsk(RecordStore& store, const AskRecord& rec);

// Two versions that are the same release. The server's catalogue may carry `0.8.0` where an image
// carries `0.8.0+1a2b3c4`: the part before the `+` must match, and the build metadata only when
// both sides have it.
bool sameVersion(const char* a, const char* b);

// A result's journal as the library's writer reads one (agent/Envelopes.h), over the text a
// Journal keeps. `skip` drops that many of the oldest entries, which is how a journal too long for
// the body's buffer is shortened until it fits.
//
// **The two strings it hands back are borrowed** and are overwritten by the next call, which is
// exactly what `writeUpdateResult` allows: it writes each entry before it asks for the next.
class JournalLines final : public UpdateJournal {
 public:
  JournalLines(const Journal& journal, size_t skip) : journal_(journal), skip_(skip) {}
  int count() const override;
  bool at(int i, uint32_t& when, const char*& step, const char*& note) const override;

 private:
  const Journal& journal_;
  size_t skip_;
  mutable JournalEntry one_;
};

// -- the machine -------------------------------------------------------------------------------

class Asked final : public IUpdateProvider {
 public:
  struct Seams {
    AskTable* table = nullptr;
    AskFetch* fetch = nullptr;
    AskTrial* trial = nullptr;
    RecordStore* store = nullptr;
    TrialClock* clock = nullptr;
    DeliberateRestart* restart = nullptr;
  };

  // `runningVersion` is what this image says it is (its own descriptor's version).
  // `canBeUpdated` is false for a build that trusts no release key (src/update/Trust.h): such a
  // build writes no heartbeat member and takes no ask, because nothing could be signed in a way
  // it believes. `out` may be nullptr; with one, every step prints a line as it happens.
  void begin(const Seams& seams, const char* runningVersion, bool canBeUpdated, TextOut* out);

  // At boot, once, **after** task 60's `Updater::atBoot()` - which is what decides whether this
  // boot is a trial, a rollback or neither, and this reads its answer. An ask in hand goes on from
  // the step it stands at.
  void atBoot();

  // Every pass of the loop, from `loop()` and **not** from the pump inside a job: a step of this
  // machine can restart the board, and a job that was running when the ask came is finished first.
  void step();

  // What the console's `update` prints about the ask: the one in hand, or the last one's result.
  void report(TextOut& out) const;

  // Whether an ask is in hand, which is what `update fetch` and `update apply` are refused by.
  bool inHand() const { return record_.inHand(); }
  // The sentence a person is given then, or nullptr.
  const char* whyNotNow() const;

  const AskRecord& record() const { return record_; }

  // The trial's third condition (deliverable 5): while an ask is in hand it is a heartbeat that
  // carried `update` with `step` `proving` **and was answered 2xx**, and nothing else. With no ask
  // in hand it is task 60's own rule, any answered heartbeat, which is what a switch made at the
  // console gets.
  bool heartbeatProvesTheTrial(bool everHeartbeated) const;

  // IUpdateProvider
  bool describeUpdate(HeartbeatUpdate& out) override;
  void onUpdateAsk(const UpdateAsk& ask) override;
  void onHeartbeatAnswered(const HeartbeatUpdate& posted) override;
  bool resultToPost(long long& id, const char*& body, size_t& len) override;
  void onResultPosted(long long id, bool taken, bool notFound) override;
  bool holdsJobs() const override { return record_.inHand(); }

 private:
  // The ask a reply handed over, waiting for `step()` to act on it: `onUpdateAsk` is called from
  // inside the heartbeat, which can be inside a job, and taking an ask can write flash and
  // restart the board.
  void take(const UpdateAsk& ask);
  // The step the ask stands at, taken. Called by `take`, by `atBoot` and by `step`.
  void goOn();
  // The three refusals, in the order the task names them, and then the next step.
  void decide();
  // The boot after the sector was written: `two`, or the ask ends `stuck`.
  void afterTheSlot();
  void beginFetching(bool again);
  // The request, once the server can be reached. Called again every pass until it can be.
  void startTheFetch();
  void watchTheFetch();
  void beginSwitching();
  void watchTheTrial();
  void end(AskOutcome outcome, AskRefusal refusal, uint8_t check, const char* fmt, ...)
      __attribute__((format(printf, 5, 6)));
  void journal(const char* step, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
  void say(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
  bool save();
  // Whether task 60's record is this ask's own switch, which is what says its journal belongs in
  // this result and what its ending means for this ask.
  bool recordIsOurs() const;
  // The newest step of the journal, which is what the heartbeat carries: task 60's record's once
  // the switch has been made, and this ask's own before that.
  void newestStep(char* out, size_t cap) const;
  // The result's bytes, with `skip` of the oldest journal entries dropped. 0 when they did not fit.
  size_t buildResult(size_t skip);

  Seams seams_;
  const char* version_ = "";
  bool canBeUpdated_ = false;
  TextOut* out_ = nullptr;

  AskRecord record_;
  bool begun_ = false;
  bool pending_ = false;  // an ask is waiting for step() to take it
  UpdateAsk waiting_;
  // Deliverable 5's latch, which is RAM and not the store: a trial is one boot.
  bool provedByHeartbeat_ = false;

  // What the heartbeat's member points at for the length of one heartbeat.
  char step_[kMaxStepLen + 1] = {0};

  // The result's bytes. Forty journal entries at the contract's bounds - 2 000 characters of step
  // and note in all - come to about 3 900 bytes, which is more than this; a journal that long is
  // shortened from the oldest end until the body fits, which is what `buildResult` is for, and the
  // journal of a real update is seven entries and under five hundred bytes.
  char body_[2048] = {0};
};

}  // namespace update
}  // namespace mm
