// The server asks, and the board does the steps (task p1.15-61, src/update/Ask.h).
//
// No test here can write a board's partition table, fetch 1.4 MB over TLS, boot a second slot or
// pull a board's power, so all six of the machine's seams are scripted: a table whose word a test
// sets and whose one write a test allows or refuses, a fetch that answers whatever ending a test
// gives it, a switch and a trial that keep task 60's own record in the store the way task 60
// keeps it, a clock that moves only when a test says so, a restart that **returns** - so that a
// test can go on and look at what was left behind - and a record store that is a map and outlives
// a "reboot", which is what makes an ask across three boots a thing a test can drive at all.
//
// Every fake pushes a word into one shared list as it is called, which is how *the steps in
// order* is held rather than described.
//
// The bytes of the result are held against the contract fixture
// `tests/fixtures/contracts/UpdateResult.json`, which meshmaster.dk's task 244 made with its own
// serializer: a difference there is a wire the two ends do not agree on.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <agent/TextOut.h>

#include "update/Ask.h"
#include "update/Fetch.h"
#include "update/Trial.h"
#include "update/UpdateRecord.h"

#ifndef FIXTURES_DIR
#error "FIXTURES_DIR must be defined; platformio.ini's [env:native] does it"
#endif

namespace {

using mm::update::AskOutcome;
using mm::update::AskPosted;
using mm::update::AskRecord;
using mm::update::AskRefusal;
using mm::update::Asked;
using mm::update::AskStep;
using mm::update::Ended;
using mm::update::Ending;
using mm::update::FetchResult;
using mm::update::JournalEntry;
using mm::update::Phase;
using mm::update::UpdateRecord;

const char* const kOld = "0.7.2+4a4e791";
const char* const kNew = "0.8.0+1a2b3c4";

// -- the fakes ---------------------------------------------------------------------------------

// The record store: a map, which is what makes it outlive a "reboot".
class MapStore final : public mm::update::RecordStore {
 public:
  std::map<std::string, std::string> values;
  std::vector<std::string>* order = nullptr;
  bool refuse = false;

  size_t get(const char* key, char* out, size_t cap) override {
    out[0] = '\0';
    const auto it = values.find(key);
    if (it == values.end()) return 0;
    std::snprintf(out, cap, "%s", it->second.c_str());
    return std::strlen(out);
  }

  bool put(const char* key, const char* value) override {
    if (refuse) return false;
    if (value == nullptr || value[0] == '\0') {
      values.erase(key);
    } else {
      values[key] = value;
    }
    if (order != nullptr && std::strcmp(key, mm::update::kAskKeyStep) == 0) {
      order->push_back(std::string("ask:") + (value == nullptr || value[0] == '\0' ? "none" : value));
    }
    return true;
  }

  bool has(const char* key) const { return values.find(key) != values.end(); }
  std::string value(const char* key) const {
    const auto it = values.find(key);
    return it == values.end() ? std::string() : it->second;
  }
};

class ScriptedClock final : public mm::update::TrialClock {
 public:
  uint32_t nowMs = 1000;
  uint32_t nowEpoch = 1795165200;  // 2026-11-20T09:00:00Z
  uint32_t ms() const override { return nowMs; }
  uint32_t epoch() const override { return nowEpoch; }
  void advance(uint32_t seconds) {
    nowMs += seconds * 1000u;
    nowEpoch += seconds;
  }
};

class ScriptedRestart final : public mm::update::DeliberateRestart {
 public:
  std::vector<std::string>* order = nullptr;
  int notes = 0;
  int posts = 0;
  int restarts = 0;
  std::string lastNote;

  void note(const char* why) override {
    notes++;
    lastNote = why == nullptr ? "" : why;
  }
  void postWhatWaits() override { posts++; }
  void restart() override {
    restarts++;
    if (order != nullptr) order->push_back("restart");
  }
};

// This board's table, and the one write of it.
class ScriptedTable final : public mm::update::AskTable {
 public:
  std::string wordNow = "two";
  std::string wordAfterAdd = "two";
  bool addWorks = true;
  // Which of the two endings a write that did not add the slot had (task p1.16-63): the table the
  // board came with is back in the sector, or the sector holds neither table.
  bool sectorHoldsATable = true;
  std::string addLine = "added     app1 at 0x350000, 0x320000 bytes; reboot for it to take effect";
  int adds = 0;
  std::vector<std::string>* order = nullptr;

  const char* word() const override { return wordNow.c_str(); }

  bool addSecondSlot(char* line, size_t cap, bool* theSectorHoldsATable) override {
    adds++;
    if (order != nullptr) order->push_back("add-slot");
    std::snprintf(line, cap, "%s", addLine.c_str());
    if (theSectorHoldsATable != nullptr) *theSectorHoldsATable = addWorks || sectorHoldsATable;
    if (addWorks) wordNow = wordAfterAdd;
    return addWorks;
  }
};

// The fetch of task 59, as far as this machine can see it: a base asked for, a running flag and an
// ending a test gives it.
class ScriptedFetch final : public mm::update::AskFetch {
 public:
  std::vector<std::string>* order = nullptr;
  std::vector<std::string> bases;
  bool startWorks = true;
  bool runningNow = false;
  bool readyNow = true;
  int starts = 0;
  FetchResult res;

  bool ready() const override { return readyNow; }

  bool start(const char* base) override {
    starts++;
    bases.push_back(base == nullptr ? "" : base);
    if (order != nullptr) order->push_back("fetch");
    if (!startWorks) return false;
    runningNow = true;
    res = FetchResult();
    return true;
  }
  bool running() const override { return runningNow; }
  const FetchResult& result() const override { return res; }

  // What a test says the fetch came to.
  void endWith(Ending ending, uint8_t check, const char* why) {
    runningNow = false;
    res = FetchResult();
    res.ending = ending;
    res.check = check;
    std::snprintf(res.why, sizeof(res.why), "%s", why);
  }
  // A start that was refused before any request: the ending is set and nothing ran.
  void refuseStartWith(Ending ending, uint8_t check, const char* why) {
    startWorks = false;
    endWith(ending, check, why);
  }
};

// The switch and the trial of task 60, keeping that task's own record in the same store the way
// `Updater` keeps it: `switching` before the boot choice, `proving` at the trial's boot, and
// `committing` or `rolling-back` and `restored` at its end.
class ScriptedTrial final : public mm::update::AskTrial {
 public:
  MapStore* store = nullptr;
  std::vector<std::string>* order = nullptr;
  mm::update::TrialClock* clock = nullptr;
  std::string refuse;  // non-empty: `apply` refuses with this sentence and nothing is switched
  std::string from;
  std::string to = kNew;
  bool onTrialNow = false;
  int applies = 0;
  UpdateRecord rec;

  bool apply(char* why, size_t cap) override {
    applies++;
    if (!refuse.empty()) {
      std::snprintf(why, cap, "%s", refuse.c_str());
      return false;
    }
    rec = UpdateRecord();
    rec.phase = Phase::Switching;
    std::snprintf(rec.from, sizeof(rec.from), "%s", from.c_str());
    std::snprintf(rec.to, sizeof(rec.to), "%s", to.c_str());
    rec.at = clock->epoch();
    rec.slot = 1;
    rec.journal.add(clock->epoch(), "switching", "");
    save();
    if (order != nullptr) order->push_back("switch");
    return true;  // on a board this does not return
  }

  bool onTrial() const override { return onTrialNow; }
  const UpdateRecord& record() const override { return rec; }

  // A boot reads the record back, which is the first thing task 60's own `atBoot` does.
  void load() { rec = mm::update::loadUpdate(*store); }

  // What task 60's `atBoot` does for the image that was switched to.
  void beginTrial() {
    rec.phase = Phase::OnTrial;
    rec.journal.add(clock->epoch(), "proving", "mesh up, wifi up");
    onTrialNow = true;
    save();
  }

