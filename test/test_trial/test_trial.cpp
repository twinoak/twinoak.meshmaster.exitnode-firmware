// Switched, proved, or back (task p1.15-60, src/update/Trial.h and src/update/UpdateRecord.h).
//
// No test here can write a board's boot choice, boot a second slot or pull its power, so all four
// of the machine's seams are scripted: slots whose running index, rollback state and verdicts a
// test sets, a clock that moves only when a test says so, a restart that **returns** instead of
// restarting - so that a test can go on and look at what was left behind - and a record store that
// is a map and outlives a "reboot", which is what makes a trial across two and three boots a thing
// a test can drive at all.
//
// Every fake pushes a word into one shared list as it is called, which is how the order of the
// three things a switch does - the record, then the boot choice, then the restart - is held rather
// than described.
//
// The bytes in the slot are the release of task 58's own tooling, from tests/fixtures/release/:
// a real 4 kB image of this product with a real descriptor and a real SHA-256, so that the
// re-read of the hash before a switch and the reading of a descriptor out of the first slot are
// run over bytes a release really produced. A second version of it is made here by rewriting the
// descriptor's version field and its crc32, which is what an image flashed over USB looks like
// to a board that has never run it.

#include <gtest/gtest.h>

#include <SHA256.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include <agent/TextOut.h>

#include "update/Descriptor.h"
#include "update/Release.h"
#include "update/SlotRecord.h"
#include "update/Trial.h"
#include "update/UpdateRecord.h"

#ifndef RELEASE_FIXTURES_DIR
#error "RELEASE_FIXTURES_DIR must be defined; platformio.ini's [env:native] does it"
#endif

namespace {

namespace fs = std::filesystem;

using mm::update::BootFinding;
using mm::update::Condition;
using mm::update::Ended;
using mm::update::JournalEntry;
using mm::update::kTrialMs;
using mm::update::Phase;
using mm::update::SlotRecord;
using mm::update::SlotState;
using mm::update::UpdateRecord;
using mm::update::Updater;

// The version the fixture release's image carries inside itself, and the one the tests pretend is
// running before a switch.
const char* const kNewVersion = "0.0.0-test+1a2b3c4";
const char* const kOldVersion = "0.0.0-old+9f8e7d6";
const char* const kBoard = "heltec_v3";

// -- the fakes ---------------------------------------------------------------------------------

// The record store: a map, which is what makes it outlive a "reboot". Every write of the state
// key goes into the order list, because that key is written last and so is the moment the record
// becomes a record.
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
    if (order != nullptr && std::strcmp(key, mm::update::kUpdateKeyState) == 0) {
      order->push_back(std::string("record:") + (value == nullptr || value[0] == '\0' ? "none" : value));
    }
    return true;
  }

  bool has(const char* key) const { return values.find(key) != values.end(); }
  std::string value(const char* key) const {
    const auto it = values.find(key);
    return it == values.end() ? std::string() : it->second;
  }
};

// The two application slots, the boot choice and the two rollback marks.
class ScriptedSlots final : public mm::update::BootSlots {
 public:
  bool there = true;
  const char* reason = "app1 at 0x350000, 0x320000 bytes";
  int running = 0;
  SlotState state = SlotState::Undefined;
  bool otherVerifies = false;
  uint32_t otherLength = 0;
  bool otherReadable = true;  // describeOther() itself answering no
  bool bootOtherWorks = true;
  bool markValidWorks = true;
  bool markInvalidWorks = true;
  std::vector<std::string>* order = nullptr;

  int boots = 0;
  int valids = 0;
  int invalids = 0;

  bool available() const override { return there; }
  const char* why() const override { return reason; }
  int runningSlot() const override { return running; }
  SlotState runningState() const override { return state; }

  bool describeOther(mm::update::SlotImage& out) const override {
    out = mm::update::SlotImage();
    if (!otherReadable) return false;
    out.verifies = otherVerifies;
    out.length = otherLength;
    return true;
  }

  bool bootOther() override {
    if (!bootOtherWorks) return false;
    boots++;
    if (order != nullptr) order->push_back("boot-other");
    return true;
  }

  bool markValid() override {
    if (!markValidWorks) return false;
    valids++;
    if (order != nullptr) order->push_back("mark-valid");
    return true;
  }

  bool markInvalidAndRestart() override {
    if (!markInvalidWorks) return false;
    invalids++;
    if (order != nullptr) order->push_back("mark-invalid-and-restart");
    return true;
  }
};

// The bytes of the slot that is not running. Only the reads matter here: the writes are task 59's.
class ScriptedSlot final : public mm::update::SlotSink {
 public:
  std::vector<uint8_t> bytes;
  bool readFails = false;

  bool available() const override { return true; }
  const char* why() const override { return "app1"; }
  uint32_t capacity() const override { return 0x320000; }
  bool begin(uint32_t) override { return false; }
  uint32_t held() const override { return 0; }
  bool write(const uint8_t*, size_t) override { return false; }
  bool restart() override { return false; }
  bool finish() override { return false; }
  bool spoil() override { return false; }

  bool read(uint32_t offset, uint8_t* out, uint32_t len) const override {
    if (readFails) return false;
    if (offset > bytes.size() || len > bytes.size() - offset) return false;
    std::memcpy(out, bytes.data() + offset, len);
    return true;
  }
};

class ScriptedConditions final : public mm::update::TrialConditions {
 public:
  bool mesh = false;
  bool link = false;
  bool server = false;
  bool meshStarted() const override { return mesh; }
  bool linkAndClock() const override { return link; }
  bool heartbeatAnswered() const override { return server; }
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
    if (order != nullptr) order->push_back("note");
  }
  void postWhatWaits() override {
    posts++;
    if (order != nullptr) order->push_back("post-what-waits");
  }
  void restart() override {
    restarts++;
    if (order != nullptr) order->push_back("restart");
  }
};

class ScriptedClock final : public mm::update::TrialClock {
 public:
  uint32_t nowMs = 1000;
  uint32_t nowEpoch = 1795165200;  // 2026-11-20T09:00:00Z
  uint32_t ms() const override { return nowMs; }
  uint32_t epoch() const override { return nowEpoch; }
  void advanceMs(uint32_t by) {
    nowMs += by;
    nowEpoch += by / 1000u;
  }
};

class Lines final : public mm::TextOut {
 public:
  std::string all;
  void write(const char* text, size_t len) override { all.append(text, len); }
  bool holds(const char* what) const { return all.find(what) != std::string::npos; }
};

