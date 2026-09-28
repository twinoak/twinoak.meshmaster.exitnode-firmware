// The apply, on a host: the plan's commands sent once each, in order, with strictly increasing
// sender timestamps; a rejected second command leaves the third `not-sent`; a silence stops the
// sequence and is never sent again; the region sequence sends its lines without waiting and the
// terminator after a failure; and the result is the .NET agent's bytes for the same replies
// (tests/fixtures/contracts/ConfigApplyResult.json, and what ConfigApplyHandlerTests asserts).
//
// The engine (src/agent/ConfigApply.cpp) takes an ApplyExchange, and the one below is a fake
// repeater: it records every line it is handed with its timestamp, answers from a script, and
// can eat a reply or refuse to build a packet. The board's own exchange is JobRunner's, which a
// host cannot run; the last tests hold that file's text to the two orders that matter - the
// restore after the password line, and no restore around a reboot.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "agent/CliReplies.h"
#include "agent/ConfigApply.h"
#include "agent/ConfigRead.h"
#include "agent/JobEnvelopes.h"

#ifndef FIXTURES_DIR
#error "FIXTURES_DIR must be defined; platformio.ini's [env:native] does it"
#endif
#ifndef PROJECT_DIR
#error "PROJECT_DIR must be defined; platformio.ini's [env:native] does it"
#endif

namespace {

namespace fs = std::filesystem;

std::string readFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  EXPECT_TRUE(in.good()) << "cannot read " << path;
  std::ostringstream all;
  all << in.rdbuf();
  return all.str();
}

std::string fixture(const std::string& name) {
  std::string text = readFile(fs::path(FIXTURES_DIR) / (name + ".json"));
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
  return text;
}

// A repeater on a script: the reply to a line, or one of three silences.
struct Scripted {
  enum class Kind { Answer, Eat, RefuseToBuild, LeaseOut } kind = Kind::Answer;
  std::string reply;
};

struct SentLine {
  std::string what;
  std::string line;
  uint32_t timestamp = 0;
  bool awaited = false;
  bool retry = false;
  bool escape = false;
};

class FakeRepeater : public mm::ApplyExchange {
 public:
  // The clock in whole seconds, as the RTC's; `unique` is MeshCore's getCurrentTimeUnique
  // (src/MeshCore.h:108-114 at d9296435): the time, or one above the last one handed out.
  uint32_t now = 1'700'000'000;
  uint32_t lastUnique = 0;
  bool regionsAnswer = true;
  std::string regionsValue = "* F\n dk F\ndefault dk";

  std::vector<SentLine> sent;
  std::vector<std::string> logged;
  std::map<std::string, Scripted> script;

  void answer(const std::string& line, const std::string& reply) { script[line] = {Scripted::Kind::Answer, reply}; }
  void eat(const std::string& line) { script[line] = {Scripted::Kind::Eat, ""}; }
  void refuseToBuild(const std::string& line) { script[line] = {Scripted::Kind::RefuseToBuild, ""}; }
  void leaseOutAt(const std::string& line) { script[line] = {Scripted::Kind::LeaseOut, ""}; }

  Sent send(const mm::ApplySend& s, char* reply, size_t cap) override {
    sent.push_back({s.what, s.line, s.timestamp, s.awaitReply, s.retry, s.escape});
    reply[0] = '\0';
    const auto it = script.find(s.line);
    if (it == script.end()) {
      // A `get` of a key nobody scripted answers `> ?`; anything else is `OK`.
      if (!s.awaitReply) return Sent::Answered;
      std::snprintf(reply, cap, "%s", std::strncmp(s.line, "get ", 4) == 0 ? "> ?" : "OK");
      return Sent::Answered;
    }
    switch (it->second.kind) {
      case Scripted::Kind::Answer:
        if (s.awaitReply) std::snprintf(reply, cap, "%s", it->second.reply.c_str());
        return Sent::Answered;
      case Scripted::Kind::Eat: return s.awaitReply ? Sent::NoReply : Sent::Answered;
      case Scripted::Kind::RefuseToBuild: return Sent::NotSent;
      case Scripted::Kind::LeaseOut:
      default: return Sent::LeaseOut;
    }
  }

  uint32_t nextTimestamp() override {
    if (now <= lastUnique) return ++lastUnique;
    return lastUnique = now;
  }

  bool readRegions(mm::ConfigSnapshot& snapshot) override {
    sent.push_back({"region", "region", nextTimestamp(), true, true, false});
    sent.push_back({"region default", "region default", nextTimestamp(), true, true, false});
    if (!regionsAnswer) return false;
    snapshot.put(mm::kRegionsKey, regionsValue.c_str());
    return true;
  }