  void commit() {
    rec.journal.add(clock->epoch(), "committing", "mesh, wifi and the clock, and a heartbeat answered");
    rec.phase = Phase::Ended;
    rec.ended = Ended::Committed;
    onTrialNow = false;
    save();
  }

  // The rollback, which task 60 spreads over two boots: the image on trial writes `rolling-back`
  // and the boot after it writes `restored` and the ending.
  void rollBack(const char* why) {
    rec.journal.add(clock->epoch(), "rolling-back", why);
    std::snprintf(rec.why, sizeof(rec.why), "%s", why);
    onTrialNow = false;
    save();
  }
  void restored(const char* running) {
    rec.journal.add(clock->epoch(), "restored", (std::string(running) + " running").c_str());
    rec.phase = Phase::Ended;
    rec.ended = Ended::RolledBack;
    save();
  }
  // The record's third ending (task p1.16-64): a trial that failed and a board that could not be
  // sent back at all. There is no `restored` entry, because nothing was restored - the image that
  // failed is what runs - and `rolledBack` is not said of it.
  void stuck(const char* why) {
    rec.journal.add(clock->epoch(), "rolling-back", why);
    std::snprintf(rec.why, sizeof(rec.why), "%s", why);
    rec.phase = Phase::Ended;
    rec.ended = Ended::Stuck;
    onTrialNow = false;
    save();
  }

  void save() { mm::update::saveUpdate(*store, rec); }
};

class Lines final : public mm::TextOut {
 public:
  std::string all;
  void write(const char* text, size_t len) override { all.append(text, len); }
  bool holds(const char* what) const { return all.find(what) != std::string::npos; }
};

// -- one board, across its reboots -------------------------------------------------------------

// The store, the table's word and the clock outlive a reboot; the machine and the counters do
// not, which is exactly what a restart is.
struct Board {
  MapStore store;
  ScriptedClock clock;
  std::vector<std::string> order;

  ScriptedTable table;
  ScriptedFetch fetch;
  ScriptedTrial trial;
  ScriptedRestart restart;
  Lines lines;
  Asked asked;
  std::string version = kOld;
  bool canBeUpdated = true;

  Board() {
    store.order = &order;
    table.order = &order;
    fetch.order = &order;
    trial.order = &order;
    trial.store = &store;
    trial.clock = &clock;
    restart.order = &order;
  }

  // A boot: a fresh machine, fresh counters, the store as it was. Task 60's record is read back
  // first, as that task's own `atBoot` reads it.
  Asked& boot(const char* runningVersion) {
    version = runningVersion;
    trial.from = runningVersion;
    trial.applies = 0;
    fetch.starts = 0;
    table.adds = 0;
    restart.restarts = 0;
    restart.notes = 0;
    lines.all.clear();
    trial.load();
    return begin();
  }

  // The boot into the image that was switched to: task 60 finds its own `switching` record and
  // this boot is the trial.
  Asked& bootOnTrial(const char* runningVersion) {
    version = runningVersion;
    trial.from = trial.rec.from;
    trial.applies = 0;
    fetch.starts = 0;
    restart.restarts = 0;
    lines.all.clear();
    trial.load();
    trial.beginTrial();
    return begin();
  }

  // The boot after a trial that failed: the old image is back and task 60's record is finished.
  Asked& bootBack(const char* runningVersion) {
    version = runningVersion;
    trial.applies = 0;
    fetch.starts = 0;
    restart.restarts = 0;
    lines.all.clear();
    trial.load();
    trial.restored(runningVersion);
    return begin();
  }

  // The boot after a trial that failed and that nothing could send back: the image that failed is
  // what runs, and its boot finishes task 60's record as `stuck` (task p1.16-64).
  Asked& bootStuck(const char* runningVersion, const char* why) {
    version = runningVersion;
    trial.applies = 0;
    fetch.starts = 0;
    restart.restarts = 0;
    lines.all.clear();
    trial.load();
    trial.stuck(why);
    return begin();
  }

  Asked& begin() {
    asked = Asked();
    Asked::Seams seams;
    seams.table = &table;
    seams.fetch = &fetch;
    seams.trial = &trial;
    seams.store = &store;
    seams.clock = &clock;
    seams.restart = &restart;
    asked.begin(seams, version.c_str(), canBeUpdated, &lines);
    asked.atBoot();
    return asked;
  }

  // The ask arrives in a heartbeat's reply, and `step()` is what acts on it.
  void hearTheAsk(long long id, const char* releaseVersion = kNew, long long releaseId = 9) {
    mm::UpdateAsk ask;
    ask.id = id;
    ask.releaseId = releaseId;
    std::snprintf(ask.product, sizeof(ask.product), "twinoak-exitnode-firmware");
    std::snprintf(ask.version, sizeof(ask.version), "%s", releaseVersion);
    std::snprintf(ask.base, sizeof(ask.base), "/agent/v1/firmware/%lld", releaseId);
    std::snprintf(ask.expectedSha256, sizeof(ask.expectedSha256),
                  "3b1f0c4e5d6a79880817a6b5c4d3e2f1a0b9c8d7e6f5041322314051607f8e9d");
    ask.size = 1432000;
    asked.onUpdateAsk(ask);
  }

  void pump(int times = 3) {
    for (int i = 0; i < times; i++) asked.step();
  }

  AskRecord record() { return mm::update::loadAsk(store); }

  // The journal's steps, oldest first.
  std::vector<std::string> steps() {
    std::vector<std::string> out;
    const AskRecord rec = record();
    JournalEntry e;
    for (size_t i = 0; rec.journal.entry(i, e); i++) out.push_back(e.step);
    return out;
  }

  // What the heartbeat's member says now.
  mm::HeartbeatUpdate member() {
    mm::HeartbeatUpdate out;
    EXPECT_TRUE(asked.describeUpdate(out));
    return out;
  }

  // One post of the result, answered as a test says. Returns false when none waited.
  bool postTheResult(bool taken, bool notFound, std::string* body = nullptr) {
    long long id = 0;
    const char* bytes = nullptr;
    size_t len = 0;
    if (!asked.resultToPost(id, bytes, len)) return false;
    EXPECT_EQ(record().id, id);
    if (body != nullptr) body->assign(bytes, len);
    asked.onResultPosted(id, taken, notFound);
    return true;
  }
};

std::string contractFixture(const char* name) {
  std::ifstream in(std::string(FIXTURES_DIR) + "/" + name, std::ios::binary);
  std::ostringstream all;
  all << in.rdbuf();
  std::string text = all.str();
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
  return text;
}

}  // namespace

// ---- the whole way through, from the table `two` ---------------------------------------------