// -- one board, across its reboots -------------------------------------------------------------

// The store, the slot's bytes and the clock outlive a reboot; the Updater and the counters do
// not, which is exactly what a restart is.
struct Board {
  MapStore store;
  ScriptedSlot slot;
  ScriptedClock clock;
  std::vector<std::string> order;

  ScriptedSlots slots;
  ScriptedConditions conditions;
  ScriptedRestart restart;
  Lines lines;
  Updater updater;
  std::string version = kOldVersion;
  std::string bootReason = "Software reset";

  Board() {
    store.order = &order;
    slots.order = &order;
    restart.order = &order;
  }

  // A boot: a fresh machine, fresh counters, the store and the bytes as they were.
  Updater& boot(const char* runningVersion, int runningSlot, SlotState state, const char* reason = "Software reset") {
    version = runningVersion;
    bootReason = reason;
    slots.running = runningSlot;
    slots.state = state;
    slots.boots = 0;
    slots.valids = 0;
    slots.invalids = 0;
    restart.notes = 0;
    restart.posts = 0;
    restart.restarts = 0;
    conditions = ScriptedConditions();
    lines.all.clear();
    updater = Updater();
    Updater::Seams seams;
    seams.slots = &slots;
    seams.slot = &slot;
    seams.store = &store;
    seams.conditions = &conditions;
    seams.restart = &restart;
    seams.clock = &clock;
    updater.begin(seams, version.c_str(), kBoard, bootReason.c_str(), &lines);
    return updater;
  }

  UpdateRecord record() { return mm::update::loadUpdate(store); }

  // The journal's steps, oldest first.
  std::vector<std::string> steps() {
    std::vector<std::string> out;
    const UpdateRecord rec = record();
    JournalEntry e;
    for (size_t i = 0; rec.journal.entry(i, e); i++) out.push_back(e.step);
    return out;
  }

  std::string lastNote() {
    const UpdateRecord rec = record();
    JournalEntry e;
    std::string note;
    for (size_t i = 0; rec.journal.entry(i, e); i++) note = e.note;
    return note;
  }
};

// -- the fixture release's image ---------------------------------------------------------------

std::vector<uint8_t> fileAt(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::vector<uint8_t> theImage(const char* board = "heltec_v3") {
  char name[96];
  std::snprintf(name, sizeof(name), "meshmaster-agent-%s-v0.0.0-test.bin", board);
  const std::vector<uint8_t> bytes = fileAt(fs::path(RELEASE_FIXTURES_DIR) / name);
  EXPECT_FALSE(bytes.empty()) << name << " is not in " << RELEASE_FIXTURES_DIR;
  return bytes;
}

std::string hexOf(const std::vector<uint8_t>& bytes) {
  SHA256 sha;
  sha.reset();
  sha.update(bytes.data(), bytes.size());
  uint8_t digest[32];
  sha.finalize(digest, sizeof(digest));
  char out[65];
  for (int i = 0; i < 32; i++) std::snprintf(out + 2 * i, 3, "%02x", digest[i]);
  return out;
}

// The same image with another version in its descriptor, and the crc32 made good again: an image
// this board has never run, which is what a flash over USB leaves in the first slot.
std::vector<uint8_t> imageAtVersion(std::vector<uint8_t> bytes, const char* version) {
  const mm::update::MemoryBytes reader(bytes.data(), static_cast<uint32_t>(bytes.size()));
  mm::update::Descriptor found;
  uint32_t at = 0;
  size_t count = 0;
  EXPECT_TRUE(mm::update::findOneDescriptor(reader, found, at, count)) << "the fixture image has one descriptor";
  uint8_t* d = bytes.data() + at;
  std::memset(d + 16, 0, 32);
  std::snprintf(reinterpret_cast<char*>(d + 16), 32, "%s", version);
  const uint32_t crc = mm::update::crc32(d, 92);
  for (int i = 0; i < 4; i++) d[92 + static_cast<size_t>(i)] = static_cast<uint8_t>((crc >> (8 * i)) & 0xFF);
  return bytes;
}

// A judged record of the image in the slot, as task 59 leaves one.
void writeJudged(Board& b, const std::vector<uint8_t>& image, const char* version) {
  SlotRecord rec;
  rec.judged = true;
  std::snprintf(rec.version, sizeof(rec.version), "%s", version);
  std::snprintf(rec.sha256hex, sizeof(rec.sha256hex), "%s", hexOf(image).c_str());
  rec.size = static_cast<uint32_t>(image.size());
  std::snprintf(rec.base, sizeof(rec.base), "%s", "/agent/v1/firmware/7");
  mm::update::saveRecord(b.store, rec);
}

// A board that has just applied the judged image: the record says switching and the boot choice
// points at the second slot. Returns the image's bytes.
std::vector<uint8_t> applyTheJudgedImage(Board& b) {
  const std::vector<uint8_t> image = theImage();
  b.slot.bytes = image;
  writeJudged(b, image, kNewVersion);
  b.boot(kOldVersion, 0, SlotState::Valid);
  char why[224] = {0};
  EXPECT_TRUE(b.updater.apply(why, sizeof(why))) << why;
  return image;
}

}  // namespace

// ---- the switch ------------------------------------------------------------------------------

TEST(TheSwitch, WithNothingJudgedItIsRefusedAndNothingIsSwitched) {
  Board b;
  b.slot.bytes = theImage();
  b.boot(kOldVersion, 0, SlotState::Valid);

  char why[224] = {0};
  EXPECT_FALSE(b.updater.apply(why, sizeof(why)));
  EXPECT_NE(std::string::npos, std::string(why).find("holds nothing this firmware will boot")) << why;
  EXPECT_NE(std::string::npos, std::string(why).find("update fetch")) << why;
  EXPECT_EQ(0, b.slots.boots) << "the boot choice must be untouched";
  EXPECT_EQ(0, b.restart.restarts);
  EXPECT_FALSE(b.store.has(mm::update::kUpdateKeyState));
  EXPECT_EQ(Phase::None, b.record().phase);
}

TEST(TheSwitch, WithASlotThatNoLongerHashesToTheRecordItIsRefusedAndNothingIsSwitched) {
  Board b;
  const std::vector<uint8_t> image = theImage();
  b.slot.bytes = image;
  writeJudged(b, image, kNewVersion);
  b.slot.bytes[2000] ^= 0x01;  // one byte of the slot changed since the fetch judged it
  b.boot(kOldVersion, 0, SlotState::Valid);

  char why[224] = {0};
  EXPECT_FALSE(b.updater.apply(why, sizeof(why)));
  EXPECT_NE(std::string::npos, std::string(why).find("hashes to")) << why;
  EXPECT_NE(std::string::npos, std::string(why).find("nothing is switched")) << why;
  EXPECT_EQ(0, b.slots.boots);
  EXPECT_EQ(0, b.restart.restarts);
  EXPECT_FALSE(b.store.has(mm::update::kUpdateKeyState));
}