  void log(const char* line) override { logged.emplace_back(line); }

  int timesSent(const std::string& line) const {
    int n = 0;
    for (const auto& s : sent) {
      if (s.line == line) n++;
    }
    return n;
  }
  std::vector<std::string> lines() const {
    std::vector<std::string> out;
    for (const auto& s : sent) out.push_back(s.line);
    return out;
  }
};

// The three-command plan of ConfigApplyHandlerTests: advert.interval, flood.max, repeat.
mm::ConfigApplyPlan threeCommands() {
  mm::ConfigApplyPlan plan;
  plan.clear();
  EXPECT_TRUE(plan.addCommand("advert.interval", "set advert.interval 120"));
  EXPECT_TRUE(plan.addCommand("flood.max", "set flood.max 100"));
  EXPECT_TRUE(plan.addCommand("repeat", "set repeat off"));
  std::snprintf(plan.planHash, sizeof plan.planHash, "3f2a9c0d");
  return plan;
}

// A plan with one plain key and the regions entry as RegionCommands.Of spells it (task p4-57):
// the load, two region lines, the blank line, the home, the default and the save.
mm::ConfigApplyPlan withRegions() {
  mm::ConfigApplyPlan plan;
  plan.clear();
  EXPECT_TRUE(plan.addCommand("advert.interval", "set advert.interval 120"));
  EXPECT_TRUE(plan.addCommand("regions", "region load"));
  EXPECT_TRUE(plan.addLine("region load", nullptr, false));
  EXPECT_TRUE(plan.addLine(" eu F", nullptr, false));
  EXPECT_TRUE(plan.addLine("  dk F", nullptr, false));
  EXPECT_TRUE(plan.addLine(mm::kRegionLoadTerminator, "OK - loaded", true));
  EXPECT_TRUE(plan.addLine("region home dk", "home is now", false));
  EXPECT_TRUE(plan.addLine("region default dk", "default scope is now", false));
  EXPECT_TRUE(plan.addLine("region save", "OK", false));
  EXPECT_TRUE(plan.addCommand("repeat", "set repeat off"));
  return plan;
}

std::string resultJson(const mm::ConfigApplyPlan& plan, const mm::ConfigApplyOutcome& out, const mm::ConfigSnapshot& reRead) {
  char buf[4096];
  const size_t len = mm::writeConfigApplyResult(plan, out, reRead, buf, sizeof buf);
  EXPECT_GT(len, 0u);
  return std::string(buf, len);
}

// ------------------------------------------------------------------ the sequence

// Three commands where the second is rejected: the first two on the wire once each, in order,
// the third never; the two sent keys and `ver` read back (ConfigApplyHandlerTests.
// The_second_rejected_stops_the_sequence_the_third_is_not_sent_and_the_two_sent_are_read_back).
TEST(ConfigApply, ARejectedSecondCommandLeavesTheThirdNotSentAndReadsBackWhatWasSent) {
  FakeRepeater r;
  r.answer("set advert.interval 120", "OK");
  r.answer("set flood.max 100", "Error, max 64");
  r.answer("get advert.interval", "> 120");
  r.answer("get flood.max", "> 64");
  r.answer("ver", "v1.17.1 (Build: 14 Aug 2026)");
  const mm::ConfigApplyPlan plan = threeCommands();
  mm::ConfigApplyOutcome out;
  mm::ConfigSnapshot reRead;

  const mm::ApplyRun run = mm::runConfigApply(plan, r, out, reRead);

  EXPECT_EQ(run, mm::ApplyRun::Failed);
  EXPECT_EQ(r.lines(), (std::vector<std::string>{"set advert.interval 120", "set flood.max 100", "get advert.interval",
                                                  "get flood.max", "ver"}));
  EXPECT_EQ(r.timesSent("set repeat off"), 0) << "the third line is never on the wire";
  ASSERT_EQ(out.commandCount, 3);
  EXPECT_EQ(out.commands[0].outcome, mm::CommandOutcome::Ok);
  EXPECT_STREQ(out.commands[0].reply, "OK");
  EXPECT_EQ(out.commands[1].outcome, mm::CommandOutcome::Error);
  EXPECT_STREQ(out.commands[1].reply, "Error, max 64");
  EXPECT_EQ(out.commands[2].outcome, mm::CommandOutcome::NotSent);
  EXPECT_FALSE(out.commands[2].hasReply);
  EXPECT_EQ(out.sent(), 2);
  EXPECT_EQ(out.ok(), 1);
  EXPECT_FALSE(out.allOk());
  ASSERT_EQ(reRead.count, 2);
  EXPECT_STREQ(reRead.values[0].key, "advert.interval");
  EXPECT_STREQ(reRead.values[0].value, "120");
  EXPECT_STREQ(reRead.values[1].key, "flood.max");
  EXPECT_STREQ(reRead.values[1].value, "64");
  EXPECT_STREQ(reRead.firmware, "v1.17.1 (Build: 14 Aug 2026)");
  EXPECT_FALSE(out.reReadStopped);

  // The sets are sent once, never retried; the gets take the bounded retry every read takes.
  EXPECT_FALSE(r.sent[0].retry);
  EXPECT_FALSE(r.sent[1].retry);
  EXPECT_TRUE(r.sent[2].retry);
  EXPECT_TRUE(r.sent[4].retry);
}