TEST(TheWholeWay, WithTheTableTwoAJudgedReleaseAndATrialThatPassesTheResultIsCommitted) {
  Board b;
  b.table.wordNow = "two";
  b.boot(kOld);
  ASSERT_FALSE(b.asked.inHand());

  // The ask arrives in a reply. Nothing happens inside the heartbeat: the step after it is what
  // takes it, so a job that was running when it came is finished first.
  b.hearTheAsk(17);
  EXPECT_FALSE(b.asked.inHand()) << "the ask is not taken inside the heartbeat";
  EXPECT_EQ(0, b.fetch.starts);

  b.asked.step();
  EXPECT_TRUE(b.asked.inHand());
  EXPECT_TRUE(b.asked.holdsJobs()) << "no job is claimed from `check` passed onwards";
  EXPECT_EQ(AskStep::Fetching, b.record().step);
  ASSERT_EQ(1u, b.fetch.bases.size());
  EXPECT_EQ("/agent/v1/firmware/9", b.fetch.bases[0]);
  EXPECT_EQ(std::vector<std::string>({"check", "fetching"}), b.steps());

  // The fetch judges the image. The switch follows, and the board restarts into it.
  b.clock.advance(40);
  b.fetch.endWith(Ending::Fetched, 0, "fetched 0.8.0+1a2b3c4");
  b.asked.step();
  EXPECT_EQ(1, b.trial.applies);
  EXPECT_EQ(AskStep::Switching, b.record().step);

  // ... the board restarts, and the new image comes up on trial.
  b.clock.advance(30);
  b.bootOnTrial(kNew);
  EXPECT_EQ(AskStep::Proving, b.record().step);
  EXPECT_TRUE(b.asked.holdsJobs());
  mm::HeartbeatUpdate m = b.member();
  EXPECT_STREQ("two", m.table);
  EXPECT_TRUE(m.hasAsk);
  EXPECT_EQ(17, m.id);
  EXPECT_STREQ("proving", m.step);

  // The three conditions are seen and task 60 commits. The ask ends with them.
  b.clock.advance(20);
  b.trial.commit();
  b.asked.step();
  EXPECT_FALSE(b.asked.inHand());
  EXPECT_FALSE(b.asked.holdsJobs()) << "the claim is free again the moment the ask has ended";

  const AskRecord rec = b.record();
  EXPECT_EQ(AskStep::Ended, rec.step);
  EXPECT_EQ(AskOutcome::Committed, rec.outcome);
  EXPECT_EQ(AskRefusal::None, rec.refusal);
  EXPECT_EQ(0, rec.check);
  EXPECT_STREQ(kOld, rec.from);
  EXPECT_STREQ(kNew, rec.to);
  EXPECT_EQ(AskPosted::Waiting, rec.posted);

  // The journal is this ask's three steps and then task 60's own, in the order they were taken.
  EXPECT_EQ(std::vector<std::string>({"check", "fetching", "switching", "proving", "committing"}), b.steps());

  // And the steps really were in that order, as the fakes saw them.
  std::vector<std::string> seen;
  for (const std::string& word : b.order) {
    if (word == "fetch" || word.rfind("ask:", 0) == 0 || word == "switch" || word == "restart" ||
        word == "add-slot") {
      seen.push_back(word);
    }
  }
  EXPECT_EQ(std::vector<std::string>({"ask:check", "ask:fetching", "fetch", "ask:switching", "switch", "ask:proving",
                                      "ask:done"}),
            seen)
      << "each step is written down before it begins: the record says `fetching` and then the "
         "request goes out";

  // The result goes out and is taken.
  std::string body;
  EXPECT_TRUE(b.postTheResult(true, false, &body));
  EXPECT_NE(std::string::npos, body.find("\"outcome\":\"committed\"")) << body;
  EXPECT_NE(std::string::npos, body.find("\"fromVersion\":\"0.7.2+4a4e791\"")) << body;
  EXPECT_NE(std::string::npos, body.find("\"toVersion\":\"0.8.0+1a2b3c4\"")) << body;
  EXPECT_EQ(std::string::npos, body.find("\"refusal\"")) << body;
  EXPECT_EQ(std::string::npos, body.find("\"check\":")) << body;
  EXPECT_EQ(AskPosted::Taken, b.record().posted);
  EXPECT_FALSE(b.postTheResult(true, false)) << "a result that was taken does not go out again";

  // And the heartbeat says the board can be updated and has nothing in hand.
  m = b.member();
  EXPECT_STREQ("two", m.table);
  EXPECT_FALSE(m.hasAsk);
}

// ---- the same from the table `one` -----------------------------------------------------------

TEST(TheWholeWay, FromTheTableOneTheSlotIsAddedAndTheAskGoesOnAfterTheRestart) {
  Board b;
  b.table.wordNow = "one";
  b.table.wordAfterAdd = "two";
  b.boot(kOld);

  b.hearTheAsk(17);
  b.asked.step();
  // The sector was written and the board was told to restart; nothing was fetched.
  EXPECT_EQ(1, b.table.adds);
  EXPECT_EQ(1, b.restart.restarts);
  EXPECT_EQ(0, b.fetch.starts);
  EXPECT_EQ(AskStep::AddingSlot, b.record().step);
  EXPECT_EQ(std::vector<std::string>({"check", "adding-slot"}), b.steps());
  // The `check` entry says what the board was when the ask was taken, as the contract's sample
  // does: the table word and the running version.
  JournalEntry first;
  ASSERT_TRUE(b.record().journal.entry(0, first));
  EXPECT_STREQ("table one, 0.7.2+4a4e791 running", first.note);
  // And the `adding-slot` entry carries no note at all, which is what the sample has.
  JournalEntry second;
  ASSERT_TRUE(b.record().journal.entry(1, second));
  EXPECT_STREQ("", second.note);

  // ... the board restarts, and the ask goes on without being asked again.
  b.clock.advance(39);
  b.boot(kOld);
  EXPECT_EQ(AskStep::Fetching, b.record().step);
  EXPECT_EQ(1, b.fetch.starts);
  EXPECT_EQ(std::vector<std::string>({"check", "adding-slot", "fetching"}), b.steps());

  b.clock.advance(30);
  b.fetch.endWith(Ending::Fetched, 0, "fetched");
  b.asked.step();
  b.clock.advance(20);
  b.bootOnTrial(kNew);
  b.clock.advance(20);
  b.trial.commit();
  b.asked.step();

  EXPECT_EQ(AskOutcome::Committed, b.record().outcome);
  EXPECT_EQ(std::vector<std::string>({"check", "adding-slot", "fetching", "switching", "proving", "committing"}),
            b.steps());
}

// A table that is not `two` after that restart is a board that cannot say what happened.
TEST(TheWholeWay, ATableThatIsNotTwoAfterTheRestartEndsTheAskStuck) {
  Board b;
  b.table.wordNow = "one";
  b.table.wordAfterAdd = "one";  // the write took, the sector did not
  b.boot(kOld);
  b.hearTheAsk(17);
  b.asked.step();
  EXPECT_EQ(AskStep::AddingSlot, b.record().step);

  b.boot(kOld);
  const AskRecord rec = b.record();
  EXPECT_EQ(AskStep::Ended, rec.step);
  EXPECT_EQ(AskOutcome::Stuck, rec.outcome);
  EXPECT_EQ(AskRefusal::None, rec.refusal);
  EXPECT_EQ(0, b.fetch.starts);
  std::string body;
  EXPECT_TRUE(b.postTheResult(true, false, &body));
  EXPECT_NE(std::string::npos, body.find("\"outcome\":\"stuck\"")) << body;
  EXPECT_NE(std::string::npos, body.find("the table is `one` after the slot was added")) << body;
}

// A sector that would not be written at all: the same ending, and nothing is fetched.
TEST(TheWholeWay, ASectorThatCannotBeWrittenEndsTheAskStuckBeforeAnyRestart) {
  Board b;
  b.table.wordNow = "one";
  b.table.addWorks = false;
  b.table.addLine = "refused   the sector could not be erased";
  b.boot(kOld);
  b.hearTheAsk(17);
  b.asked.step();
  EXPECT_EQ(AskOutcome::Stuck, b.record().outcome);
  EXPECT_EQ(0, b.restart.restarts);
  EXPECT_EQ(0, b.fetch.starts);
}

// Nobody is at the console to read the sentence, so the journal says which of the two endings a
// write that did not add the slot had (task p1.16-63, deliverable 5). Nothing of the contract
// changes: the outcome is `stuck`, which it was already.
TEST(ASlotThatWasNotAdded, TheTableTheBoardHadIsSaidSoInTheJournalAndNothingRestarts) {
  Board b;
  b.table.wordNow = "one";
  b.table.addWorks = false;
  b.table.sectorHoldsATable = true;
  b.table.addLine =
      "the slot was NOT added: the new table would not hold in 3 tries and the old one is back in the sector after 1, "
      "so the board is as it was";
  b.boot(kOld);
  b.hearTheAsk(17);
  b.asked.step();

  const AskRecord rec = b.record();
  EXPECT_EQ(AskStep::Ended, rec.step);
  EXPECT_EQ(AskOutcome::Stuck, rec.outcome);
  EXPECT_EQ(AskRefusal::None, rec.refusal);
  EXPECT_EQ(1, b.table.adds);
  EXPECT_EQ(0, b.restart.restarts) << "a board is restarted only when the slot was added";
  EXPECT_EQ(0, b.fetch.starts);

  std::string body;
  EXPECT_TRUE(b.postTheResult(true, false, &body));
  EXPECT_NE(std::string::npos, body.find("\"outcome\":\"stuck\"")) << body;
  EXPECT_NE(std::string::npos, body.find("the table is the one the board had")) << body;
  EXPECT_EQ(std::string::npos, body.find("holds no partition table")) << body;

  // And the next ask is taken: this board is as it was, and nothing of it is in hand.
  b.hearTheAsk(18);
  b.asked.step();
  EXPECT_EQ(18, b.record().id);
  EXPECT_EQ(2, b.table.adds) << "the table is still `one`, so it is tried again";
}

