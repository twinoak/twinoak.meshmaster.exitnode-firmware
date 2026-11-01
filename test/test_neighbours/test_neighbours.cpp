// The neighbour table and the scope sweep (tasks/phase-1.8/33), on a host: which adverts make a
// neighbour and which do not, the 21st replacing the oldest, a discovery response entering; the
// bytes of the two requests and the reading of a regions reply (bounded, valid UTF-8, cut at the
// padding); and the sweep over a fake mesh and a clock the test moves - nothing on the air while
// a job is open, every request through the airtime gate, a silent neighbour `timeout`, newest
// first, the schedule and the `nbr:` line. And a grep of src/mesh/AgentMesh.cpp, because that file
// needs a board to run: the regions request goes out direct on an empty path and nothing zero-hop
// of MeshCore's is called.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include <agent/Airtime.h>
#include <agent/Observations.h>

#include "observer/Neighbours.h"

#ifndef PROJECT_DIR
#error "PROJECT_DIR must be defined; platformio.ini's [env:native] does it"
#endif

using namespace mm::observer;

namespace {

namespace fs = std::filesystem;

uint32_t g_millis = 1000;
uint32_t fakeMillis() { return g_millis; }

// A key whose bytes are all `b`, for a table that needs twenty different ones.
void keyOf(uint8_t b, uint8_t out[kNeighbourKeyLen]) { std::memset(out, b, kNeighbourKeyLen); }

std::string readWhole(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The mesh behind the seam: records what the sweep asked, answers as the test says.
struct FakeLink : NeighbourLink {
  bool isBusy = false;
  int discoveries = 0;
  uint32_t lastWindowMs = 0;
  bool discoveryFails = false;
  std::vector<std::vector<uint8_t>> queries;  // the keys asked, in order
  std::map<uint8_t, std::string> answers;     // key byte -> scopes; absent is silence
  std::map<uint8_t, bool> sendFails;
  uint32_t timeoutMs = 2500;
  std::vector<uint32_t> queryAtMs;

  bool busy() const override { return isBusy; }
  bool sendDiscovery(uint32_t windowMs) override {
    discoveries++;
    lastWindowMs = windowMs;
    return !discoveryFails;
  }
  QueryStatus queryRegions(const uint8_t key[kNeighbourKeyLen], char* scopes, size_t cap) override {
    queries.emplace_back(key, key + kNeighbourKeyLen);
    queryAtMs.push_back(g_millis);
    scopes[0] = '\0';
    if (sendFails.count(key[0]) != 0) return QueryStatus::SendFailed;
    const auto it = answers.find(key[0]);
    if (it == answers.end()) return QueryStatus::Timeout;  // a silent neighbour
    std::snprintf(scopes, cap, "%s", it->second.c_str());
    return QueryStatus::Responded;
  }
  uint32_t queryTimeoutMs() const override { return timeoutMs; }
};

struct Rig {
  NeighbourTable table;
  FakeLink link;
  mm::AirtimeGate gate;
  NeighbourSweep sweep;

  Rig() {
    g_millis = 1000;
    sweep.begin(table, link, &gate, &fakeMillis);
    SweepSettings s;
    s.enabled = true;
    s.intervalHours = 24;
    sweep.configure(s);
  }
  // Loop once, then move the clock.
  bool tick(uint32_t ms = 0, bool slot = true) {
    const bool done = sweep.loop(slot);
    g_millis += ms;
    return done;
  }
  void addNeighbour(uint8_t b, int8_t snrQ4 = 20, uint32_t heardAt = 0) {
    uint8_t key[kNeighbourKeyLen];
    keyOf(b, key);
    table.put(key, snrQ4, -90, heardAt == 0 ? g_millis : heardAt);
  }
  // Run the sweep until the cycle finishes or `maxTicks` passes, one second a tick.
  bool runCycle(int maxTicks = 400) {
    for (int i = 0; i < maxTicks; i++) {
      if (tick(1000)) return true;
    }
    return false;
  }
};

}  // namespace

// ---- the table ------------------------------------------------------------------------------

TEST(Table, AZeroHopRepeaterAdvertEntersAOneHopOneDoesNotAndAChatNodesDoesNot) {
  NeighbourTable t;
  uint8_t key[kNeighbourKeyLen];
  keyOf(0x11, key);
  EXPECT_TRUE(t.offer(key, kAdvTypeRepeater, 0, 39, -87, 5000));
  EXPECT_EQ(1, t.count());
  ASSERT_NE(nullptr, t.find(key));
  EXPECT_EQ(39, t.find(key)->snrQ4);
  EXPECT_EQ(-87, t.find(key)->rssi);
  EXPECT_EQ(5000u, t.find(key)->heardAtMs);

  keyOf(0x22, key);
  EXPECT_FALSE(t.offer(key, kAdvTypeRepeater, 1, 39, -87, 5000)) << "one hop: heard through another repeater";
  EXPECT_FALSE(t.offer(key, 1 /* ADV_TYPE_CHAT */, 0, 39, -87, 5000)) << "a chat node at zero hops is not a repeater";
  EXPECT_FALSE(t.offer(key, 3 /* ADV_TYPE_ROOM */, 0, 39, -87, 5000));
  EXPECT_FALSE(t.offer(key, mm::kAdvTypeUnknown, 0, 39, -87, 5000));
  EXPECT_EQ(1, t.count());

  // The same repeater again refreshes its entry rather than taking a second one.
  keyOf(0x11, key);
  EXPECT_TRUE(t.offer(key, kAdvTypeRepeater, 0, 12, -95, 9000));
  EXPECT_EQ(1, t.count());
  EXPECT_EQ(12, t.find(key)->snrQ4);
  EXPECT_EQ(9000u, t.find(key)->heardAtMs);
}

TEST(Table, TheTwentyFirstReplacesTheOneHeardLongestAgo) {
  NeighbourTable t;
  uint8_t key[kNeighbourKeyLen];
  for (int i = 0; i < kNeighbourCap; i++) {
    keyOf(static_cast<uint8_t>(0x10 + i), key);
    // Heard at 10 s, 20 s, ... ; the one at 0x15 is made the oldest by hand below.
    t.put(key, 10, -80, 10000u * static_cast<uint32_t>(i + 1));
  }
  EXPECT_EQ(kNeighbourCap, t.count());
  keyOf(0x15, key);
  t.put(key, 10, -80, 500);  // the oldest now

  keyOf(0xEE, key);
  t.put(key, 30, -70, 999000);
  EXPECT_EQ(kNeighbourCap, t.count()) << "a fixed array: never a 21st";
  EXPECT_NE(nullptr, t.find(key));
  keyOf(0x15, key);
  EXPECT_EQ(nullptr, t.find(key)) << "the one heard longest ago went";
  keyOf(0x10, key);
  EXPECT_NE(nullptr, t.find(key)) << "the others stayed";
}

TEST(Table, ADiscoveryResponseEntersAndIsReadByTagAndType) {
  // onControlDataRecv at d664d89d:972-991: the type nibble 0x90 with the node type in the low
  // nibble, the inbound SNR, the request's tag, the full key.
  uint8_t key[kNeighbourKeyLen];
  keyOf(0x77, key);
  uint8_t payload[6 + kNeighbourKeyLen];
  payload[0] = static_cast<uint8_t>(kCtlNodeDiscoverResp | kAdvTypeRepeater);
  payload[1] = 24;
  const uint32_t tag = 0xA1B2C3D4u;
  std::memcpy(&payload[2], &tag, 4);
  std::memcpy(&payload[6], key, kNeighbourKeyLen);

  uint8_t got[kNeighbourKeyLen];
  EXPECT_TRUE(readDiscoveryResponse(payload, sizeof payload, tag, got));
  EXPECT_EQ(0, std::memcmp(got, key, kNeighbourKeyLen));
  EXPECT_FALSE(readDiscoveryResponse(payload, sizeof payload, tag + 1, got)) << "another request's tag";
  EXPECT_FALSE(readDiscoveryResponse(payload, 6 + 8, tag, got)) << "a prefix-only answer is too short";
  payload[0] = static_cast<uint8_t>(kCtlNodeDiscoverResp | 1);
  EXPECT_FALSE(readDiscoveryResponse(payload, sizeof payload, tag, got)) << "a chat node's answer";
  payload[0] = static_cast<uint8_t>(kCtlNodeDiscoverReq | kAdvTypeRepeater);
  EXPECT_FALSE(readDiscoveryResponse(payload, sizeof payload, tag, got)) << "a request, not a response";

  NeighbourTable t;
  t.put(key, 24, kNeighbourNoRssi, 4000);
  EXPECT_EQ(1, t.count());
  EXPECT_EQ(kNeighbourNoRssi, t.find(key)->rssi);
}

// ---- the bytes --------------------------------------------------------------------------------

TEST(Bytes, TheDiscoveryRequestIsTheRepeatersOwn) {
  // sendNodeDiscoverReq (MyMesh.cpp:1000-1013 at d664d89d, :846-859 at the pin): type 0x80 with
  // prefix_only 0, the filter bit for ADV_TYPE_REPEATER, the tag, since 0.
  uint8_t out[kDiscoveryRequestLen];
  EXPECT_EQ(kDiscoveryRequestLen, discoveryRequest(0x04030201u, out));
  const uint8_t expected[kDiscoveryRequestLen] = {0x80, 0x04, 0x01, 0x02, 0x03, 0x04, 0, 0, 0, 0};
  EXPECT_EQ(0, std::memcmp(out, expected, sizeof expected));
}

TEST(Bytes, TheRegionsRequestsPlaintextIsTheTagThenOneThenZero) {
  // sendAnonRegionsReq (MyMesh.cpp:1968-1971): {tag(4)}{ANON_REQ_TYPE_REGIONS}{0x00}. The 0x00 is
  // a zero-length reply path, which the repeater reads as "reply zero hop" (handleAnonRegionsReq).
  uint8_t out[kRegionsRequestLen];
  EXPECT_EQ(kRegionsRequestLen, regionsRequest(0xDDCCBBAAu, out));
  const uint8_t expected[kRegionsRequestLen] = {0xAA, 0xBB, 0xCC, 0xDD, 0x01, 0x00};
  EXPECT_EQ(0, std::memcmp(out, expected, sizeof expected));
}

TEST(Bytes, ARegionsReplysNamesAreBoundedValidUtf8AndCutAtThePadding) {
  // The reply's data after the mesh stripped the tag: the repeater's clock, then the names, then
  // the datagram's zero padding to its cipher block.
  char scopes[kScopesCap + 1];
  {
    const uint8_t data[] = {1, 2, 3, 4, 'd', 'k', ',', 'd', 'k', '-', 'a', 'a', 'r', 'h', 'u', 's', 0, 0, 0, 0};
    EXPECT_EQ(12u, readRegionsReply(data, sizeof data, scopes, sizeof scopes));
    EXPECT_STREQ("dk,dk-aarhus", scopes);
  }
  {
    const uint8_t data[] = {1, 2, 3, 4};  // a repeater with no regions: eight bytes, no names
    EXPECT_EQ(0u, readRegionsReply(data, sizeof data, scopes, sizeof scopes));
    EXPECT_STREQ("", scopes);
    EXPECT_EQ(0u, readRegionsReply(data, 2, scopes, sizeof scopes)) << "shorter than the clock";
  }
  {
    // More than the consumer takes: cut at kScopesCap.
    std::vector<uint8_t> data = {1, 2, 3, 4};
    for (int i = 0; i < 300; i++) data.push_back(static_cast<uint8_t>('a' + (i % 26)));
    EXPECT_EQ(kScopesCap, readRegionsReply(data.data(), data.size(), scopes, sizeof scopes));
    EXPECT_EQ(kScopesCap, std::strlen(scopes));
  }
  {
    // A three-byte character cut in two by the end of the data: dropped whole, not half.
    const uint8_t data[] = {1, 2, 3, 4, 'd', 'k', ',', 0xE2, 0x82};
    EXPECT_EQ(3u, readRegionsReply(data, sizeof data, scopes, sizeof scopes));
    EXPECT_STREQ("dk,", scopes);
  }
  {
    // A small buffer: what fits, and a NUL.
    const uint8_t data[] = {1, 2, 3, 4, 'd', 'k', '-', 'f', 'y', 'n'};
    char small[4];
    EXPECT_EQ(3u, readRegionsReply(data, sizeof data, small, sizeof small));
    EXPECT_STREQ("dk-", small);
  }
}

TEST(Bytes, TheStatusWordsAreTheObserverFirmwares) {
  EXPECT_STREQ("responded", queryStatusName(QueryStatus::Responded));
  EXPECT_STREQ("timeout", queryStatusName(QueryStatus::Timeout));
  EXPECT_STREQ("send_failed", queryStatusName(QueryStatus::SendFailed));
  EXPECT_STREQ("", queryStatusName(QueryStatus::Unsent));
}

// ---- the sweep --------------------------------------------------------------------------------

TEST(Sweep, ACycleIsADiscoveryWindowThenOneQueryPerNeighbourNewestFirst) {
  Rig r;
  r.addNeighbour(0x30, 8, 100);   // oldest
  r.addNeighbour(0x10, 20, 900);  // newest
  r.addNeighbour(0x20, 40, 500);
  r.addNeighbour(0x21, 44, 500);  // as recent as 0x20, stronger: before it
  r.link.answers[0x10] = "dk,dk-aarhus";
  r.link.answers[0x20] = "dk";
  r.link.answers[0x21] = "";  // a repeater with no regions answers with nothing: still responded
  // 0x30 never answers

  ASSERT_TRUE(r.sweep.requestNow());
  EXPECT_FALSE(r.sweep.requestNow()) << "one at a time";
  r.tick();
  EXPECT_EQ(SweepPhase::Discovering, r.sweep.phase());
  r.tick();
  EXPECT_EQ(1, r.link.discoveries) << "the discovery goes out once, on the first pass the air is free";
  EXPECT_EQ(kDiscoveryWindowMs, r.link.lastWindowMs);
  EXPECT_EQ(0u, r.link.queries.size()) << "nothing is asked while the window is open";
  for (int i = 0; i < 59; i++) r.tick(1000);
  EXPECT_EQ(SweepPhase::Discovering, r.sweep.phase()) << "not before sixty seconds";
  r.tick(1000);
  r.tick();
  EXPECT_EQ(SweepPhase::Querying, r.sweep.phase());

  ASSERT_TRUE(r.runCycle());
  EXPECT_EQ(SweepPhase::Idle, r.sweep.phase());
  ASSERT_EQ(4u, r.link.queries.size());
  EXPECT_EQ(0x10, r.link.queries[0][0]) << "newest first";
  EXPECT_EQ(0x21, r.link.queries[1][0]) << "then the stronger of two equally recent";
  EXPECT_EQ(0x20, r.link.queries[2][0]);
  EXPECT_EQ(0x30, r.link.queries[3][0]);

  ASSERT_EQ(4, r.sweep.snapshotCount());
  EXPECT_EQ(QueryStatus::Responded, r.sweep.entry(0).status);
  EXPECT_STREQ("dk,dk-aarhus", r.sweep.entry(0).scopes);
  EXPECT_EQ(QueryStatus::Responded, r.sweep.entry(1).status);
  EXPECT_STREQ("", r.sweep.entry(1).scopes);
  EXPECT_EQ(QueryStatus::Timeout, r.sweep.entry(3).status) << "a silent neighbour is timeout";
  EXPECT_EQ(4, r.sweep.queriedCount());
  EXPECT_EQ(3, r.sweep.respondedCount());
  EXPECT_EQ(1u, r.sweep.cyclesRun());
}

TEST(Sweep, NothingGoesOnTheAirWhileAJobIsOpen) {
  Rig r;
  r.addNeighbour(0x10);
  r.addNeighbour(0x11);
  r.link.answers[0x10] = "dk";
  r.link.answers[0x11] = "dk";

  r.link.isBusy = true;
  ASSERT_TRUE(r.sweep.requestNow());
  for (int i = 0; i < 30; i++) r.tick(1000);
  EXPECT_EQ(0, r.link.discoveries) << "the discovery waits for the job";
  EXPECT_EQ(SweepPhase::Discovering, r.sweep.phase());
  r.link.isBusy = false;
  r.tick(1000);
  EXPECT_EQ(1, r.link.discoveries);

  // The window closes; a job opens again before the first query.
  for (int i = 0; i < 60; i++) r.tick(1000);
  EXPECT_EQ(SweepPhase::Querying, r.sweep.phase());
  r.link.isBusy = true;
  for (int i = 0; i < 30; i++) r.tick(1000);
  EXPECT_EQ(0u, r.link.queries.size()) << "no sweep frame while a job is open";
  r.link.isBusy = false;
  r.tick(1000);
  EXPECT_EQ(1u, r.link.queries.size());
  // And between two queries as well.
  r.link.isBusy = true;
  for (int i = 0; i < 30; i++) r.tick(1000);
  EXPECT_EQ(1u, r.link.queries.size());
  r.link.isBusy = false;
  ASSERT_TRUE(r.runCycle());
  EXPECT_EQ(2u, r.link.queries.size());
}

TEST(Sweep, EveryRequestWaitsForTheAirtimeGateAndIsCountedOnIt) {
  Rig r;
  mm::AgentBudget budget;
  budget.maxTransactionsPerMinute = 60;
  budget.minGapSeconds = 20;
  r.gate.setBudget(budget);
  for (uint8_t b = 0x10; b < 0x13; b++) {
    r.addNeighbour(b);
    r.link.answers[b] = "dk";
  }
  // A job's transaction has just gone out: the discovery waits the gap.
  r.gate.took(g_millis);
  ASSERT_TRUE(r.sweep.requestNow());
  r.tick(1000);
  for (int i = 0; i < 18; i++) r.tick(1000);
  EXPECT_EQ(0, r.link.discoveries) << "the gap since the last start";
  r.tick(1000);
  r.tick(1000);
  EXPECT_EQ(1, r.link.discoveries);
  EXPECT_EQ(2u, r.gate.transmissions()) << "the discovery is a transaction of the budget";

  ASSERT_TRUE(r.runCycle());
  ASSERT_EQ(3u, r.link.queryAtMs.size());
  EXPECT_GE(r.link.queryAtMs[1] - r.link.queryAtMs[0], 20000u) << "twenty seconds between two queries";
  EXPECT_GE(r.link.queryAtMs[2] - r.link.queryAtMs[1], 20000u);
  EXPECT_EQ(5u, r.gate.transmissions()) << "the discovery and three queries, on the jobs' own gate";
  // What the gate says a job would wait right after the sweep: the gap, as after any transaction.
  EXPECT_GT(r.gate.waitMs(r.link.queryAtMs[2] + 1000), 0u);
}

TEST(Sweep, ASendThatFailsIsSendFailedAndNotCountedAsQueried) {
  Rig r;
  r.addNeighbour(0x10);
  r.addNeighbour(0x11);
  r.link.answers[0x11] = "dk";
  r.link.sendFails[0x10] = true;
  ASSERT_TRUE(r.sweep.requestNow());
  ASSERT_TRUE(r.runCycle());
  ASSERT_EQ(2, r.sweep.snapshotCount());
  EXPECT_EQ(QueryStatus::SendFailed, r.sweep.entry(0).status);
  EXPECT_EQ(1, r.sweep.queriedCount()) << "queried_neighbors counts requests actually sent";
  EXPECT_EQ(1, r.sweep.respondedCount());
}

TEST(Sweep, ADiscoveryThatCannotBeMadeStillOpensTheWindowAndTheTableIsReported) {
  Rig r;
  r.addNeighbour(0x10);
  r.link.answers[0x10] = "dk";
  r.link.discoveryFails = true;
  ASSERT_TRUE(r.sweep.requestNow());
  ASSERT_TRUE(r.runCycle());
  EXPECT_EQ(1, r.link.discoveries);
  EXPECT_EQ(1, r.sweep.snapshotCount());
  EXPECT_EQ(QueryStatus::Responded, r.sweep.entry(0).status);
}

TEST(Sweep, AnEmptyTableIsACycleOfNoQueriesAndAReportOfNone) {
  Rig r;
  ASSERT_TRUE(r.sweep.requestNow());
  ASSERT_TRUE(r.runCycle());
  EXPECT_EQ(1, r.link.discoveries);
  EXPECT_EQ(0u, r.link.queries.size());
  EXPECT_EQ(0, r.sweep.snapshotCount());
}

TEST(Sweep, TheScheduleIsFiveMinutesAfterSwitchOnThenEveryIntervalAndOnlyWithASlot) {
  Rig r;
  r.addNeighbour(0x10);
  r.link.answers[0x10] = "dk";
  // Not due yet.
  for (int i = 0; i < 299; i++) r.tick(1000);
  EXPECT_EQ(SweepPhase::Idle, r.sweep.phase());
  // Due, but no slot is connected: the report would go nowhere, so the air is not spent.
  for (int i = 0; i < 30; i++) r.tick(1000, false);
  EXPECT_EQ(SweepPhase::Idle, r.sweep.phase());
  EXPECT_EQ(0, r.link.discoveries);
  char line[160];
  r.sweep.describe(line, sizeof line);
  EXPECT_STREQ("nbr: due, waiting for a slot, last none yet, 1 neighbours, 0 responded", line);
  // A slot connects: the cycle runs.
  r.tick(1000, true);
  EXPECT_EQ(SweepPhase::Discovering, r.sweep.phase());
  ASSERT_TRUE(r.runCycle());
  r.sweep.noteReport(ReportResult::Ok);
  // The next one is an interval away.
  r.sweep.describe(line, sizeof line);
  EXPECT_STREQ("nbr: next in 23h 59m, last ok 0 s ago, 1 neighbours, 1 responded", line);
  for (int i = 0; i < 24 * 3600 - 10; i++) r.tick(1000);
  EXPECT_EQ(SweepPhase::Idle, r.sweep.phase());
  for (int i = 0; i < 20; i++) r.tick(1000);
  EXPECT_EQ(SweepPhase::Discovering, r.sweep.phase()) << "the second cycle, 24 h after the first ended";
  r.sweep.describe(line, sizeof line);
  EXPECT_EQ(0u, std::string(line).find("nbr: running (discovery, ")) << line;
}

TEST(Sweep, AShorterIntervalCountsFromTheLastCycleAndOffCancelsAPeriodicCycleButNotAManualOne) {
  Rig r;
  r.addNeighbour(0x10);
  r.link.answers[0x10] = "dk";
  for (int i = 0; i < 301; i++) r.tick(1000);
  ASSERT_TRUE(r.runCycle());
  SweepSettings s;
  s.enabled = true;
  s.intervalHours = 12;
  r.sweep.configure(s);
  char line[160];
  r.sweep.describe(line, sizeof line);
  EXPECT_EQ(0u, std::string(line).find("nbr: next in 11h 59m")) << line;

  // Off: the schedule goes, and a periodic cycle that is running is dropped.
  for (int i = 0; i < 12 * 3600 + 5; i++) r.tick(1000);
  ASSERT_EQ(SweepPhase::Discovering, r.sweep.phase());
  s.enabled = false;
  r.sweep.configure(s);
  EXPECT_EQ(SweepPhase::Idle, r.sweep.phase());
  r.sweep.describe(line, sizeof line);
  EXPECT_EQ(0u, std::string(line).find("nbr: off (mqtt.neighbors off)")) << line;
  for (int i = 0; i < 48 * 3600; i += 600) r.tick(600000);
  EXPECT_EQ(1u, r.sweep.cyclesRun()) << "nothing runs while off";

  // A manual cycle runs while off, and keeps running if the setting is written again.
  ASSERT_TRUE(r.sweep.requestNow());
  r.tick(1000);
  r.tick(1000);
  ASSERT_EQ(SweepPhase::Discovering, r.sweep.phase());
  r.sweep.configure(s);
  EXPECT_EQ(SweepPhase::Discovering, r.sweep.phase());
  ASSERT_TRUE(r.runCycle());
  EXPECT_EQ(2u, r.sweep.cyclesRun());
}

TEST(Sweep, TheNbrLineNamesTheQueryInProgressAndTheLastReportsOutcome) {
  Rig r;
  for (uint8_t b = 0x10; b < 0x13; b++) r.addNeighbour(b);
  r.link.answers[0x11] = "dk";
  ASSERT_TRUE(r.sweep.requestNow());
  for (int i = 0; i < 62; i++) r.tick(1000);
  ASSERT_EQ(SweepPhase::Querying, r.sweep.phase());
  char line[160];
  r.sweep.describe(line, sizeof line);
  EXPECT_STREQ("nbr: running (query 1 of 3), last none yet, 3 neighbours, 0 responded", line);
  r.tick(1000);
  r.sweep.describe(line, sizeof line);
  EXPECT_STREQ("nbr: running (query 2 of 3), last none yet, 3 neighbours, 0 responded", line);
  ASSERT_TRUE(r.runCycle());
  r.sweep.noteReport(ReportResult::SkippedHeap);
  r.tick(90000);
  r.sweep.describe(line, sizeof line);
  EXPECT_STREQ("nbr: next in 23h 58m, last skipped (heap) 1m 30s ago, 3 neighbours, 1 responded", line);
  r.sweep.noteReport(ReportResult::Failed);
  r.sweep.describe(line, sizeof line);
  EXPECT_NE(std::string::npos, std::string(line).find("last failed 0 s ago")) << line;
}

// ---- the board's end, as a grep -------------------------------------------------------------

// src/mesh/AgentMesh.cpp needs a board to run. What this holds: the regions request and the node
// discovery go out through `sendDirect` on an empty path - MeshCore's own zero-hop send is
// ROUTE_TYPE_DIRECT with path_len 0 (Mesh.cpp:717-726 at 81dbae59), and the direct send is the one
// place a packet of this board goes out without a transport code - and nothing of MeshCore's
// zero-hop or flood sends is called for them (test/test_flood_scope's Board test holds the counts).
TEST(Board, TheRegionsRequestAndTheDiscoveryAreDirectSendsOnAnEmptyPath) {
  const std::string text = readWhole(fs::path(PROJECT_DIR) / "src" / "mesh" / "AgentMesh.cpp");
  ASSERT_FALSE(text.empty());
  const size_t query = text.find("AgentMesh::queryRegions(");
  const size_t discovery = text.find("AgentMesh::sendDiscovery(");
  ASSERT_NE(query, std::string::npos);
  ASSERT_NE(discovery, std::string::npos);
  EXPECT_NE(text.find("sendDirect(pkt, kNoPath, 0);", query), std::string::npos) << "the regions request is direct on an empty path";
  EXPECT_NE(text.find("sendDirect(pkt, kNoPath, 0);", discovery), std::string::npos) << "and so is the discovery";
  EXPECT_NE(text.find("createAnonDatagram(PAYLOAD_TYPE_ANON_REQ, self_id, peer_, sharedSecret(), plain, sizeof plain)", query),
            std::string::npos)
      << "the login's datagram, with Neighbours.h's six bytes as its plaintext";
  EXPECT_NE(text.find("observer::regionsRequest(tag, plain);", query), std::string::npos);
  EXPECT_NE(text.find("awaitReply(tag, queryTimeoutMs(), pump_)", query), std::string::npos)
      << "the reply is awaited as a job's is, for the observer firmware's timeout";
  EXPECT_NE(text.find("const uint8_t kNoPath[1] = {0};"), std::string::npos);
  // The reply is decoded the way a job's is: the peer is set for the query and forgotten after.
  const size_t end = text.find("return status;", query);
  ASSERT_NE(end, std::string::npos);
  const std::string body = text.substr(query, end - query);
  EXPECT_NE(body.find("peerSet_ = true;"), std::string::npos);
  EXPECT_NE(body.find("endJob();"), std::string::npos);
}

// PlatformIO's googletest integration links no gtest_main, so every test file brings its own.
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