// Every command of an apply and of its re-read has its own sender timestamp, strictly above the
// one before, even when the clock has not moved a second between them (the region loader's
// lines expect no reply and go out as fast as the budget allows): an equal timestamp is a retry
// the repeater does not run (MyMesh::onPeerDataRecv:709-711).
TEST(ConfigApply, TheSenderTimestampsAreStrictlyIncreasingAcrossTheApplyAndItsReRead) {
  FakeRepeater r;
  r.answer("set advert.interval 120", "OK");
  r.answer("set flood.max 100", "OK");
  r.answer("set repeat off", "OK - repeat is now OFF");
  mm::ConfigApplyOutcome out;
  mm::ConfigSnapshot reRead;
  const mm::ApplyRun run = mm::runConfigApply(threeCommands(), r, out, reRead);
  EXPECT_EQ(run, mm::ApplyRun::Ok);
  ASSERT_EQ(r.sent.size(), 7u) << "three sets, three gets, ver";
  for (size_t i = 1; i < r.sent.size(); i++) {
    EXPECT_GT(r.sent[i].timestamp, r.sent[i - 1].timestamp) << "line " << i << ": " << r.sent[i].line;
  }
  EXPECT_EQ(r.sent[0].timestamp, r.now) << "the first is the clock's second";
  EXPECT_EQ(r.sent[6].timestamp, r.now + 6) << "and the clock never moved: each one is one above the last";
  EXPECT_TRUE(out.allOk());
  EXPECT_EQ(out.ok(), 3);
}

// A dropped reply is `no-reply`, stops the sequence, and is never sent again
// (ConfigApplyHandlerTests.A_dropped_reply_is_no_reply_stops_the_sequence_and_is_never_sent_again).
TEST(ConfigApply, ASilenceIsNoReplyStopsTheSequenceAndIsNeverSentAgain) {
  FakeRepeater r;
  r.answer("set advert.interval 120", "OK");
  r.eat("set flood.max 100");
  r.answer("get advert.interval", "> 120");
  r.answer("get flood.max", "> 64");
  r.answer("ver", "v1.17.1");
  mm::ConfigApplyOutcome out;
  mm::ConfigSnapshot reRead;
  const mm::ApplyRun run = mm::runConfigApply(threeCommands(), r, out, reRead);
  EXPECT_EQ(run, mm::ApplyRun::Failed);
  EXPECT_EQ(r.timesSent("set flood.max 100"), 1) << "sent once, whatever came back";
  EXPECT_EQ(r.timesSent("set repeat off"), 0);
  EXPECT_EQ(out.commands[1].outcome, mm::CommandOutcome::NoReply);
  EXPECT_FALSE(out.commands[1].hasReply);
  EXPECT_EQ(out.commands[2].outcome, mm::CommandOutcome::NotSent);
  // The key whose reply was lost is read back too: the value may have applied (§5a).
  ASSERT_EQ(reRead.count, 2);
  EXPECT_STREQ(reRead.values[1].key, "flood.max");
  EXPECT_EQ(out.sent(), 2);
  EXPECT_EQ(out.ok(), 1);
}