TEST(ASlotThatWasNotAdded, ASectorThatHoldsNoTableIsSaidSoInTheJournalAndNothingRestarts) {
  Board b;
  b.table.wordNow = "one";
  b.table.addWorks = false;
  b.table.sectorHoldsATable = false;
  b.table.addLine =
      "the slot was NOT added and the sector holds NO table: do NOT cut the power - the old table is put back once a "
      "minute from now";
  b.boot(kOld);
  b.hearTheAsk(17);
  b.asked.step();

  const AskRecord rec = b.record();
  EXPECT_EQ(AskStep::Ended, rec.step);
  EXPECT_EQ(AskOutcome::Stuck, rec.outcome);
  EXPECT_EQ(0, b.restart.restarts) << "a restart is exactly what this board would not survive";
  EXPECT_EQ(0, b.fetch.starts);

  std::string body;
  EXPECT_TRUE(b.postTheResult(true, false, &body));
  EXPECT_NE(std::string::npos, body.find("\"outcome\":\"stuck\"")) << body;
  EXPECT_NE(std::string::npos, body.find("the sector holds no partition table")) << body;
}

// ---- the five refusals, each alone -----------------------------------------------------------

TEST(TheRefusals, AnUpdateAlreadyInHandIsInProgressAndNothingIsFetched) {
  Board b;
  b.boot(kOld);
  // Task 60's own record says a switch is in hand: somebody typed `update apply` a moment ago.
  b.trial.rec.phase = Phase::Switching;
  std::snprintf(b.trial.rec.from, sizeof(b.trial.rec.from), "%s", kOld);
  std::snprintf(b.trial.rec.to, sizeof(b.trial.rec.to), "%s", "0.9.0+deadbee");
  b.hearTheAsk(17);
  b.asked.step();

  const AskRecord rec = b.record();
  EXPECT_EQ(AskOutcome::Refused, rec.outcome);
  EXPECT_EQ(AskRefusal::InProgress, rec.refusal);
  EXPECT_EQ(0, b.fetch.starts);
  EXPECT_EQ(0, b.trial.applies);
  EXPECT_FALSE(b.asked.holdsJobs()) << "a refused ask holds nothing";
  std::string body;
  ASSERT_TRUE(b.postTheResult(true, false, &body));
  EXPECT_NE(std::string::npos, body.find("\"outcome\":\"refused\",")) << body;
  EXPECT_NE(std::string::npos, body.find("\"refusal\":\"inProgress\"")) << body;
  EXPECT_EQ(std::string::npos, body.find("\"check\":")) << body;
  // `fromVersion` and `toVersion` are both the version that is running: nothing was switched.
  EXPECT_NE(std::string::npos, body.find("\"fromVersion\":\"0.7.2+4a4e791\",\"toVersion\":\"0.7.2+4a4e791\"")) << body;
}

TEST(TheRefusals, AFirmwareOnTrialIsInProgressToo) {
  Board b;
  b.boot(kOld);
  b.trial.onTrialNow = true;
  b.hearTheAsk(17);
  b.asked.step();
  EXPECT_EQ(AskRefusal::InProgress, b.record().refusal);
  EXPECT_EQ(0, b.fetch.starts);
}

TEST(TheRefusals, AFetchAlreadyRunningIsInProgressToo) {
  Board b;
  b.boot(kOld);
  b.fetch.runningNow = true;  // `update fetch` at the console a moment ago
  b.hearTheAsk(17);
  b.asked.step();
  EXPECT_EQ(AskRefusal::InProgress, b.record().refusal);
  EXPECT_EQ(0, b.fetch.starts);
}

TEST(TheRefusals, ATableThisFirmwareWillNotWriteIsTableAndNothingIsFetched) {
  Board b;
  b.table.wordNow = "other";
  b.boot(kOld);
  b.hearTheAsk(17);
  b.asked.step();
  const AskRecord rec = b.record();
  EXPECT_EQ(AskOutcome::Refused, rec.outcome);
  EXPECT_EQ(AskRefusal::Table, rec.refusal);
  EXPECT_EQ(0, b.fetch.starts);
  EXPECT_EQ(0, b.table.adds);
  EXPECT_EQ(0, b.trial.applies);
  std::string body;
  ASSERT_TRUE(b.postTheResult(true, false, &body));
  EXPECT_NE(std::string::npos, body.find("\"refusal\":\"table\"")) << body;
  // The heartbeat of such a board says the word, so the server knows before it asks again.
  EXPECT_STREQ("other", b.member().table);
}

TEST(TheRefusals, TheVersionThatIsRunningIsCurrentAndNothingIsFetched) {
  Board b;
  b.boot(kNew);
  b.hearTheAsk(17, kNew);
  b.asked.step();
  const AskRecord rec = b.record();
  EXPECT_EQ(AskOutcome::Refused, rec.outcome);
  EXPECT_EQ(AskRefusal::Current, rec.refusal);
  EXPECT_EQ(0, b.fetch.starts);
  EXPECT_EQ(0, b.trial.applies);
  std::string body;
  ASSERT_TRUE(b.postTheResult(true, false, &body));
  EXPECT_NE(std::string::npos, body.find("\"refusal\":\"current\"")) << body;
}

// The server's catalogue may hold `0.8.0` where the image says `0.8.0+1a2b3c4`, and those are one
// release: a board running it refuses the press rather than fetching itself again.
TEST(TheRefusals, AVersionWithNoBuildMetadataIsStillTheOneThatRuns) {
  Board b;
  b.boot("0.8.0+1a2b3c4");
  b.hearTheAsk(17, "0.8.0");
  b.asked.step();
  EXPECT_EQ(AskRefusal::Current, b.record().refusal);

  // And two builds of the same version are not: the commit is part of the release.
  Board other;
  other.boot("0.8.0+1a2b3c4");
  other.hearTheAsk(18, "0.8.0+9999999");
  other.asked.step();
  EXPECT_EQ(AskStep::Fetching, other.record().step);
}

TEST(TheRefusals, AReleaseThatCannotBeFetchedIsFetchAndNothingIsSwitched) {
  Board b;
  b.boot(kOld);
  b.hearTheAsk(17);
  b.asked.step();
  b.fetch.endWith(Ending::Fetch, 0, "there is no such resource");
  b.asked.step();
  const AskRecord rec = b.record();
  EXPECT_EQ(AskOutcome::Refused, rec.outcome);
  EXPECT_EQ(AskRefusal::Fetch, rec.refusal);
  EXPECT_EQ(0, rec.check);
  EXPECT_EQ(0, b.trial.applies);
  std::string body;
  ASSERT_TRUE(b.postTheResult(true, false, &body));
  EXPECT_NE(std::string::npos, body.find("\"refusal\":\"fetch\"")) << body;
  EXPECT_EQ(std::string::npos, body.find("\"check\":")) << body;
  EXPECT_NE(std::string::npos, body.find("there is no such resource")) << body;
}