TEST(TheSwitch, ASlotThatCannotBeReadBackIsRefusedToo) {
  Board b;
  const std::vector<uint8_t> image = theImage();
  b.slot.bytes = image;
  writeJudged(b, image, kNewVersion);
  b.slot.readFails = true;
  b.boot(kOldVersion, 0, SlotState::Valid);

  char why[224] = {0};
  EXPECT_FALSE(b.updater.apply(why, sizeof(why)));
  EXPECT_NE(std::string::npos, std::string(why).find("could not be read back")) << why;
  EXPECT_EQ(0, b.slots.boots);
  EXPECT_EQ(0, b.restart.restarts);
}

TEST(TheSwitch, TheRecordIsWrittenBeforeTheBootChoiceAndTheBootChoiceBeforeTheRestart) {
  Board b;
  const std::vector<uint8_t> image = theImage();
  b.slot.bytes = image;
  writeJudged(b, image, kNewVersion);
  b.boot(kOldVersion, 0, SlotState::Valid);
  b.order.clear();

  char why[224] = {0};
  ASSERT_TRUE(b.updater.apply(why, sizeof(why))) << why;

  const std::vector<std::string> want = {"record:switching", "boot-other", "post-what-waits", "note", "restart"};
  EXPECT_EQ(want, b.order) << "the record, then the boot choice, then what waits in RAM, then the restart";

  const UpdateRecord rec = b.record();
  EXPECT_EQ(Phase::Switching, rec.phase);
  EXPECT_STREQ(kOldVersion, rec.from);
  EXPECT_STREQ(kNewVersion, rec.to);
  EXPECT_STREQ(hexOf(image).c_str(), rec.sha256hex);
  EXPECT_EQ(1u, rec.slot) << "the slot that is not running";
  EXPECT_EQ(b.clock.nowEpoch, rec.at);
  EXPECT_EQ(std::vector<std::string>({"switching"}), b.steps());
  EXPECT_EQ(1, b.restart.posts) << "what waits in RAM gets its one try";
  EXPECT_EQ(1, b.restart.notes);
  EXPECT_NE(std::string::npos, b.restart.lastNote.find("console")) << b.restart.lastNote;
}

TEST(TheSwitch, AFrameworkThatRefusesTheBootChoiceLeavesNoRecordAtAll) {
  Board b;
  const std::vector<uint8_t> image = theImage();
  b.slot.bytes = image;
  writeJudged(b, image, kNewVersion);
  b.boot(kOldVersion, 0, SlotState::Valid);
  b.slots.bootOtherWorks = false;

  char why[224] = {0};
  EXPECT_FALSE(b.updater.apply(why, sizeof(why)));
  EXPECT_NE(std::string::npos, std::string(why).find("refused the boot choice")) << why;
  EXPECT_EQ(0, b.restart.restarts);
  EXPECT_FALSE(b.store.has(mm::update::kUpdateKeyState)) << "a switch that did not happen is not a record";
}

TEST(TheSwitch, OnABoardWithNoSecondSlotItRefusesWithTheSlotsOwnSentence) {
  Board b;
  b.boot(kOldVersion, 0, SlotState::Valid);
  b.slots.there = false;
  b.slots.reason = "this board's table has one application slot; type `update slot`";

  char why[224] = {0};
  EXPECT_FALSE(b.updater.apply(why, sizeof(why)));
  EXPECT_STREQ("this board's table has one application slot; type `update slot`", why);
  EXPECT_EQ(0, b.slots.boots);
}

TEST(TheSwitch, NothingIsSwitchedOnTopOfAnUpdateThatIsInHand) {
  Board b;
  const std::vector<uint8_t> image = applyTheJudgedImage(b);
  (void)image;
  // The same image judged again, and a second apply from the same boot.
  writeJudged(b, b.slot.bytes, kNewVersion);
  char why[224] = {0};
  EXPECT_FALSE(b.updater.apply(why, sizeof(why)));
  EXPECT_NE(std::string::npos, std::string(why).find("already in hand")) << why;
}

// ---- the trial -------------------------------------------------------------------------------

TEST(TheTrial, TheThreeConditionsAtThirtySecondsAreMarkedValidThenAndTheRecordSaysCommitted) {
  Board b;
  applyTheJudgedImage(b);

  // The restart into the new image.
  b.boot(kNewVersion, 1, SlotState::Pending);
  EXPECT_EQ(BootFinding::TrialBegun, b.updater.atBoot());
  EXPECT_TRUE(b.updater.onTrial());
  EXPECT_TRUE(b.updater.bootloaderWillUndo());
  EXPECT_EQ(Phase::OnTrial, b.record().phase);
  EXPECT_EQ(std::vector<std::string>({"switching", "proving"}), b.steps());
  // Task 59's record no longer describes the other slot: what it called judged is running now.
  EXPECT_FALSE(mm::update::loadRecord(b.store).judged);

  // Twenty seconds: two of the three, and nothing is marked.
  b.clock.advanceMs(20000);
  b.conditions.mesh = true;
  b.conditions.link = true;
  b.updater.step();
  EXPECT_EQ(0, b.slots.valids) << "the image is not valid until all three have been seen";
  EXPECT_TRUE(b.updater.onTrial());
  EXPECT_TRUE(b.updater.conditionMet(Condition::Mesh));
  EXPECT_FALSE(b.updater.conditionMet(Condition::Server));

  // Thirty: the third, and it is marked at that moment.
  b.clock.advanceMs(10000);
  b.conditions.server = true;
  b.updater.step();
  EXPECT_EQ(1, b.slots.valids);
  EXPECT_FALSE(b.updater.onTrial());
  EXPECT_EQ(0, b.restart.restarts) << "a commit restarts nothing";

  const UpdateRecord rec = b.record();
  EXPECT_EQ(Phase::Ended, rec.phase);
  EXPECT_EQ(Ended::Committed, rec.ended);
  EXPECT_STREQ(kOldVersion, rec.from);
  EXPECT_STREQ(kNewVersion, rec.to);
  EXPECT_EQ(std::vector<std::string>({"switching", "proving", "committing"}), b.steps());
  EXPECT_NE(std::string::npos, b.lastNote().find("heartbeat answered 30 s")) << b.lastNote();
}