// The region sequence: the lines that expect no reply are sent without waiting, the answered
// ones are one outcome with every reply joined, and the next command follows.
TEST(ConfigApply, TheRegionSequenceIsOneEntryWithEveryReplyItGotAndItsSilentLinesAreNotWaitedFor) {
  FakeRepeater r;
  r.answer("set advert.interval 120", "OK");
  r.answer(" ", "OK - loaded 2 regions");
  r.answer("region home dk", " home is now dk");
  r.answer("region default dk", " default scope is now dk");
  r.answer("region save", "OK");
  r.answer("set repeat off", "OK - repeat is now OFF");
  r.answer("get advert.interval", "> 120");
  r.answer("get repeat", "> off");
  r.answer("ver", "v1.17.1");
  const mm::ConfigApplyPlan plan = withRegions();
  mm::ConfigApplyOutcome out;
  mm::ConfigSnapshot reRead;
  const mm::ApplyRun run = mm::runConfigApply(plan, r, out, reRead);
  EXPECT_EQ(run, mm::ApplyRun::Ok);
  EXPECT_EQ(r.lines(), (std::vector<std::string>{"set advert.interval 120", "region load", " eu F", "  dk F", " ",
                                                  "region home dk", "region default dk", "region save", "set repeat off",
                                                  "get advert.interval", "region", "region default", "get repeat", "ver"}));
  EXPECT_FALSE(r.sent[1].awaited) << "region load answers nothing";
  EXPECT_FALSE(r.sent[2].awaited) << "a region line answers nothing";
  EXPECT_FALSE(r.sent[3].awaited);
  EXPECT_TRUE(r.sent[4].awaited) << "the blank line answers the count";
  EXPECT_TRUE(r.sent[4].escape);
  ASSERT_EQ(out.commandCount, 3);
  EXPECT_EQ(out.commands[1].outcome, mm::CommandOutcome::Ok);
  EXPECT_STREQ(out.commands[1].reply, "OK - loaded 2 regions | home is now dk | default scope is now dk | OK");
  EXPECT_EQ(out.commands[2].outcome, mm::CommandOutcome::Ok);
  ASSERT_EQ(reRead.count, 3);
  EXPECT_STREQ(reRead.values[1].key, "regions");
  EXPECT_STREQ(reRead.values[1].value, "* F\n dk F\ndefault dk");
  EXPECT_EQ(out.ok(), 3);
  EXPECT_TRUE(out.allOk());
  for (size_t i = 1; i < r.sent.size(); i++) {
    EXPECT_GT(r.sent[i].timestamp, r.sent[i - 1].timestamp) << "line " << i << ": " << r.sent[i].line;
  }
}

// A region sequence that dies part-way still sends the blank line that ends the load - until it
// arrives every CLI command that repeater hears is read as a region - and nothing else of it;
// the commands after it are not sent.
TEST(ConfigApply, TheRegionSequenceSendsTheTerminatorAfterAFailureAndNothingElseOfIt) {
  FakeRepeater r;
  r.answer("set advert.interval 120", "OK");
  r.refuseToBuild("  dk F");
  r.answer(" ", "OK - loaded 1 regions");
  r.answer("get advert.interval", "> 120");
  r.answer("ver", "v1.17.1");
  const mm::ConfigApplyPlan plan = withRegions();
  mm::ConfigApplyOutcome out;
  mm::ConfigSnapshot reRead;
  const mm::ApplyRun run = mm::runConfigApply(plan, r, out, reRead);
  EXPECT_EQ(run, mm::ApplyRun::Failed);
  // An entry that never left whole is `not-sent`, as the .NET handler records it, and a
  // not-sent key is not read back: only the plain key and `ver`.
  EXPECT_EQ(r.lines(), (std::vector<std::string>{"set advert.interval 120", "region load", " eu F", "  dk F", " ",
                                                  "get advert.interval", "ver"}));
  EXPECT_EQ(r.timesSent(" "), 1) << "the terminator, exactly once";
  EXPECT_TRUE(r.sent[4].escape);
  EXPECT_EQ(r.timesSent("region home dk"), 0);
  EXPECT_EQ(r.timesSent("region save"), 0);
  EXPECT_EQ(r.timesSent("set repeat off"), 0);
  EXPECT_EQ(out.commands[1].outcome, mm::CommandOutcome::NotSent);
  EXPECT_TRUE(out.commands[1].hasReply) << "the reason the line did not go is the record";
  EXPECT_NE(std::string(out.commands[1].reply).find("could not be built"), std::string::npos);
  EXPECT_EQ(out.commands[2].outcome, mm::CommandOutcome::NotSent);
  // The re-read: a not-sent entry was never touched, so only the plain key and ver.
  EXPECT_EQ(r.timesSent("region"), 0) << "regions was not sent, so it is not read back";
  EXPECT_EQ(out.sent(), 1);
}