TEST(TheRefusals, ACheckThatFailedIsCheckWithItsNumberAndNothingIsSwitched) {
  for (uint8_t number = 1; number <= 5; number++) {
    Board b;
    b.boot(kOld);
    b.hearTheAsk(17);
    b.asked.step();
    char why[64];
    std::snprintf(why, sizeof(why), "the image is not this product for this board");
    b.fetch.endWith(Ending::Check, number, why);
    b.asked.step();
    const AskRecord rec = b.record();
    EXPECT_EQ(AskOutcome::Refused, rec.outcome);
    EXPECT_EQ(AskRefusal::Check, rec.refusal);
    EXPECT_EQ(number, rec.check);
    EXPECT_EQ(0, b.trial.applies);
    std::string body;
    ASSERT_TRUE(b.postTheResult(true, false, &body));
    char expected[32];
    std::snprintf(expected, sizeof(expected), "\"check\":%u", static_cast<unsigned>(number));
    EXPECT_NE(std::string::npos, body.find("\"refusal\":\"check\"")) << body;
    EXPECT_NE(std::string::npos, body.find(expected)) << body;
  }
}

// A fetch is not begun before the server can be reached: a board that resumes one at its own
// boot has not joined the WiFi yet, and three tries in three passes of the loop would end the ask
// for nothing.
TEST(TheRefusals, AFetchWaitsForTheLinkRatherThanSpendingItsTries) {
  Board b;
  b.fetch.readyNow = false;
  b.boot(kOld);
  b.hearTheAsk(17);
  b.asked.step();
  EXPECT_EQ(AskStep::Fetching, b.record().step) << "the step is written down all the same";
  EXPECT_EQ(0, b.fetch.starts) << "and nothing was asked for";
  b.pump(5);
  EXPECT_EQ(0, b.fetch.starts);
  EXPECT_TRUE(b.asked.inHand()) << "the ask is not refused for want of a link";

  // The link comes up, and the one request goes out.
  b.fetch.readyNow = true;
  b.asked.step();
  EXPECT_EQ(1, b.fetch.starts);
  b.fetch.endWith(Ending::Fetched, 0, "fetched");
  b.asked.step();
  EXPECT_EQ(1, b.trial.applies);
}

// And a fetch that a restart interrupted begins again at the boot after it, once the link is
// there: a fetch does not go on over a restart, and over WiFi a file is seconds.
TEST(TheRefusals, AFetchThatARestartInterruptedBeginsAgainAtTheNextBoot) {
  Board b;
  b.boot(kOld);
  b.hearTheAsk(17);
  b.asked.step();
  ASSERT_EQ(1, b.fetch.starts);
  ASSERT_TRUE(b.fetch.runningNow);

  // ... the board restarts in the middle of it.
  b.fetch.runningNow = false;
  b.fetch.res = FetchResult();
  b.clock.advance(30);
  b.boot(kOld);
  EXPECT_EQ(AskStep::Fetching, b.record().step);
  EXPECT_EQ(1, b.fetch.starts) << "once at this boot";
  EXPECT_EQ(std::vector<std::string>({"check", "fetching", "fetching"}), b.steps())
      << "and the journal says it began again";
  b.fetch.endWith(Ending::Fetched, 0, "fetched");
  b.asked.step();
  EXPECT_EQ(1, b.trial.applies);
}

// A board with no second slot after all - which the table word should have caught - is the one
// fetch ending that is not a refusal of this contract: the board cannot say what happened.
TEST(TheRefusals, AFetchThatRefusesForWantOfASlotIsStuck) {
  Board b;
  b.boot(kOld);
  b.hearTheAsk(17);
  b.asked.step();
  b.fetch.refuseStartWith(Ending::Slot, 0, "this board has no second application slot");
  b.asked.step();
  EXPECT_EQ(AskOutcome::Stuck, b.record().outcome);
  EXPECT_EQ(0, b.trial.applies);
}

// A switch that the framework refused: nothing was switched and nothing is claimed about it.
TEST(TheRefusals, ASwitchThatWasRefusedIsStuck) {
  Board b;
  b.boot(kOld);
  b.hearTheAsk(17);
  b.asked.step();
  b.fetch.endWith(Ending::Fetched, 0, "fetched");
  b.trial.refuse = "the framework refused the boot choice, so nothing is switched";
  b.asked.step();
  EXPECT_EQ(1, b.trial.applies);
  EXPECT_EQ(0, b.restart.restarts);
  const AskRecord rec = b.record();
  EXPECT_EQ(AskOutcome::Stuck, rec.outcome);
  std::string body;
  ASSERT_TRUE(b.postTheResult(true, false, &body));
  EXPECT_NE(std::string::npos, body.find("the framework refused the boot choice")) << body;
}

// ---- a trial that fails ----------------------------------------------------------------------

TEST(ATrialThatFails, IsOneResultRolledBackPostedByTheOldImageWithTheOldVersion) {
  Board b;
  b.boot(kOld);
  b.hearTheAsk(17);
  b.asked.step();
  b.clock.advance(40);
  b.fetch.endWith(Ending::Fetched, 0, "fetched");
  b.asked.step();

  // The new image boots and is on trial; ten minutes pass without a heartbeat.
  b.clock.advance(20);
  b.bootOnTrial(kNew);
  EXPECT_EQ(AskStep::Proving, b.record().step);
  b.clock.advance(600);
  b.trial.rollBack("no heartbeat answered in 10 min");
  // The record is **not** finished by the image on trial: the boot after it finishes it.
  EXPECT_EQ(Phase::OnTrial, mm::update::loadUpdate(b.store).phase);
  b.asked.step();
  EXPECT_EQ(AskStep::Proving, b.record().step) << "the ask waits for the boot that finishes the record";

  // ... the bootloader puts the old image back, and its boot ends both records.
  b.clock.advance(20);
  b.bootBack(kOld);
  const AskRecord rec = b.record();
  EXPECT_EQ(AskStep::Ended, rec.step);
  EXPECT_EQ(AskOutcome::RolledBack, rec.outcome);
  EXPECT_EQ(AskRefusal::None, rec.refusal);
  EXPECT_STREQ(kOld, rec.from);
  EXPECT_STREQ(kOld, rec.to) << "toVersion is what runs when the result is written";
  EXPECT_EQ(std::vector<std::string>({"check", "fetching", "switching", "proving", "rolling-back", "restored"}),
            b.steps());

  std::string body;
  ASSERT_TRUE(b.postTheResult(true, false, &body));
  EXPECT_NE(std::string::npos, body.find("\"outcome\":\"rolledBack\"")) << body;
  EXPECT_NE(std::string::npos, body.find("\"fromVersion\":\"0.7.2+4a4e791\",\"toVersion\":\"0.7.2+4a4e791\"")) << body;
  EXPECT_NE(std::string::npos, body.find("no heartbeat answered in 10 min")) << body;
  EXPECT_EQ(std::string::npos, body.find("\"refusal\"")) << body
      << "no real result has a refusal beside rolledBack";
  EXPECT_FALSE(b.postTheResult(true, false));
}

// And a trial that failed but could not be sent back is **not** `rolledBack`: nothing was rolled
// back, and the firmware that failed is the one running. `stuck` is the contract's word for a
// board that cannot say what happened, and the result carries the version that runs (task
// p1.16-64, deliverable 5). No outcome is added.
TEST(ATrialThatFails, AndCouldNotBeSentBackIsOneResultStuckWithTheVersionThatRuns) {
  Board b;
  b.boot(kOld);
  b.hearTheAsk(17);
  b.asked.step();
  b.clock.advance(40);
  b.fetch.endWith(Ending::Fetched, 0, "fetched");
  b.asked.step();

  b.clock.advance(20);
  b.bootOnTrial(kNew);
  ASSERT_EQ(AskStep::Proving, b.record().step);
  b.clock.advance(600);
  b.trial.rollBack("no heartbeat answered in 10 min");
  b.asked.step();
  EXPECT_EQ(AskStep::Proving, b.record().step) << "the ask waits for the boot that finishes the record";

  // The restart came back as the same image, and nothing could send the board back.
  b.clock.advance(20);
  b.bootStuck(kNew, "no server: no heartbeat was answered, and the board could not be sent back");
  const AskRecord rec = b.record();
  EXPECT_EQ(AskStep::Ended, rec.step);
  EXPECT_EQ(AskOutcome::Stuck, rec.outcome);
  EXPECT_EQ(AskRefusal::None, rec.refusal);
  EXPECT_STREQ(kOld, rec.from);
  EXPECT_STREQ(kNew, rec.to) << "toVersion is the version that runs, which is the one that failed";

  std::string body;
  ASSERT_TRUE(b.postTheResult(true, false, &body));
  EXPECT_NE(std::string::npos, body.find("\"outcome\":\"stuck\"")) << body;
  EXPECT_EQ(std::string::npos, body.find("rolledBack")) << body << "nothing was rolled back";
  EXPECT_NE(std::string::npos, body.find("could not be sent back")) << body;
  EXPECT_EQ(std::string::npos, body.find("\"refusal\"")) << body;
  EXPECT_FALSE(b.postTheResult(true, false));
}