TEST(TheTrial, ARecordThatHasEndedCommittedSurvivesTheNextBoot) {
  Board b;
  applyTheJudgedImage(b);
  b.boot(kNewVersion, 1, SlotState::Pending);
  b.updater.atBoot();
  b.conditions.mesh = b.conditions.link = b.conditions.server = true;
  b.clock.advanceMs(5000);
  b.updater.step();
  ASSERT_EQ(Ended::Committed, b.record().ended);

  // And the boot after it: an ordinary boot, nothing switched, the record still readable.
  b.boot(kNewVersion, 1, SlotState::Valid);
  b.slots.otherVerifies = true;
  b.slots.otherLength = static_cast<uint32_t>(b.slot.bytes.size());
  b.slot.bytes = imageAtVersion(theImage(), kOldVersion);  // the first slot holds what it was switched from
  EXPECT_EQ(BootFinding::Nothing, b.updater.atBoot());
  EXPECT_EQ(0, b.slots.boots) << "the image the record was switched from is one it knows";
  EXPECT_EQ(Ended::Committed, b.record().ended);
}

// Each of the three missing alone at ten minutes: marked invalid, a restart, and the old image's
// boot ends the record *rolled back* naming that condition.
class OneMissingCondition : public ::testing::TestWithParam<int> {};

TEST_P(OneMissingCondition, IsARollbackThatTheOldImagesBootFinishes) {
  const int missing = GetParam();
  Board b;
  applyTheJudgedImage(b);

  b.boot(kNewVersion, 1, SlotState::Pending);
  ASSERT_EQ(BootFinding::TrialBegun, b.updater.atBoot());
  b.conditions.mesh = missing != 0;
  b.conditions.link = missing != 1;
  b.conditions.server = missing != 2;

  // One second short of the ten minutes: nothing has happened yet.
  b.clock.advanceMs(kTrialMs - 1000);
  b.updater.step();
  EXPECT_EQ(0, b.slots.invalids);
  EXPECT_TRUE(b.updater.onTrial());
  EXPECT_GT(b.updater.trialLeftMs(), 0u);

  b.clock.advanceMs(1000);
  b.updater.step();
  EXPECT_EQ(1, b.slots.invalids) << "the image marks itself invalid and the board restarts";
  EXPECT_EQ(0, b.slots.valids);
  EXPECT_FALSE(b.updater.onTrial());
  // The record is **not** finished here: the boot after the restart finishes it.
  EXPECT_EQ(Phase::OnTrial, b.record().phase);

  // The bootloader chooses the old image.
  b.boot(kOldVersion, 0, SlotState::Valid);
  EXPECT_EQ(BootFinding::RolledBack, b.updater.atBoot());
  const UpdateRecord rec = b.record();
  EXPECT_EQ(Phase::Ended, rec.phase);
  EXPECT_EQ(Ended::RolledBack, rec.ended);
  EXPECT_STREQ(kOldVersion, rec.from);
  EXPECT_STREQ(kNewVersion, rec.to);
  static const char* const kWords[3] = {"no mesh", "no wifi", "no server"};
  EXPECT_NE(std::string::npos, std::string(rec.why).find(kWords[missing])) << rec.why;
  EXPECT_EQ(std::vector<std::string>({"switching", "proving", "rolling-back", "restored"}), b.steps());
  EXPECT_NE(std::string::npos, b.lastNote().find(kOldVersion)) << b.lastNote();
}

INSTANTIATE_TEST_SUITE_P(EachOfTheThree, OneMissingCondition, ::testing::Values(0, 1, 2));