// The terminator goes out even when the sequence died of the lease: a repeater left in load mode
// is worse than one packet past a lease. And the job is `timeout`, with its data.
TEST(ConfigApply, TheTerminatorGoesOutEvenWhenTheLeaseRanOutUnderTheSequence) {
  FakeRepeater r;
  r.answer("set advert.interval 120", "OK");
  r.leaseOutAt("  dk F");
  r.answer(" ", "OK - loaded 1 regions");
  const mm::ConfigApplyPlan plan = withRegions();
  mm::ConfigApplyOutcome out;
  mm::ConfigSnapshot reRead;
  const mm::ApplyRun run = mm::runConfigApply(plan, r, out, reRead);
  EXPECT_EQ(run, mm::ApplyRun::Timeout);
  EXPECT_EQ(r.timesSent(" "), 1);
  EXPECT_EQ(out.commands[1].outcome, mm::CommandOutcome::NotSent);
  EXPECT_NE(std::string(out.commands[1].reply).find("lease ran out"), std::string::npos);
}

// The re-read stops at the first key the repeater does not answer at all; what was read stands
// and `ver` is not asked for.
TEST(ConfigApply, TheReReadStopsAtTheFirstSilenceAndTheJobIsStillOkWhenEverySetWasAnswered) {
  FakeRepeater r;
  r.answer("set advert.interval 120", "OK");
  r.answer("set flood.max 100", "OK");
  r.answer("set repeat off", "OK - repeat is now OFF");
  r.answer("get advert.interval", "> 120");
  r.eat("get flood.max");
  mm::ConfigApplyOutcome out;
  mm::ConfigSnapshot reRead;
  const mm::ApplyRun run = mm::runConfigApply(threeCommands(), r, out, reRead);
  EXPECT_EQ(run, mm::ApplyRun::Ok) << "the re-read is the server's evidence, not the job's outcome";
  EXPECT_TRUE(out.reReadStopped);
  EXPECT_EQ(r.timesSent("get repeat"), 0);
  EXPECT_EQ(r.timesSent("ver"), 0);
  ASSERT_EQ(reRead.count, 1);
  EXPECT_STREQ(reRead.firmware, "");
}

// A `get` the repeater answers with something other than `> value` is a raw line, command in
// front, as ConfigReadHandler records one.
TEST(ConfigApply, AReReadThatIsNotAValueIsARawLineWithTheCommandInFront) {
  FakeRepeater r;
  r.answer("set flood.max 100", "OK");
  r.answer("get flood.max", "??: flood.max");
  r.answer("ver", "v1.17.1");
  mm::ConfigApplyPlan plan;
  plan.clear();
  plan.addCommand("flood.max", "set flood.max 100");
  mm::ConfigApplyOutcome out;
  mm::ConfigSnapshot reRead;
  mm::runConfigApply(plan, r, out, reRead);
  EXPECT_EQ(reRead.count, 0);
  ASSERT_EQ(reRead.rawCount, 1);
  EXPECT_STREQ(reRead.raw[0], "get flood.max: ??: flood.max");
}

// ------------------------------------------------------------------ the result

// ConfigApplyResult.json, byte for byte: the sample ContractsTests writes, built as the outcome
// the engine would leave for those replies.
TEST(ConfigApplyResult, TheSameOutcomeMakesTheDotNetFixtureByteForByte) {
  mm::ConfigApplyPlan plan;
  plan.clear();
  ASSERT_TRUE(plan.addCommand("repeat", "set repeat on"));
  ASSERT_TRUE(plan.addCommand("flood.max", "set flood.max 100"));
  ASSERT_TRUE(plan.addCommand("name", "set name X"));
  mm::ConfigApplyOutcome out;
  out.clear();
  out.commandCount = 3;
  out.commands[0].outcome = mm::CommandOutcome::Ok;
  out.commands[0].setReply("OK - repeat is now ON");
  out.commands[1].outcome = mm::CommandOutcome::Error;
  out.commands[1].setReply("Error, max 64");
  out.commands[2].outcome = mm::CommandOutcome::NotSent;
  mm::ConfigSnapshot reRead;
  reRead.clear();
  reRead.put("repeat", "on");
  reRead.put("flood.max", "64");
  reRead.addRaw("get flood.max.advert: ??: flood.max.advert");
  std::snprintf(reRead.firmware, sizeof reRead.firmware, "v1.17.1 (Build: 14 Aug 2026)");

  EXPECT_EQ(resultJson(plan, out, reRead), fixture("ConfigApplyResult"));
}