// ---- the same id, again and again ------------------------------------------------------------

TEST(TheSameId, ThreeRepliesWhileItIsInHandAreOneUpdate) {
  Board b;
  b.boot(kOld);
  b.hearTheAsk(17);
  b.asked.step();
  EXPECT_EQ(1, b.fetch.starts);

  for (int i = 0; i < 3; i++) {
    b.hearTheAsk(17);
    b.asked.step();
    b.asked.step();
  }
  EXPECT_EQ(1, b.fetch.starts) << "the same id while it is in hand changes nothing";
  EXPECT_EQ(AskStep::Fetching, b.record().step);
  EXPECT_EQ(std::vector<std::string>({"check", "fetching"}), b.steps());
  EXPECT_TRUE(b.lines.holds("is the ask already in hand"));
}

TEST(TheSameId, AfterTheResultWasTakenItIsPostedOnceMoreAndNothingElseHappens) {
  Board b;
  b.table.wordNow = "other";
  b.boot(kOld);
  b.hearTheAsk(17);
  b.asked.step();
  ASSERT_EQ(AskOutcome::Refused, b.record().outcome);
  ASSERT_TRUE(b.postTheResult(true, false));
  ASSERT_EQ(AskPosted::Taken, b.record().posted);

  b.hearTheAsk(17);
  b.asked.step();
  EXPECT_EQ(AskPosted::Waiting, b.record().posted);
  EXPECT_EQ(AskStep::Ended, b.record().step) << "nothing else happens: no step is taken again";
  EXPECT_EQ(0, b.fetch.starts);
  EXPECT_EQ(0, b.table.adds);
  EXPECT_FALSE(b.asked.holdsJobs());
  std::string body;
  EXPECT_TRUE(b.postTheResult(true, false, &body));
  EXPECT_NE(std::string::npos, body.find("\"refusal\":\"table\"")) << "the same result, not a new one";
  EXPECT_EQ(AskPosted::Taken, b.record().posted);
}

// A different id while the one before it is still in hand, or its result still waiting, is not
// taken now: the server asks again in every reply until it has a result, so nothing is lost.
TEST(TheSameId, ADifferentIdIsNotTakenWhileOneIsInHandOrItsResultWaits) {
  Board b;
  b.boot(kOld);
  b.hearTheAsk(17);
  b.asked.step();
  ASSERT_EQ(AskStep::Fetching, b.record().step);

  b.hearTheAsk(18);
  b.asked.step();
  EXPECT_EQ(17, b.record().id) << "the ask in hand is not replaced";
  EXPECT_TRUE(b.lines.holds("another ask is in hand"));

  // Its result waits: still not taken.
  b.fetch.endWith(Ending::Fetch, 0, "no reply");
  b.asked.step();
  ASSERT_EQ(AskPosted::Waiting, b.record().posted);
  b.hearTheAsk(18);
  b.asked.step();
  EXPECT_EQ(17, b.record().id);

  // The result is taken, and then the next ask is taken too.
  ASSERT_TRUE(b.postTheResult(true, false));
  b.hearTheAsk(18);
  b.asked.step();
  EXPECT_EQ(18, b.record().id);
  EXPECT_EQ(AskStep::Fetching, b.record().step);
}

// ---- the result's post -----------------------------------------------------------------------

TEST(TheResultsPost, FailsTwiceThenIsTakenAndARestartBetweenDoesNotLoseIt) {
  Board b;
  b.table.wordNow = "other";
  b.boot(kOld);
  b.hearTheAsk(17);
  b.asked.step();
  ASSERT_EQ(AskPosted::Waiting, b.record().posted);

  // Two posts that did not go through: neither taken nor the 404.
  EXPECT_TRUE(b.postTheResult(false, false));
  EXPECT_EQ(AskPosted::Waiting, b.record().posted);
  EXPECT_TRUE(b.postTheResult(false, false));
  EXPECT_EQ(AskPosted::Waiting, b.record().posted);

  // ... the board restarts, and the result is still waiting.
  b.boot(kOld);
  EXPECT_EQ(AskPosted::Waiting, b.record().posted);
  EXPECT_TRUE(b.postTheResult(true, false));
  EXPECT_EQ(AskPosted::Taken, b.record().posted);
  EXPECT_FALSE(b.postTheResult(true, false)) << "three posts, and then none";
}

TEST(TheResultsPost, AnswerToTheOneFourOhFourIsNotPostedAgainEverAfterARestart) {
  Board b;
  b.table.wordNow = "other";
  b.boot(kOld);
  b.hearTheAsk(17);
  b.asked.step();
  EXPECT_TRUE(b.postTheResult(false, true));
  EXPECT_EQ(AskPosted::Gone, b.record().posted);
  EXPECT_FALSE(b.postTheResult(true, false));

  b.boot(kOld);
  EXPECT_FALSE(b.postTheResult(true, false));
  // And the same id again does not bring it back: the server does not know this update.
  b.hearTheAsk(17);
  b.asked.step();
  EXPECT_EQ(AskPosted::Gone, b.record().posted);
  EXPECT_FALSE(b.postTheResult(true, false));
}

// An answer to another id does nothing at all: the board posts one result at a time.
TEST(TheResultsPost, AnAnswerForAnotherIdIsIgnored) {
  Board b;
  b.table.wordNow = "other";
  b.boot(kOld);
  b.hearTheAsk(17);
  b.asked.step();
  b.asked.onResultPosted(999, true, false);
  EXPECT_EQ(AskPosted::Waiting, b.record().posted);
}

// The bytes, for the sample the contract's own line carries (deliverable: the result the firmware
// writes for that sample is `UpdateResult.json` byte for byte).
TEST(TheResultsPost, TheFixturesSampleIsWrittenByteForByte) {
  const std::string fixture = contractFixture("UpdateResult.json");
  ASSERT_FALSE(fixture.empty())
      << "tests/fixtures/contracts/UpdateResult.json is missing - run tools/sync-fixtures.ps1";

  // The record that sample is: an ask that was refused at check 4 - which is the shape the line
  // was written to carry every field of - with the journal of a whole run.
  Board b;
  AskRecord rec;
  rec.step = AskStep::Ended;
  rec.id = 17;
  rec.releaseId = 9;
  std::snprintf(rec.version, sizeof(rec.version), "0.8.0");
  std::snprintf(rec.base, sizeof(rec.base), "/agent/v1/firmware/9");
  rec.size = 1432000;
  std::snprintf(rec.from, sizeof(rec.from), "0.7.2");
  std::snprintf(rec.to, sizeof(rec.to), "0.7.2");
  rec.outcome = AskOutcome::RolledBack;
  rec.refusal = AskRefusal::Check;
  rec.check = 4;
  rec.posted = AskPosted::Waiting;
  rec.journal.add(1795165200u, "check", "table one, 0.7.2 running");
  rec.journal.add(1795165201u, "adding-slot", "");
  rec.journal.add(1795165240u, "fetching", "0.8.0, 1432000 bytes");
  rec.journal.add(1795165270u, "switching", "");
  rec.journal.add(1795165290u, "proving", "mesh up, wifi up");
  rec.journal.add(1795165890u, "rolling-back", "no heartbeat answered in 10 min");
  rec.journal.add(1795165910u, "restored", "0.7.2 running");
  ASSERT_TRUE(mm::update::saveAsk(b.store, rec));

  b.boot("0.7.2");
  std::string body;
  ASSERT_TRUE(b.postTheResult(true, false, &body));
  EXPECT_EQ(fixture, body);
}