// The image is never marked valid before the third condition, at any order of the three.
TEST(TheTrial, TheImageIsNeverMarkedValidBeforeTheThirdConditionInAnyOrder) {
  static const int kOrders[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
  for (const auto& order : kOrders) {
    Board b;
    applyTheJudgedImage(b);
    b.boot(kNewVersion, 1, SlotState::Pending);
    ASSERT_EQ(BootFinding::TrialBegun, b.updater.atBoot());
    for (int i = 0; i < 3; i++) {
      b.clock.advanceMs(5000);
      switch (order[i]) {
        case 0:
          b.conditions.mesh = true;
          break;
        case 1:
          b.conditions.link = true;
          break;
        default:
          b.conditions.server = true;
          break;
      }
      b.updater.step();
      if (i < 2) {
        EXPECT_EQ(0, b.slots.valids) << "order " << order[0] << order[1] << order[2] << ", after " << (i + 1);
        EXPECT_TRUE(b.updater.onTrial());
      }
    }
    EXPECT_EQ(1, b.slots.valids) << "order " << order[0] << order[1] << order[2];
    EXPECT_EQ(Ended::Committed, b.record().ended);
  }
}

TEST(TheTrial, ARestartAtFourMinutesEndsTheRecordRolledBackWithTheRestartsReason) {
  Board b;
  applyTheJudgedImage(b);
  b.boot(kNewVersion, 1, SlotState::Pending);
  ASSERT_EQ(BootFinding::TrialBegun, b.updater.atBoot());
  b.conditions.mesh = true;
  b.clock.advanceMs(4u * 60u * 1000u);
  b.updater.step();
  ASSERT_TRUE(b.updater.onTrial());
  ASSERT_EQ(0, b.slots.invalids) << "nothing has decided anything yet";

  // The watchdog, a crash or the power: the board resets and the bootloader, finding the image
  // still pending, boots the old one.
  b.boot(kOldVersion, 0, SlotState::Valid, "Task watchdog reset");
  EXPECT_EQ(BootFinding::RolledBack, b.updater.atBoot());
  const UpdateRecord rec = b.record();
  EXPECT_EQ(Ended::RolledBack, rec.ended);
  EXPECT_NE(std::string::npos, std::string(rec.why).find("restarted during the trial")) << rec.why;
  EXPECT_NE(std::string::npos, std::string(rec.why).find("Task watchdog reset")) << rec.why;
  EXPECT_EQ(std::vector<std::string>({"switching", "proving", "rolling-back", "restored"}), b.steps());
  EXPECT_EQ(0, b.slots.boots) << "the bootloader did it; this firmware switches nothing back";
}

TEST(TheTrial, ASwitchingRecordThatComesBackAsTheSameImageIsNothingTried) {
  Board b;
  applyTheJudgedImage(b);
  // The restart came before the boot choice took, so the first slot is running again.
  b.boot(kOldVersion, 0, SlotState::Valid);
  EXPECT_EQ(BootFinding::SwitchNotTaken, b.updater.atBoot());
  const UpdateRecord rec = b.record();
  EXPECT_EQ(Ended::RolledBack, rec.ended);
  EXPECT_NE(std::string::npos, std::string(rec.why).find("never taken")) << rec.why;
  EXPECT_EQ(0, b.slots.boots);
  EXPECT_EQ(0, b.restart.restarts);
}

TEST(TheTrial, AFrameworkThatWillNotMarkAPendingImageValidSendsItBackInstead) {
  Board b;
  applyTheJudgedImage(b);
  b.boot(kNewVersion, 1, SlotState::Pending);
  ASSERT_EQ(BootFinding::TrialBegun, b.updater.atBoot());
  b.slots.markValidWorks = false;
  b.conditions.mesh = b.conditions.link = b.conditions.server = true;
  b.clock.advanceMs(5000);
  b.updater.step();
  EXPECT_EQ(1, b.slots.invalids) << "the bootloader would undo it at the next reset anyway";
  EXPECT_NE(std::string::npos, std::string(b.record().why).find("would not mark it valid")) << b.record().why;
}

// ---- a bootloader that does not mark an image pending ----------------------------------------

TEST(ABootloaderThatDoesNotUndo, IsSaidSoInTheJournalAndTheConditionsStillCommit) {
  Board b;
  applyTheJudgedImage(b);
  b.boot(kNewVersion, 1, SlotState::New);  // switched to, and never marked pending
  EXPECT_EQ(BootFinding::TrialBegun, b.updater.atBoot());
  EXPECT_FALSE(b.updater.bootloaderWillUndo());
  JournalEntry e;
  ASSERT_TRUE(b.record().journal.entry(1, e));
  EXPECT_STREQ("proving", e.step);
  EXPECT_NE(std::string::npos, std::string(e.note).find("not pending")) << e.note;
  EXPECT_NE(std::string::npos, std::string(e.note).find("leaves it new")) << e.note;
  EXPECT_NE(std::string::npos, std::string(e.note).find("this firmware undoes it")) << e.note;

  b.conditions.mesh = b.conditions.link = b.conditions.server = true;
  b.clock.advanceMs(9000);
  b.updater.step();
  EXPECT_EQ(1, b.slots.valids);
  EXPECT_EQ(Ended::Committed, b.record().ended);
}

TEST(ABootloaderThatDoesNotUndo, SetsTheBootChoiceBackItselfWhenAConditionIsMissing) {
  Board b;
  applyTheJudgedImage(b);
  b.boot(kNewVersion, 1, SlotState::New);
  ASSERT_EQ(BootFinding::TrialBegun, b.updater.atBoot());
  b.conditions.mesh = true;
  b.conditions.link = true;  // no heartbeat
  b.clock.advanceMs(kTrialMs);
  b.updater.step();

  EXPECT_EQ(0, b.slots.invalids) << "there is no pending state to mark invalid";
  EXPECT_EQ(1, b.slots.boots) << "the boot choice is set back by this firmware";
  EXPECT_EQ(1, b.restart.restarts);
  EXPECT_EQ(Phase::OnTrial, b.record().phase) << "the boot after the restart finishes it";

  b.boot(kOldVersion, 0, SlotState::Undefined);
  EXPECT_EQ(BootFinding::RolledBack, b.updater.atBoot());
  EXPECT_EQ(Ended::RolledBack, b.record().ended);
  EXPECT_NE(std::string::npos, std::string(b.record().why).find("no server")) << b.record().why;
}

TEST(ABootloaderThatDoesNotUndo, UndoesATrialThatWasCutShortAtTheNextBoot) {
  Board b;
  applyTheJudgedImage(b);
  b.boot(kNewVersion, 1, SlotState::New);
  ASSERT_EQ(BootFinding::TrialBegun, b.updater.atBoot());
  b.clock.advanceMs(60000);

  // A reset four minutes in. The bootloader does not undo it, so the new image boots again - and
  // finds its own trial in the record.
  b.boot(kNewVersion, 1, SlotState::New, "Panic reset");
  EXPECT_EQ(BootFinding::CutShort, b.updater.atBoot());
  EXPECT_FALSE(b.updater.onTrial());
  EXPECT_EQ(1, b.slots.boots);
  EXPECT_EQ(1, b.restart.restarts);
  EXPECT_EQ(Phase::OnTrial, b.record().phase);
  EXPECT_NE(std::string::npos, std::string(b.record().why).find("Panic reset")) << b.record().why;

  b.boot(kOldVersion, 0, SlotState::Undefined);
  EXPECT_EQ(BootFinding::RolledBack, b.updater.atBoot());
  EXPECT_EQ(Ended::RolledBack, b.record().ended);
  EXPECT_NE(std::string::npos, std::string(b.record().why).find("does not undo")) << b.record().why;
}

// ---- a flash over USB -------------------------------------------------------------------------

// A board that committed an update runs from the second slot; the first then holds what it was
// switched from. The record of that state is what the tests below start from.
static void aBoardRunningFromTheSecondSlot(Board& b) {
  applyTheJudgedImage(b);
  b.boot(kNewVersion, 1, SlotState::Pending);
  b.updater.atBoot();
  b.conditions.mesh = b.conditions.link = b.conditions.server = true;
  b.clock.advanceMs(5000);
  b.updater.step();
  ASSERT_EQ(Ended::Committed, b.record().ended);
}

TEST(AFlashOverUsb, AnUnknownImageOfThisProductInTheFirstSlotIsSwitchedToOnce) {
  Board b;
  aBoardRunningFromTheSecondSlot(b);

  const std::vector<uint8_t> flashed = imageAtVersion(theImage(), "0.9.0+abcdef1");
  b.slot.bytes = flashed;
  b.boot(kNewVersion, 1, SlotState::Valid);
  b.slots.otherVerifies = true;
  b.slots.otherLength = static_cast<uint32_t>(flashed.size());

  EXPECT_EQ(BootFinding::SwitchedAtBoot, b.updater.atBoot());
  EXPECT_EQ(1, b.slots.boots);
  EXPECT_EQ(1, b.restart.restarts);
  EXPECT_EQ(0, b.restart.posts) << "nothing waits in RAM at a boot";
  const UpdateRecord rec = b.record();
  EXPECT_EQ(Phase::Switching, rec.phase);
  EXPECT_STREQ(kNewVersion, rec.from);
  EXPECT_STREQ("0.9.0+abcdef1", rec.to);
  EXPECT_EQ(0u, rec.slot);
  EXPECT_NE(std::string::npos, b.lastNote().find("over USB")) << b.lastNote();

  // And it is on trial like any other.
  b.boot("0.9.0+abcdef1", 0, SlotState::Pending);
  EXPECT_EQ(BootFinding::TrialBegun, b.updater.atBoot());

  // Once: the trial fails, the board goes back, and that image is not switched to again.
  b.conditions.mesh = true;
  b.clock.advanceMs(kTrialMs);
  b.updater.step();
  b.boot(kNewVersion, 1, SlotState::Valid, "Software reset");
  b.slots.otherVerifies = true;
  b.slots.otherLength = static_cast<uint32_t>(flashed.size());
  EXPECT_EQ(BootFinding::RolledBack, b.updater.atBoot());
  EXPECT_EQ(0, b.slots.boots) << "an image the record knows as tried and failed is not switched to again";

  // And not at the boot after that either.
  b.boot(kNewVersion, 1, SlotState::Valid);
  b.slots.otherVerifies = true;
  b.slots.otherLength = static_cast<uint32_t>(flashed.size());
  EXPECT_EQ(BootFinding::Nothing, b.updater.atBoot());
  EXPECT_EQ(0, b.slots.boots);
}

TEST(AFlashOverUsb, AnImageOfTheOtherBoardIsNotSwitchedTo) {
  Board b;
  aBoardRunningFromTheSecondSlot(b);
  const std::vector<uint8_t> v4 = theImage("heltec_v4");
  b.slot.bytes = v4;
  b.boot(kNewVersion, 1, SlotState::Valid);
  b.slots.otherVerifies = true;
  b.slots.otherLength = static_cast<uint32_t>(v4.size());

  EXPECT_EQ(BootFinding::Nothing, b.updater.atBoot());
  EXPECT_EQ(0, b.slots.boots);
  EXPECT_TRUE(b.lines.holds("heltec_v4")) << b.lines.all;
  EXPECT_TRUE(b.lines.holds("not this product for this board")) << b.lines.all;
}

TEST(AFlashOverUsb, AnImageTheFrameworkWillNotLoadIsNotSwitchedTo) {
  Board b;
  aBoardRunningFromTheSecondSlot(b);
  b.slot.bytes = imageAtVersion(theImage(), "0.9.0+abcdef1");
  b.boot(kNewVersion, 1, SlotState::Valid);
  b.slots.otherVerifies = false;  // esp_image_verify said no
  b.slots.otherLength = 0;

  EXPECT_EQ(BootFinding::Nothing, b.updater.atBoot());
  EXPECT_EQ(0, b.slots.boots);
}

TEST(AFlashOverUsb, AnImageWithNoDescriptorOrTwoIsNotSwitchedTo) {
  for (int which = 0; which < 2; which++) {
    Board b;
    aBoardRunningFromTheSecondSlot(b);
    std::vector<uint8_t> image = imageAtVersion(theImage(), "0.9.0+abcdef1");
    if (which == 0) {
      // None: the magic of the one descriptor broken.
      const mm::update::MemoryBytes reader(image.data(), static_cast<uint32_t>(image.size()));
      mm::update::Descriptor found;
      uint32_t at = 0;
      size_t count = 0;
      ASSERT_TRUE(mm::update::findOneDescriptor(reader, found, at, count));
      image[at] ^= 0xFF;
    } else {
      // Two: the 96 bytes again, on a 4-byte boundary at the end.
      const mm::update::MemoryBytes reader(image.data(), static_cast<uint32_t>(image.size()));
      mm::update::Descriptor found;
      uint32_t at = 0;
      size_t count = 0;
      ASSERT_TRUE(mm::update::findOneDescriptor(reader, found, at, count));
      const size_t end = image.size() - mm::update::kDescriptorLength;
      std::memcpy(image.data() + (end & ~static_cast<size_t>(3)), image.data() + at, mm::update::kDescriptorLength);
    }
    b.slot.bytes = image;
    b.boot(kNewVersion, 1, SlotState::Valid);
    b.slots.otherVerifies = true;
    b.slots.otherLength = static_cast<uint32_t>(image.size());
    EXPECT_EQ(BootFinding::Nothing, b.updater.atBoot()) << "case " << which;
    EXPECT_EQ(0, b.slots.boots) << "case " << which;
  }
}

TEST(AFlashOverUsb, RunningFromTheFirstSlotNothingIsSwitchedAtBootWhateverTheSecondHolds) {
  Board b;
  const std::vector<uint8_t> unknown = imageAtVersion(theImage(), "0.9.0+abcdef1");
  b.slot.bytes = unknown;
  b.boot(kOldVersion, 0, SlotState::Valid);
  b.slots.otherVerifies = true;
  b.slots.otherLength = static_cast<uint32_t>(unknown.size());

  EXPECT_EQ(BootFinding::Nothing, b.updater.atBoot());
  EXPECT_EQ(0, b.slots.boots);
  EXPECT_EQ(0, b.restart.restarts);
}

TEST(AFlashOverUsb, OnABoardWithNoSecondSlotTheBootFindsNothingToDo) {
  Board b;
  b.boot(kOldVersion, 0, SlotState::Undefined);
  b.slots.there = false;
  b.slots.reason = "this board has no second application slot";
  EXPECT_EQ(BootFinding::NoSlots, b.updater.atBoot());
  EXPECT_EQ(0, b.slots.boots);
  EXPECT_EQ(0, b.restart.restarts);
  EXPECT_TRUE(b.lines.holds("no second application slot")) << b.lines.all;
}

// ---- the record and its journal ---------------------------------------------------------------

TEST(TheRecord, RoundTripsAndSaysNothingInHandWhenItIsCleared) {
  MapStore store;
  UpdateRecord rec;
  rec.phase = Phase::OnTrial;
  std::snprintf(rec.from, sizeof(rec.from), "%s", kOldVersion);
  std::snprintf(rec.to, sizeof(rec.to), "%s", kNewVersion);
  std::snprintf(rec.sha256hex, sizeof(rec.sha256hex), "%s", std::string(64, 'a').c_str());
  rec.at = 1795165200;
  rec.slot = 1;
  rec.journal.add(1795165200, "switching", "0.0.0-old to 0.0.0-test");
  ASSERT_TRUE(mm::update::saveUpdate(store, rec));

  const UpdateRecord back = mm::update::loadUpdate(store);
  EXPECT_EQ(Phase::OnTrial, back.phase);
  EXPECT_TRUE(back.inHand());
  EXPECT_STREQ(kOldVersion, back.from);
  EXPECT_STREQ(kNewVersion, back.to);
  EXPECT_EQ(std::string(64, 'a'), std::string(back.sha256hex));
  EXPECT_EQ(1795165200u, back.at);
  EXPECT_EQ(1u, back.slot);
  EXPECT_EQ(1u, back.journal.count());
  JournalEntry e;
  ASSERT_TRUE(back.journal.entry(0, e));
  EXPECT_EQ(1795165200u, e.at);
  EXPECT_STREQ("switching", e.step);
  EXPECT_STREQ("0.0.0-old to 0.0.0-test", e.note);

  const UpdateRecord none;
  ASSERT_TRUE(mm::update::saveUpdate(store, none));
  for (const char* key : {mm::update::kUpdateKeyState, mm::update::kUpdateKeyFrom, mm::update::kUpdateKeyTo,
                          mm::update::kUpdateKeySha, mm::update::kUpdateKeyAt, mm::update::kUpdateKeySlot,
                          mm::update::kUpdateKeyEnd, mm::update::kUpdateKeyWhy, mm::update::kUpdateKeyJournal}) {
    EXPECT_FALSE(store.has(key)) << key;
  }
  EXPECT_EQ(Phase::None, mm::update::loadUpdate(store).phase);
}

TEST(TheRecord, AHalfWrittenOneIsNotARecord) {
  MapStore store;
  UpdateRecord rec;
  rec.phase = Phase::Ended;
  rec.ended = Ended::Committed;
  std::snprintf(rec.to, sizeof(rec.to), "%s", kNewVersion);
  ASSERT_TRUE(mm::update::saveUpdate(store, rec));
  ASSERT_EQ(Ended::Committed, mm::update::loadUpdate(store).ended);

  // A `done` with no ending, a state nothing here wrote, and an `in hand` with nothing to go to.
  store.values[mm::update::kUpdateKeyEnd] = "";
  store.values.erase(mm::update::kUpdateKeyEnd);
  EXPECT_EQ(Phase::None, mm::update::loadUpdate(store).phase);
  store.values[mm::update::kUpdateKeyState] = "halfway";
  EXPECT_EQ(Phase::None, mm::update::loadUpdate(store).phase);
  store.values[mm::update::kUpdateKeyState] = "trial";
  store.values.erase(mm::update::kUpdateKeyTo);
  EXPECT_EQ(Phase::None, mm::update::loadUpdate(store).phase);
}

TEST(TheRecord, TheStateIsWrittenLastOnTheWayInAndClearedFirstOnTheWayOut) {
  MapStore store;
  std::vector<std::string> order;
  // Every key, in the order they are written.
  class Watching final : public mm::update::RecordStore {
   public:
    MapStore* inner = nullptr;
    std::vector<std::string>* keys = nullptr;
    size_t get(const char* key, char* out, size_t cap) override { return inner->get(key, out, cap); }
    bool put(const char* key, const char* value) override {
      keys->push_back(key);
      return inner->put(key, value);
    }
  } watching;
  watching.inner = &store;
  watching.keys = &order;

  UpdateRecord rec;
  rec.phase = Phase::Switching;
  std::snprintf(rec.to, sizeof(rec.to), "%s", kNewVersion);
  ASSERT_TRUE(mm::update::saveUpdate(watching, rec));
  ASSERT_FALSE(order.empty());
  EXPECT_EQ(std::string(mm::update::kUpdateKeyState), order.back()) << "the state goes in last";

  order.clear();
  const UpdateRecord none;
  ASSERT_TRUE(mm::update::saveUpdate(watching, none));
  ASSERT_FALSE(order.empty());
  EXPECT_EQ(std::string(mm::update::kUpdateKeyState), order.front()) << "and comes out first";
}

TEST(TheJournal, FortyFiveStepsAreKeptAsFortyWithinTwoThousandCharacters) {
  mm::update::Journal journal;
  for (int i = 0; i < 45; i++) {
    char note[32];
    std::snprintf(note, sizeof(note), "entry %d", i);
    journal.add(1795165200u + static_cast<uint32_t>(i), "proving", note);
  }
  EXPECT_EQ(mm::update::kJournalMaxEntries, journal.count());
  EXPECT_LE(journal.characters(), mm::update::kJournalMaxCharacters);

  // The oldest five went and the newest stayed: the end of a journal is what an outcome is read
  // against, and what the first lines said is in the record's own fields too.
  JournalEntry first;
  JournalEntry last;
  ASSERT_TRUE(journal.entry(0, first));
  ASSERT_TRUE(journal.entry(journal.count() - 1, last));
  EXPECT_STREQ("entry 44", last.note);
  EXPECT_STREQ("entry 5", first.note);
  EXPECT_EQ(1795165200u + 5u, first.at);
  EXPECT_FALSE(journal.entry(journal.count(), last));

  // And the text it is stored as reads back as the same forty entries.
  mm::update::Journal again;
  again.load(journal.text());
  EXPECT_EQ(journal.count(), again.count());
  EXPECT_STREQ(journal.text(), again.text());
}

TEST(TheJournal, TheCharacterBoundBitesBeforeTheEntryBoundWhenTheNotesAreLong) {
  mm::update::Journal journal;
  const std::string longNote(mm::update::kMaxNoteLen, 'x');
  for (int i = 0; i < 40; i++) journal.add(1795165200u, "proving", longNote.c_str());
  EXPECT_LE(journal.characters(), mm::update::kJournalMaxCharacters);
  EXPECT_LT(journal.count(), mm::update::kJournalMaxEntries) << "2 000 characters of 104 is not 40 entries";
  EXPECT_GT(journal.count(), 1u);
}

TEST(TheJournal, ASeparatorInAStepOrANoteBecomesASpaceAndAnEntryWithNoStepIsNotOne) {
  mm::update::Journal journal;
  journal.add(7, "proving", "one|two\nthree");
  journal.add(7, "", "nothing to say about nothing");
  ASSERT_EQ(1u, journal.count());
  JournalEntry e;
  ASSERT_TRUE(journal.entry(0, e));
  EXPECT_STREQ("one two three", e.note);
  EXPECT_EQ(std::string("7|proving|one two three"), std::string(journal.text()));
}

TEST(TheJournal, ACorruptOrCutValueKeepsTheLinesThatAreWhole) {
  mm::update::Journal journal;
  journal.load("7|switching|from here to there\nnot a line at all\n9|proving|and on\n11|rolling-b");
  EXPECT_EQ(2u, journal.count());
  JournalEntry e;
  ASSERT_TRUE(journal.entry(1, e));
  EXPECT_STREQ("proving", e.step);
  EXPECT_STREQ("and on", e.note);

  // And the same read straight out of a store into the journal's own buffer, which is how a boot
  // reads it: no second buffer, and the lines that are whole kept in place.
  MapStore store;
  store.values["fw.up.jr"] = "7|switching|from here to there\nnot a line at all\n9|proving|and on\n11|rolling-b";
  mm::update::Journal read;
  read.readFrom(store, "fw.up.jr");
  EXPECT_EQ(2u, read.count());
  EXPECT_STREQ(journal.text(), read.text());

  // A value nothing here wrote, longer than the journal's own capacity and past both bounds: what
  // comes back is inside them.
  std::string tooMuch;
  for (int i = 0; i < 200; i++) tooMuch += "7|proving|a line of some length to fill it up\n";
  store.values["fw.up.jr"] = tooMuch;
  read.readFrom(store, "fw.up.jr");
  EXPECT_LE(read.count(), mm::update::kJournalMaxEntries);
  EXPECT_LE(read.characters(), mm::update::kJournalMaxCharacters);
  EXPECT_GT(read.count(), 1u);
  ASSERT_TRUE(read.entry(0, e));
  EXPECT_STREQ("proving", e.step);
}

// ---- what a person reads ----------------------------------------------------------------------

TEST(TheWords, UpdateShowsTheTrialWhileThereIsOneAndTheEndingAfterwards) {
  Board b;
  applyTheJudgedImage(b);
  b.boot(kNewVersion, 1, SlotState::Pending);
  ASSERT_EQ(BootFinding::TrialBegun, b.updater.atBoot());
  b.clock.advanceMs(90000);
  b.conditions.mesh = true;
  b.updater.step();

  Lines out;
  b.updater.report(out);
  EXPECT_TRUE(out.holds("on trial, 8 min 30 s left")) << out.all;
  EXPECT_TRUE(out.holds("mesh yes")) << out.all;
  EXPECT_TRUE(out.holds("wifi and clock not yet")) << out.all;
  EXPECT_TRUE(out.holds("heartbeat not yet")) << out.all;
  EXPECT_TRUE(out.holds(kNewVersion)) << out.all;
  EXPECT_STREQ("on trial", b.updater.statusNote());

  b.conditions.link = b.conditions.server = true;
  b.clock.advanceMs(1000);
  b.updater.step();
  Lines after;
  b.updater.report(after);
  EXPECT_TRUE(after.holds("last      committed")) << after.all;
  EXPECT_TRUE(after.holds("2026-11-20T09:00")) << after.all;
  EXPECT_STREQ("", b.updater.statusNote());
}

TEST(TheWords, UpdateSaysWhatARolledBackUpdateWasMissing) {
  Board b;
  applyTheJudgedImage(b);
  b.boot(kNewVersion, 1, SlotState::Pending);
  ASSERT_EQ(BootFinding::TrialBegun, b.updater.atBoot());
  b.conditions.mesh = true;
  b.clock.advanceMs(kTrialMs);
  b.updater.step();
  b.boot(kOldVersion, 0, SlotState::Valid);
  ASSERT_EQ(BootFinding::RolledBack, b.updater.atBoot());

  Lines out;
  b.updater.report(out);
  EXPECT_TRUE(out.holds("last      rolled back: no wifi")) << out.all;
  EXPECT_TRUE(out.holds(kOldVersion)) << out.all;
  EXPECT_TRUE(out.holds(kNewVersion)) << out.all;
}

TEST(TheWords, NoStepLineAndNoRecordHoldsAnythingButVersionsAndSentences) {
  Board b;
  applyTheJudgedImage(b);
  b.boot(kNewVersion, 1, SlotState::Pending);
  b.updater.atBoot();
  b.conditions.mesh = b.conditions.link = b.conditions.server = true;
  b.clock.advanceMs(5000);
  b.updater.step();

  // Nothing of a token, a header or a key can be in any of this: the whole of what is written is
  // versions, a hash the server published, a slot number, a time and this firmware's own
  // sentences. The one thing worth a test is that the hash of the image is the only hex here.
  for (const char* forbidden : {"mma_", "Bearer", "Authorization", "wifi.pass", "meshmaster.token"}) {
    EXPECT_EQ(std::string::npos, b.lines.all.find(forbidden)) << forbidden << " in: " << b.lines.all;
    EXPECT_EQ(std::string::npos, std::string(b.record().journal.text()).find(forbidden)) << forbidden;
  }
}

// The other slot is not a scratchpad while an update is in hand: it is the one firmware this
// board is known to be able to run, and a fetch into it during a trial would leave a board with
// nothing that works. Task 59's `update fetch` asks this before it writes a byte.
TEST(TheTrial, TheOtherSlotMayNotBeWrittenWhileAnUpdateIsInHand) {
  Board b;
  b.slot.bytes = theImage();
  b.boot(kOldVersion, 0, SlotState::Valid);
  ASSERT_EQ(BootFinding::Nothing, b.updater.atBoot());
  EXPECT_EQ(nullptr, b.updater.whyTheOtherSlotMayNotBeWritten()) << "an ordinary boot stops nothing";

  writeJudged(b, b.slot.bytes, kNewVersion);
  char why[224] = {0};
  ASSERT_TRUE(b.updater.apply(why, sizeof(why))) << why;
  ASSERT_NE(nullptr, b.updater.whyTheOtherSlotMayNotBeWritten());
  EXPECT_NE(std::string::npos, std::string(b.updater.whyTheOtherSlotMayNotBeWritten()).find("reboot"))
      << b.updater.whyTheOtherSlotMayNotBeWritten();

  b.boot(kNewVersion, 1, SlotState::Pending);
  ASSERT_EQ(BootFinding::TrialBegun, b.updater.atBoot());
  ASSERT_NE(nullptr, b.updater.whyTheOtherSlotMayNotBeWritten());
  EXPECT_NE(std::string::npos, std::string(b.updater.whyTheOtherSlotMayNotBeWritten()).find("on trial"))
      << b.updater.whyTheOtherSlotMayNotBeWritten();

  b.conditions.mesh = b.conditions.link = b.conditions.server = true;
  b.clock.advanceMs(5000);
  b.updater.step();
  EXPECT_EQ(nullptr, b.updater.whyTheOtherSlotMayNotBeWritten()) << "a committed update is out of the way";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