// The same through the engine: the replies of the fixture, and the bytes of the fixture's
// `commands` come out of the run rather than out of a hand-built outcome.
TEST(ConfigApplyResult, TheEngineMakesTheFixturesCommandsForTheFixturesReplies) {
  FakeRepeater r;
  r.answer("set repeat on", "OK - repeat is now ON");
  r.answer("set flood.max 100", "Error, max 64");
  r.answer("get repeat", "> on");
  r.answer("get flood.max", "> 64");
  r.answer("ver", "v1.17.1 (Build: 14 Aug 2026)");
  mm::ConfigApplyPlan plan;
  plan.clear();
  plan.addCommand("repeat", "set repeat on");
  plan.addCommand("flood.max", "set flood.max 100");
  plan.addCommand("name", "set name X");
  mm::ConfigApplyOutcome out;
  mm::ConfigSnapshot reRead;
  mm::runConfigApply(plan, r, out, reRead);
  const std::string json = resultJson(plan, out, reRead);
  const std::string expected = fixture("ConfigApplyResult");
  // Everything but the raw line, which the fixture's sample invents for a key no command sent.
  EXPECT_EQ(json.substr(0, json.find("\"reReadRaw\"")), expected.substr(0, expected.find("\"reReadRaw\"")));
  EXPECT_EQ(json.substr(json.find("\"firmware\"")), expected.substr(expected.find("\"firmware\"")));
  EXPECT_NE(json.find("\"reReadRaw\":[]"), std::string::npos);
}

TEST(ConfigApplyResult, AnEmptyReReadIsAnEmptyObjectAndAFirmwareNotReadIsLeftOut) {
  mm::ConfigApplyPlan plan;
  plan.clear();
  plan.addCommand("name", "set name X");
  mm::ConfigApplyOutcome out;
  out.clear();
  out.commandCount = 1;
  out.commands[0].outcome = mm::CommandOutcome::NoReply;
  mm::ConfigSnapshot reRead;
  reRead.clear();
  EXPECT_EQ(resultJson(plan, out, reRead),
            "{\"commands\":[{\"key\":\"name\",\"command\":\"set name X\",\"outcome\":\"no-reply\",\"isOk\":false,"
            "\"wasSent\":true}],\"reRead\":{},\"reReadRaw\":[],\"sent\":1,\"ok\":0,\"allOk\":false}");
}

// ------------------------------------------------------------------ the plan's bounds

TEST(ConfigApplyPlan, APlanThisFirmwareCannotHoldIsRefusedByNameAndNothingOfItIsSent) {
  mm::ConfigApplyPlan plan;
  plan.clear();
  for (int i = 0; i < mm::kMaxApplyCommands; i++) {
    char key[16];
    std::snprintf(key, sizeof key, "k%d", i);
    EXPECT_TRUE(plan.addCommand(key, "set x 1")) << i;
  }
  EXPECT_FALSE(plan.addCommand("one more", "set x 1"));
  EXPECT_TRUE(plan.refused);
  EXPECT_NE(std::string(plan.refusedWhy).find("more than"), std::string::npos) << plan.refusedWhy;

  mm::ConfigApplyPlan lines;
  lines.clear();
  lines.addCommand("regions", "region load");
  for (int i = 0; i < mm::kMaxApplyLines; i++) EXPECT_TRUE(lines.addLine(" r", nullptr, false)) << i;
  EXPECT_FALSE(lines.addLine(" r", nullptr, false));
  EXPECT_TRUE(lines.refused);

  mm::ConfigApplyPlan broken;
  broken.clear();
  EXPECT_FALSE(broken.addCommand("name", "set name\nreboot"));
  EXPECT_TRUE(broken.refused);
  EXPECT_NE(std::string(broken.refusedWhy).find("line break"), std::string::npos) << broken.refusedWhy;

  mm::ConfigApplyPlan empty;
  empty.clear();
  EXPECT_FALSE(empty.addCommand("", "set name X"));
  EXPECT_TRUE(empty.refused);
  EXPECT_FALSE(empty.addCommand("name", ""));

  mm::ConfigApplyPlan blank;
  blank.clear();
  blank.addCommand("regions", "region load");
  EXPECT_FALSE(blank.addLine("", nullptr, false)) << "an empty line would never reach handleCommand";
  EXPECT_TRUE(blank.refused);
}

TEST(ConfigApplyPlan, TheLinesAndTheirExpectationsAreReadBackAsTheyWerePut) {
  const mm::ConfigApplyPlan plan = withRegions();
  ASSERT_EQ(plan.commandCount, 3);
  EXPECT_EQ(plan.commands[0].lineCount, 0) << "a plain set is its one command";
  EXPECT_EQ(plan.commands[1].lineCount, 7);
  EXPECT_EQ(plan.commands[1].firstLine, 0);
  EXPECT_STREQ(plan.lineText(0), "region load");
  EXPECT_EQ(plan.lineExpect(0), nullptr);
  EXPECT_STREQ(plan.lineText(3), " ");
  EXPECT_STREQ(plan.lineExpect(3), "OK - loaded");
  EXPECT_TRUE(plan.lines[3].escape);
  EXPECT_STREQ(plan.lineText(6), "region save");
  EXPECT_STREQ(plan.lineExpect(6), "OK");
  EXPECT_FALSE(plan.lines[6].escape);
}