// A journal longer than the body's buffer is shortened from its oldest end rather than lost: the
// end of a journal is what an outcome is read against.
TEST(TheResultsPost, AJournalTooLongForTheBodyIsShortenedFromTheOldestEnd) {
  Board b;
  AskRecord rec;
  rec.step = AskStep::Ended;
  rec.id = 17;
  std::snprintf(rec.version, sizeof(rec.version), "0.8.0");
  std::snprintf(rec.from, sizeof(rec.from), "0.7.2");
  std::snprintf(rec.to, sizeof(rec.to), "0.7.2");
  rec.outcome = AskOutcome::RolledBack;
  rec.posted = AskPosted::Waiting;
  for (int i = 0; i < 40; i++) {
    char note[64];
    std::snprintf(note, sizeof(note), "entry %d %s", i, std::string(40, 'x').c_str());
    rec.journal.add(1795165200u + static_cast<uint32_t>(i), "proving", note);
  }
  ASSERT_TRUE(mm::update::saveAsk(b.store, rec));
  b.boot("0.7.2");

  std::string body;
  ASSERT_TRUE(b.postTheResult(true, false, &body));
  EXPECT_LE(body.size(), 2048u);
  EXPECT_EQ(std::string::npos, body.find("entry 0 ")) << "the oldest went";
  EXPECT_NE(std::string::npos, body.find("entry 39 ")) << "the newest stayed";
  EXPECT_TRUE(b.lines.holds("shortened"));
}

// ---- the heartbeat's member ------------------------------------------------------------------

TEST(TheHeartbeat, SaysTheTableWordAndTheAskInHandWithItsNewestStep) {
  Board b;
  b.table.wordNow = "two";
  b.boot(kOld);
  mm::HeartbeatUpdate m = b.member();
  EXPECT_STREQ("two", m.table);
  EXPECT_FALSE(m.hasAsk);

  b.hearTheAsk(17);
  b.asked.step();
  m = b.member();
  EXPECT_TRUE(m.hasAsk);
  EXPECT_EQ(17, m.id);
  EXPECT_STREQ("fetching", m.step);

  b.fetch.endWith(Ending::Fetched, 0, "fetched");
  b.asked.step();
  m = b.member();
  EXPECT_STREQ("switching", m.step);

  b.bootOnTrial(kNew);
  EXPECT_STREQ("proving", b.member().step);
}

// A build that trusts no release key: no member at all, and an ask is answered with nothing.
TEST(TheHeartbeat, ABuildWithNoReleaseKeyWritesNoMemberAndTakesNoAsk) {
  Board b;
  b.canBeUpdated = false;
  b.boot(kOld);
  mm::HeartbeatUpdate m;
  EXPECT_FALSE(b.asked.describeUpdate(m)) << "no member is written at all";

  b.hearTheAsk(17);
  b.asked.step();
  b.asked.step();
  EXPECT_FALSE(b.asked.inHand());
  EXPECT_FALSE(b.record().present()) << "nothing of the ask was written down";
  EXPECT_EQ(0, b.fetch.starts);
  EXPECT_EQ(0, b.table.adds);
  EXPECT_EQ(0, b.trial.applies);
  long long id = 0;
  const char* body = nullptr;
  size_t len = 0;
  EXPECT_FALSE(b.asked.resultToPost(id, body, len)) << "and no result is posted";
  EXPECT_FALSE(b.asked.holdsJobs());
}

// ---- the trial's third condition -------------------------------------------------------------

// Deliverable 5: while an ask is in hand the third condition is a heartbeat that **carried
// `proving`** and was answered 2xx, and nothing else.
TEST(TheThirdCondition, IsAHeartbeatThatCarriedProvingAndWasAnswered) {
  Board b;
  b.boot(kOld);
  b.hearTheAsk(17);
  b.asked.step();
  b.fetch.endWith(Ending::Fetched, 0, "fetched");
  b.asked.step();
  b.bootOnTrial(kNew);

  // A heartbeat has been answered since boot, but not one that carried `proving`.
  EXPECT_FALSE(b.asked.heartbeatProvesTheTrial(true))
      << "any answered heartbeat is not enough while the server's own ask is in hand";

  // One that carried the wrong step is not enough either.
  mm::HeartbeatUpdate earlier;
  earlier.table = "two";
  earlier.hasAsk = true;
  earlier.id = 17;
  earlier.step = "fetching";
  b.asked.onHeartbeatAnswered(earlier);
  EXPECT_FALSE(b.asked.heartbeatProvesTheTrial(true));

  // The member the loop wrote for this heartbeat, answered: that is the condition.
  const mm::HeartbeatUpdate posted = b.member();
  ASSERT_STREQ("proving", posted.step);
  b.asked.onHeartbeatAnswered(posted);
  EXPECT_TRUE(b.asked.heartbeatProvesTheTrial(false)) << "and it does not need `everHeartbeated` at all";
}

// With no ask in hand - a switch made at the console - it is task 60's own rule, unchanged.
TEST(TheThirdCondition, WithNoAskInHandItIsAnyAnsweredHeartbeat) {
  Board b;
  b.boot(kOld);
  EXPECT_FALSE(b.asked.heartbeatProvesTheTrial(false));
  EXPECT_TRUE(b.asked.heartbeatProvesTheTrial(true));
}

// The latch is RAM and not the store: a trial is one boot, and a heartbeat answered before the
// restart proves nothing about the image that is running now.
TEST(TheThirdCondition, DoesNotSurviveARestart) {
  Board b;
  b.boot(kOld);
  b.hearTheAsk(17);
  b.asked.step();
  b.fetch.endWith(Ending::Fetched, 0, "fetched");
  b.asked.step();
  b.bootOnTrial(kNew);
  const mm::HeartbeatUpdate posted = b.member();
  ASSERT_STREQ("proving", posted.step);
  b.asked.onHeartbeatAnswered(posted);
  EXPECT_TRUE(b.asked.heartbeatProvesTheTrial(false));

  // ... the board restarts in the middle of the trial. What the boot before it reached the server
  // with proves nothing about the image that is running now, so the latch is gone.
  b.begin();
  EXPECT_FALSE(b.asked.heartbeatProvesTheTrial(true));
}

// A heartbeat that carried no ask at all - a board with nothing in hand - does not latch it
// either: the member has to be the ask's own.
TEST(TheThirdCondition, AHeartbeatWithNoAskInItLatchesNothing) {
  Board b;
  b.boot(kOld);
  b.hearTheAsk(17);
  b.asked.step();
  b.fetch.endWith(Ending::Fetched, 0, "fetched");
  b.asked.step();
  b.bootOnTrial(kNew);
  mm::HeartbeatUpdate bare;
  bare.table = "two";
  bare.hasAsk = false;
  bare.step = "proving";
  b.asked.onHeartbeatAnswered(bare);
  EXPECT_FALSE(b.asked.heartbeatProvesTheTrial(true));
}

// ---- no job while an update is in hand -------------------------------------------------------

TEST(NoJob, IsClaimedFromCheckPassedToTheEnd) {
  Board b;
  b.boot(kOld);
  EXPECT_FALSE(b.asked.holdsJobs());

  b.hearTheAsk(17);
  EXPECT_FALSE(b.asked.holdsJobs()) << "the ask has not been taken yet: the job that is running finishes";
  b.asked.step();
  EXPECT_TRUE(b.asked.holdsJobs());

  b.fetch.endWith(Ending::Fetched, 0, "fetched");
  b.asked.step();
  EXPECT_TRUE(b.asked.holdsJobs());
  b.bootOnTrial(kNew);
  EXPECT_TRUE(b.asked.holdsJobs()) << "and across the restart";
  b.trial.commit();
  b.asked.step();
  EXPECT_FALSE(b.asked.holdsJobs());

  // The result still waiting is not a reason to hold a job: the ask has ended.
  EXPECT_EQ(AskPosted::Waiting, b.record().posted);
  EXPECT_FALSE(b.asked.holdsJobs());
}

// ---- the record in the store -----------------------------------------------------------------

TEST(TheRecord, TheStepIsWrittenLastOnTheWayInAndClearedFirstOnTheWayOut) {
  Board b;
  b.boot(kOld);
  b.hearTheAsk(17);
  b.asked.step();
  // Every write of the step key went into the order list; the first of them came after the ask's
  // own fields were there.
  ASSERT_FALSE(b.order.empty());
  EXPECT_EQ("ask:check", b.order[0]);
  EXPECT_EQ("17", b.store.value(mm::update::kAskKeyId));
  EXPECT_EQ("9", b.store.value(mm::update::kAskKeyRelease));
  EXPECT_EQ("0.8.0+1a2b3c4", b.store.value(mm::update::kAskKeyVersion));
  EXPECT_EQ("/agent/v1/firmware/9", b.store.value(mm::update::kAskKeyBase));
  EXPECT_EQ("1432000", b.store.value(mm::update::kAskKeyLength));
  EXPECT_EQ(kOld, b.store.value(mm::update::kAskKeyFrom));
  EXPECT_EQ("3b1f0c4e5d6a79880817a6b5c4d3e2f1a0b9c8d7e6f5041322314051607f8e9d",
            b.store.value(mm::update::kAskKeySha));

  // And forgetting it forgets all fourteen keys, the step first.
  AskRecord nothing;
  b.order.clear();
  ASSERT_TRUE(mm::update::saveAsk(b.store, nothing));
  ASSERT_FALSE(b.order.empty());
  EXPECT_EQ("ask:none", b.order[0]);
  for (const char* key : {mm::update::kAskKeyStep, mm::update::kAskKeyId, mm::update::kAskKeyRelease,
                          mm::update::kAskKeyVersion, mm::update::kAskKeyBase, mm::update::kAskKeySha,
                          mm::update::kAskKeyLength, mm::update::kAskKeyFrom, mm::update::kAskKeyTo,
                          mm::update::kAskKeyOutcome, mm::update::kAskKeyRefusal, mm::update::kAskKeyCheck,
                          mm::update::kAskKeyPosted, mm::update::kAskKeyJournal}) {
    EXPECT_FALSE(b.store.has(key)) << key;
  }
  EXPECT_FALSE(mm::update::loadAsk(b.store).present());
}

TEST(TheRecord, AHalfWrittenOneIsNotAnAsk) {
  Board b;
  b.table.wordNow = "other";
  b.boot(kOld);
  b.hearTheAsk(17);
  b.asked.step();
  const std::map<std::string, std::string> whole = b.store.values;
  ASSERT_TRUE(mm::update::loadAsk(b.store).present());

  // A step word this firmware never wrote.
  b.store.values = whole;
  b.store.values[mm::update::kAskKeyStep] = "halfway";
  EXPECT_FALSE(mm::update::loadAsk(b.store).present());

  // An ask with no id, and one with no version to fetch.
  b.store.values = whole;
  b.store.values.erase(mm::update::kAskKeyId);
  EXPECT_FALSE(mm::update::loadAsk(b.store).present());
  b.store.values = whole;
  b.store.values.erase(mm::update::kAskKeyVersion);
  EXPECT_FALSE(mm::update::loadAsk(b.store).present());

  // A `done` with no outcome, and one with no account of its result.
  b.store.values = whole;
  b.store.values.erase(mm::update::kAskKeyOutcome);
  EXPECT_FALSE(mm::update::loadAsk(b.store).present());
  b.store.values = whole;
  b.store.values.erase(mm::update::kAskKeyPosted);
  EXPECT_FALSE(mm::update::loadAsk(b.store).present());
}

TEST(TheRecord, RoundTripsThroughTheStore) {
  MapStore store;
  AskRecord rec;
  rec.step = AskStep::Proving;
  rec.id = 4294967296LL;  // a long long and not an int
  rec.releaseId = 123456789LL;
  std::snprintf(rec.version, sizeof(rec.version), "0.9.0+abcdef1");
  std::snprintf(rec.base, sizeof(rec.base), "/agent/v1/firmware/123456789");
  std::snprintf(rec.sha256hex, sizeof(rec.sha256hex), "%s", std::string(64, 'a').c_str());
  rec.size = 1446640;
  std::snprintf(rec.from, sizeof(rec.from), "0.8.0+1111111");
  rec.posted = AskPosted::None;
  rec.journal.add(1795165200u, "check", "table two, 0.8.0+1111111 running");
  ASSERT_TRUE(mm::update::saveAsk(store, rec));

  const AskRecord back = mm::update::loadAsk(store);
  ASSERT_TRUE(back.present());
  EXPECT_TRUE(back.inHand());
  EXPECT_EQ(AskStep::Proving, back.step);
  EXPECT_EQ(4294967296LL, back.id);
  EXPECT_EQ(123456789LL, back.releaseId);
  EXPECT_STREQ("0.9.0+abcdef1", back.version);
  EXPECT_STREQ("/agent/v1/firmware/123456789", back.base);
  EXPECT_EQ(std::string(64, 'a'), std::string(back.sha256hex));
  EXPECT_EQ(1446640u, back.size);
  EXPECT_STREQ("0.8.0+1111111", back.from);
  EXPECT_EQ(1u, back.journal.count());
}

// ---- what the console says -------------------------------------------------------------------

TEST(TheConsole, SaysNothingWhenNothingWasAskedForAndTheStepWhileOneIsInHand) {
  Board b;
  b.boot(kOld);
  Lines nothing;
  b.asked.report(nothing);
  EXPECT_NE(std::string::npos, nothing.all.find("asked     nothing"));
  EXPECT_EQ(nullptr, b.asked.whyNotNow());

  b.hearTheAsk(17);
  b.asked.step();
  Lines inHand;
  b.asked.report(inHand);
  EXPECT_NE(std::string::npos, inHand.all.find("update 17 in hand at `fetching`")) << inHand.all;
  EXPECT_NE(std::string::npos, inHand.all.find("no job is claimed")) << inHand.all;
  ASSERT_NE(nullptr, b.asked.whyNotNow());
  EXPECT_NE(std::string::npos, std::string(b.asked.whyNotNow()).find("is in hand"));

  b.fetch.endWith(Ending::Check, 3, "the image is for heltec_v4");
  b.asked.step();
  Lines ended;
  b.asked.report(ended);
  EXPECT_NE(std::string::npos, ended.all.find("update 17 ended refused, refused check")) << ended.all;
  EXPECT_NE(std::string::npos, ended.all.find("check 3 of five")) << ended.all;
  EXPECT_NE(std::string::npos, ended.all.find("goes out at the next heartbeat")) << ended.all;
  EXPECT_EQ(nullptr, b.asked.whyNotNow());

  ASSERT_TRUE(b.postTheResult(true, false));
  Lines taken;
  b.asked.report(taken);
  EXPECT_NE(std::string::npos, taken.all.find("its result is with the server")) << taken.all;
}

TEST(TheConsole, SaysSoForABuildWithNoReleaseKey) {
  Board b;
  b.canBeUpdated = false;
  b.boot(kOld);
  Lines lines;
  b.asked.report(lines);
  EXPECT_NE(std::string::npos, lines.all.find("trusts no release key")) << lines.all;
}

// PlatformIO's googletest integration links no gtest_main, so every test file brings its own.
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