// ------------------------------------------------------------------ CliReplies

// Every spelling CommonCLI at d9296435 refuses with, and the answers that are not refusals.
TEST(CliReplies, TheHardErrorsAreCommonClisSpellingsAndAnAnswerIsNotOne) {
  EXPECT_TRUE(mm::cliReplyIsHardError("Error, max 64"));
  EXPECT_TRUE(mm::cliReplyIsHardError("ERROR: dutycycle must be 1-100"));
  EXPECT_TRUE(mm::cliReplyIsHardError("Error: interval range is 60-240 minutes"));
  EXPECT_TRUE(mm::cliReplyIsHardError("ERR: clock cannot go backwards"));
  EXPECT_TRUE(mm::cliReplyIsHardError("(ERR: clock cannot go backwards)"));
  EXPECT_TRUE(mm::cliReplyIsHardError("unknown config: foo"));
  EXPECT_TRUE(mm::cliReplyIsHardError("??: foo"));
  EXPECT_TRUE(mm::cliReplyIsHardError("Unknown command"));
  EXPECT_TRUE(mm::cliReplyIsHardError("Err - bad params"));
  EXPECT_TRUE(mm::cliReplyIsHardError("  Error, bad chars")) << "leading whitespace is skipped";
  EXPECT_FALSE(mm::cliReplyIsHardError("OK"));
  EXPECT_FALSE(mm::cliReplyIsHardError("OK - Advert sent"));
  EXPECT_FALSE(mm::cliReplyIsHardError("> TwinOak Risskov"));
  EXPECT_FALSE(mm::cliReplyIsHardError("14:05 - 23/9/2026 UTC"));
  EXPECT_FALSE(mm::cliReplyIsHardError("v1.17.1 (Build: 14 Aug 2026)"));
  EXPECT_FALSE(mm::cliReplyIsHardError("a1 42 7.25"));
  EXPECT_FALSE(mm::cliReplyIsHardError("password now: x"));
  EXPECT_FALSE(mm::cliReplyIsHardError(nullptr)) << "silence is not a reply and not an error either";
  EXPECT_FALSE(mm::cliReplyIsHardError(""));
}

TEST(CliReplies, AConfirmationStartsWithWhatTheLineExpectsOrdinal) {
  EXPECT_TRUE(mm::cliReplyIsConfirmed("OK"));
  EXPECT_TRUE(mm::cliReplyIsConfirmed("OK - repeat is now OFF"));
  EXPECT_TRUE(mm::cliReplyIsConfirmed("  OK - 50.0%"));
  EXPECT_FALSE(mm::cliReplyIsConfirmed("ok")) << "ordinal: the firmware writes OK";
  EXPECT_FALSE(mm::cliReplyIsConfirmed("(OK - stats reset)"));
  EXPECT_FALSE(mm::cliReplyIsConfirmed("Error, max 64"));
  EXPECT_FALSE(mm::cliReplyIsConfirmed(nullptr));
  EXPECT_TRUE(mm::cliReplyIsConfirmed(" home is now dk", "home is now"));
  EXPECT_TRUE(mm::cliReplyIsConfirmed(" default scope is now dk", "default scope is now"));
  EXPECT_TRUE(mm::cliReplyIsConfirmed("OK - loaded 5 regions", "OK - loaded"));
  EXPECT_FALSE(mm::cliReplyIsConfirmed("Err - unknown region", "home is now"));
}

// ------------------------------------------------------------------ the writes on the board

// A host cannot run JobRunner, so the two orders that matter are held on its text (as
// test_credentials holds the wipe): the password line is sent, then the restore, then the
// session is dropped; and a reboot marks the restore done before its command and never sends
// one.
TEST(TheWritesOnTheBoard, PasswordSetRestoresAfterThePasswordLineAndThenDropsTheSession) {
  const std::string runner = readFile(fs::path(PROJECT_DIR) / "src/agent/JobRunner.cpp");
  const size_t handler = runner.find("Outcome JobRunner::runPasswordSet(");
  const size_t next = runner.find("Outcome JobRunner::runReboot(", handler);
  ASSERT_NE(handler, std::string::npos);
  ASSERT_NE(next, std::string::npos);
  const std::string body = runner.substr(handler, next - handler);

  const size_t line = body.find("cliLine(job, ctx, what, g_secretLine, false, 0, true)");
  const size_t wiped = body.find("wipeBytes(g_secretLine, sizeof g_secretLine);");
  const size_t restore = body.find("restoreAcl(job, ctx, sent);");
  const size_t dropped = body.find("sessions_.invalidate(job.repeaterKey);");
  ASSERT_NE(line, std::string::npos) << "the password line, sent once and marked sensitive";
  ASSERT_NE(wiped, std::string::npos);
  ASSERT_NE(restore, std::string::npos);
  ASSERT_NE(dropped, std::string::npos);
  EXPECT_LT(line, wiped) << "the secret line is wiped right after the send";
  EXPECT_LT(wiped, restore) << "the restore comes after the password line";
  EXPECT_LT(restore, dropped) << "and only then is the session dropped";
  EXPECT_EQ(body.find("restoreAcl(", 0), restore) << "one restore, after the line";
  // The echo is compared and never recorded: the reply buffer is wiped before the restore's
  // reply could overwrite it, and the result carries the redacted sentence.
  const size_t replyWiped = body.find("wipeBytes(g_cliReply, sizeof g_cliReply);");
  ASSERT_NE(replyWiped, std::string::npos);
  EXPECT_LT(replyWiped, restore);
  EXPECT_NE(body.find("kPasswordSetAdminReplyRedacted"), std::string::npos);
  // No log line takes the new password or the secret line as an argument: every g_log.add of
  // the handler is read for the two names.
  size_t at = 0;
  int logLines = 0;
  while ((at = body.find("g_log.add(", at)) != std::string::npos) {
    const size_t end = body.find(");", at);
    const std::string call = body.substr(at, end - at);
    EXPECT_EQ(call.find("newPassword"), std::string::npos) << call;
    EXPECT_EQ(call.find("g_secretLine"), std::string::npos) << call;
    logLines++;
    at = end;
  }
  EXPECT_GT(logLines, 5);
}

TEST(TheWritesOnTheBoard, RebootSendsNoRestoreAndDropsTheSession) {
  const std::string runner = readFile(fs::path(PROJECT_DIR) / "src/agent/JobRunner.cpp");
  const size_t handler = runner.find("Outcome JobRunner::runReboot(");
  const size_t next = runner.find("// ------------------------------------------------------------------ the runner", handler);
  ASSERT_NE(handler, std::string::npos);
  ASSERT_NE(next, std::string::npos);
  const std::string body = runner.substr(handler, next - handler);

  const size_t skipped = body.find("ctx.aclRestoreDone = true;");
  const size_t command = body.find("cliLine(job, ctx, \"cli 'reboot'\", \"reboot\", false)");
  const size_t dropped = body.find("sessions_.invalidate(job.repeaterKey);");
  ASSERT_NE(skipped, std::string::npos) << "the restore is marked done, with the reason logged";
  ASSERT_NE(command, std::string::npos);
  ASSERT_NE(dropped, std::string::npos);
  EXPECT_LT(skipped, command) << "before the command, so the runner sends none after it either";
  EXPECT_LT(command, dropped);
  EXPECT_EQ(body.find("restoreAcl("), std::string::npos) << "no restore is ever sent around a reboot";
}

// Every write goes through the single send: `cliLine(..., false)` or the engine's `retry`
// false, never the bounded retry a read gets.
TEST(TheWritesOnTheBoard, EveryWriteIsSentOnce) {
  const std::string runner = readFile(fs::path(PROJECT_DIR) / "src/agent/JobRunner.cpp");
  const size_t from = runner.find("class JobRunner::ApplyOnTheAir");
  const size_t to = runner.find("// ------------------------------------------------------------------ the runner", from);
  ASSERT_NE(from, std::string::npos);
  ASSERT_NE(to, std::string::npos);
  const std::string writes = runner.substr(from, to - from);
  // The engine's lines are sent with the retry the engine asked for (its sets never retry, its
  // gets do); every other cliLine in the writes is `false`.
  size_t at = 0;
  int calls = 0;
  while ((at = writes.find("cliLine(job", at)) != std::string::npos) {
    const size_t end = writes.find(");", at);
    const std::string call = writes.substr(at, end - at);
    if (call.find("s.retry") == std::string::npos) {
      EXPECT_NE(call.find(", false"), std::string::npos) << call;
    }
    calls++;
    at = end;
  }
  EXPECT_EQ(calls, 6) << "the engine's line, cli, advert, clock, password, reboot";
  EXPECT_EQ(writes.find("request(job"), std::string::npos) << "no write is a REQ";
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
