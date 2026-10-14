// The contract is the .NET agent's (tasks/phase-1/02): what this firmware POSTs to
// /agent/v1/heartbeat and /agent/v1/observations is held against
// tests/fixtures/contracts/AgentHeartbeat.json and ObservationBatch.json **byte for byte**, for
// the inputs MeshMaster.Contracts' own round-trip tests wrote those fixtures from. A difference
// is a bug in the firmware, not a second dialect.
//
// ObservationBatch had no fixture until meshmaster.dk's task p4.3-71 wrote one, and this test
// held the batch to the record in src/MeshMaster.Contracts/Envelopes.cs instead. The file is
// here now and the inline expectation is gone: what is left inline is the cases the sample does
// not carry - a direct neighbour, an empty batch, a name from a stranger and a position with six
// decimals west of Greenwich.

#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include <agent/Envelopes.h>
#include <agent/JobEnvelopes.h>
#include "version.h"

#ifndef FIXTURES_DIR
#error "FIXTURES_DIR must be defined; platformio.ini's [env:native] does it"
#endif

namespace {

std::string readFixture(const char* name) {
  std::ifstream in(std::string(FIXTURES_DIR) + "/" + name, std::ios::binary);
  std::ostringstream all;
  all << in.rdbuf();
  std::string text = all.str();
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
  return text;
}

// The thirteen types of API §3, in the order MeshMaster.Contracts' JobTypes.All gives them -
// which is the order the fixture carries. Since task 05 that is the firmware's own list,
// kJobTypes: the heartbeat advertises every type, and this test holds the bytes it sends to the
// fixture. When meshmaster.dk adds a job type the fixture moves, and so must the list.
const char* const* const kAllJobTypes = mm::kJobTypes;

std::string heartbeatJson(const mm::Heartbeat& hb) {
  char out[2048];
  const size_t n = mm::writeHeartbeat(hb, out, sizeof out);
  EXPECT_GT(n, 0u);
  EXPECT_EQ(n, std::strlen(out));
  return std::string(out, n);
}

}  // namespace

// ---- the heartbeat, against the .NET bytes ---------------------------------------------------

// The sample MeshMaster.Contracts' ContractsTests builds the fixture from, field for field. Since
// meshmaster.dk's task p4.14-120 that sample is an agent that **proposes** its own check-in
// interval and a cap for its wake (EXIT-NODES §6), so the two optional fields are set here; the
// test below holds the other half, which is this firmware's: an agent that proposes neither
// writes the fixture's bytes without them.
mm::Heartbeat theFixturesSample(const mm::ErrorCounter* errors, int errorCount) {
  mm::Heartbeat hb;
  hb.agentVersion = "0.3.1";
  hb.transport = "onboard";
  hb.at = 1790172000u;  // 2026-09-23T14:00:00Z
  hb.companionPublicKey = "a1b2";
  hb.companionFirmware = "v1.17.1";
  hb.hasRadio = true;
  hb.radio = {869.618, 62.5, 8, 8};
  hb.hasJobTypes = true;
  hb.jobTypes = kAllJobTypes;
  hb.jobTypeCount = mm::kJobTypeCount;
  hb.hasBudget = true;
  hb.budget.maxTransactionsPerMinute = 2;  // AgentBudget.Default
  hb.budget.minGapSeconds = 20;
  hb.companionConnected = true;
  hb.queueDepth = 0;
  hb.errors = errors;
  hb.errorCount = errorCount;
  hb.board = "heltec_v3";
  return hb;
}

TEST(Heartbeat, TheSameInputMakesTheDotNetFixtureByteForByte) {
  const std::string fixture = readFixture("AgentHeartbeat.json");
  ASSERT_FALSE(fixture.empty()) << "tests/fixtures/contracts/AgentHeartbeat.json is missing - run tools/sync-fixtures.ps1";

  const mm::ErrorCounter errors[] = {{"uplink", 0}};
  mm::Heartbeat hb = theFixturesSample(errors, 1);
  hb.hasNextCheckInS = true;
  hb.nextCheckInS = 3600;
  hb.hasMaxTransactionsThisWake = true;
  hb.maxTransactionsThisWake = 12;

  EXPECT_EQ(fixture, heartbeatJson(hb));
}

// The Heltec proposes nothing: it runs `AgentLoop::loop()` with WiFi up and is back in thirty
// seconds, so neither field is written and its heartbeat is byte for byte what it was before
// meshmaster.dk's task 120 - which is that task's own promise ("an agent that sends neither is
// byte-for-byte as before") held from this end. The expectation is the fixture with its last two
// members cut off, so it stays anchored to the fixture rather than to a line typed out here: if
// the record's order ever changes, both tests fail and neither quietly passes.
TEST(Heartbeat, AnAgentThatProposesNothingWritesTheFixtureWithoutTheTwoWakeFields) {
  const std::string fixture = readFixture("AgentHeartbeat.json");
  ASSERT_FALSE(fixture.empty());
  const size_t at = fixture.find(",\"nextCheckInS\":");
  ASSERT_NE(at, std::string::npos) << "the fixture no longer carries nextCheckInS: " << fixture;
  ASSERT_NE(fixture.find("\"maxTransactionsThisWake\":", at), std::string::npos)
      << "nextCheckInS without maxTransactionsThisWake after it: " << fixture;
  const std::string withoutThem = fixture.substr(0, at) + "}";

  const mm::ErrorCounter errors[] = {{"uplink", 0}};
  const mm::Heartbeat hb = theFixturesSample(errors, 1);
  EXPECT_FALSE(hb.hasNextCheckInS) << "the default is to propose nothing";
  EXPECT_FALSE(hb.hasMaxTransactionsThisWake);
  EXPECT_EQ(withoutThem, heartbeatJson(hb));
}

// One without the other: both are optional members on their own, and a proposal with no cap is
// what a unit on a full battery sends.
TEST(Heartbeat, EitherOfTheTwoWakeFieldsGoesOnItsOwn) {
  const mm::ErrorCounter errors[] = {{"uplink", 0}};
  mm::Heartbeat hb = theFixturesSample(errors, 1);
  hb.hasNextCheckInS = true;
  hb.nextCheckInS = 900;
  const std::string interval = heartbeatJson(hb);
  EXPECT_NE(interval.find("\"board\":\"heltec_v3\",\"nextCheckInS\":900}"), std::string::npos) << interval;
  EXPECT_EQ(interval.find("maxTransactionsThisWake"), std::string::npos) << interval;

  hb.hasNextCheckInS = false;
  hb.hasMaxTransactionsThisWake = true;
  hb.maxTransactionsThisWake = 6;
  const std::string cap = heartbeatJson(hb);
  EXPECT_NE(cap.find("\"board\":\"heltec_v3\",\"maxTransactionsThisWake\":6}"), std::string::npos) << cap;
  EXPECT_EQ(cap.find("nextCheckInS"), std::string::npos) << cap;
}

TEST(Heartbeat, WhatThisFirmwareActuallySendsHasNoJobTypesAndFourCounters) {
  const mm::ErrorCounter errors[] = {
      {"overheard", 41}, {"wifiReconnects", 2}, {"httpFailures", 0}, {"observationsDropped", 7}};
  mm::Heartbeat hb;
  hb.agentVersion = "0.0.0-dev";
  hb.at = 1790172000u;
  hb.companionPublicKey = "7f3a9c02e1b44d9a0f6c2b7d8e9a1c3f5d7b9e0a2c4e6f8a0b1c2d3e4f5a6b7c";
  hb.companionFirmware = "companion-v1.17.1";
  hb.hasRadio = true;
  hb.radio = {869.618, 62.5, 8, 8};
  hb.hasJobTypes = true;
  hb.jobTypeCount = 0;  // nothing can be claimed until task 03
  hb.budget.maxTransactionsPerMinute = 2;
  hb.budget.minGapSeconds = 20;
  hb.companionConnected = true;
  hb.errors = errors;
  hb.errorCount = 4;
  hb.board = "heltec_v3";

  EXPECT_EQ(
      "{\"agentVersion\":\"0.0.0-dev\",\"transport\":\"onboard\",\"at\":\"2026-09-23T14:00:00Z\","
      "\"companionPublicKey\":\"7f3a9c02e1b44d9a0f6c2b7d8e9a1c3f5d7b9e0a2c4e6f8a0b1c2d3e4f5a6b7c\","
      "\"companionFirmware\":\"companion-v1.17.1\",\"radio\":{\"freqMHz\":869.618,\"bwKHz\":62.5,\"sf\":8,\"cr\":8},"
      "\"jobTypes\":[],\"budget\":{\"maxTransactionsPerMinute\":2,\"minGapSeconds\":20},"
      "\"companionConnected\":true,\"queueDepth\":0,"
      "\"errors\":{\"overheard\":41,\"wifiReconnects\":2,\"httpFailures\":0,\"observationsDropped\":7},"
      "\"board\":\"heltec_v3\"}",
      heartbeatJson(hb));
}

TEST(Heartbeat, ANullFieldIsLeftOutAndAnEmptyArrayIsNot) {
  mm::Heartbeat hb;
  hb.agentVersion = "0.0.0-dev";
  hb.at = 1790172000u;
  hb.budget.maxTransactionsPerMinute = 2;
  hb.budget.minGapSeconds = 20;
  // No identity yet: no companion key, no firmware, no radio block, no board, no counters.
  hb.board = nullptr;
  const std::string json = heartbeatJson(hb);
  EXPECT_EQ(std::string::npos, json.find("companionPublicKey"));
  EXPECT_EQ(std::string::npos, json.find("companionFirmware"));
  EXPECT_EQ(std::string::npos, json.find("\"radio\""));
  EXPECT_EQ(std::string::npos, json.find("\"errors\""));
  EXPECT_EQ(std::string::npos, json.find("\"board\""));
  EXPECT_NE(std::string::npos, json.find("\"jobTypes\":[]"));
  EXPECT_NE(std::string::npos, json.find("\"companionConnected\":false"));
}

TEST(Heartbeat, TheFirmwareStringIsTheTwoFieldsTheHeartbeatSends) {
  // `status` prints "meshmaster-agent <agentVersion> on MeshCore <companionFirmware>", and the
  // heartbeat sends the two halves: the bench reads one and the server's page shows the other.
  const std::string composed =
      std::string("meshmaster-agent ") + meshmaster_agent_version() + " on MeshCore " + mm::meshcoreTag();
  EXPECT_EQ(composed, std::string(mm::firmwareString()));
}

TEST(Heartbeat, ABodyThatDoesNotFitIsRefusedRatherThanCut) {
  mm::Heartbeat hb;
  hb.agentVersion = "0.0.0-dev";
  hb.at = 1790172000u;
  char tiny[32];
  EXPECT_EQ(0u, mm::writeHeartbeat(hb, tiny, sizeof tiny));
}

// ---- the numbers ------------------------------------------------------------------------------

TEST(Numbers, ADoubleIsTheShortestDecimalThatReadsBackAsItself) {
  mm::Heartbeat hb;
  hb.agentVersion = "v";
  hb.at = 0;
  hb.hasBudget = false;
  hb.hasJobTypes = false;
  hb.hasRadio = true;
  hb.radio = {869.618, 62.5, 12, 5};
  EXPECT_NE(std::string::npos, heartbeatJson(hb).find("\"freqMHz\":869.618,\"bwKHz\":62.5,\"sf\":12,\"cr\":5"));

  hb.radio = {433.0, 250.0, 7, 8};
  EXPECT_NE(std::string::npos, heartbeatJson(hb).find("\"freqMHz\":433,\"bwKHz\":250"));
}

// ---- the observation batch ---------------------------------------------------------------------

TEST(Observations, TheSameInputMakesTheDotNetFixtureByteForByte) {
  const std::string fixture = readFixture("ObservationBatch.json");
  ASSERT_FALSE(fixture.empty())
      << "tests/fixtures/contracts/ObservationBatch.json is missing - run tools/sync-fixtures.ps1";

  // The sample MeshMaster.Contracts' ContractsTests builds the fixture from, field for field.
  const char* hops[] = {"a1", "7f"};
  mm::ObservationItem item;
  item.at = 1789725492u;  // 2026-09-18T09:58:12Z
  item.kind = "advert";
  item.publicKey = "a1b2c3d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e8f90";
  item.hasPath = true;
  item.path = hops;
  item.pathCount = 2;
  item.hasHops = true;
  item.hops = 2;
  item.hasSnr = true;
  item.snr = 6.25;
  item.hasRssi = true;
  item.rssi = -97;
  item.hasFields = true;
  item.fields.name = "Grenaa Chimney #1";
  item.fields.hasPosition = true;
  item.fields.latE6 = 56410000;
  item.fields.lonE6 = 10880000;
  item.fields.role = "repeater";
  item.fields.hasFlags = true;
  item.fields.flags = 3;

  char out[1024];
  const size_t n = mm::writeObservationBatch("e7c2", &item, 1, out, sizeof out);
  ASSERT_GT(n, 0u);
  EXPECT_EQ(fixture, std::string(out, n));

  // The batch holds exactly the advert of Observation.json - the fixtures' own README says the
  // nested pair is one object - so the item written above is that file's bytes too.
  const std::string one = readFixture("Observation.json");
  ASSERT_FALSE(one.empty());
  EXPECT_NE(std::string::npos, fixture.find(one)) << "ObservationBatch.json does not hold Observation.json";
}

TEST(Observations, APositionIsThePlainDecimalOfTheAdvertsMicrodegrees) {
  // The fixture's own position is two decimals and north-east of Greenwich; an advert can carry
  // six and a western longitude, and neither goes through a double on the way out.
  mm::ObservationItem item;
  item.at = 1790172000u;
  item.publicKey = "a1b2c3d4";
  item.hasFields = true;
  item.fields.hasPosition = true;
  item.fields.latE6 = 56416666;
  item.fields.lonE6 = -10876500;
  char out[256];
  const size_t n = mm::writeObservationBatch("obs-1", &item, 1, out, sizeof out);
  ASSERT_GT(n, 0u);
  EXPECT_NE(std::string::npos, std::string(out, n).find("\"lat\":56.416666,\"lon\":-10.8765"));
}

TEST(Observations, ADirectNeighbourHasAnEmptyPathAndZeroHops) {
  mm::ObservationItem item;
  item.at = 1790172000u;
  item.publicKey = "a1";
  item.hasPath = true;
  item.pathCount = 0;
  item.hasHops = true;
  item.hops = 0;
  char out[256];
  const size_t n = mm::writeObservationBatch("obs-1", &item, 1, out, sizeof out);
  ASSERT_GT(n, 0u);
  EXPECT_NE(std::string::npos, std::string(out, n).find("\"path\":[],\"hops\":0"));
  EXPECT_EQ(std::string::npos, std::string(out, n).find("\"fields\""));
  EXPECT_EQ(std::string::npos, std::string(out, n).find("\"snr\""));
}

TEST(Observations, AnEmptyBatchIsStillAWellFormedBody) {
  char out[64];
  const size_t n = mm::writeObservationBatch("obs-1", nullptr, 0, out, sizeof out);
  EXPECT_EQ("{\"clientId\":\"obs-1\",\"items\":[]}", std::string(out, n));
}

TEST(Observations, ANameFromAStrangerIsEscapedAndNeverBreaksTheBody) {
  mm::ObservationItem item;
  item.at = 0;
  item.publicKey = "a1";
  item.hasFields = true;
  item.fields.name = "he said \"hi\" \\ back";
  char out[256];
  const size_t n = mm::writeObservationBatch("obs-1", &item, 1, out, sizeof out);
  ASSERT_GT(n, 0u);
  EXPECT_NE(std::string::npos,
            std::string(out, n).find("\"name\":\"he said \\\"hi\\\" \\\\ back\""));
}

// ---- what a name is allowed to be ---------------------------------------------------------------

TEST(Names, TheNameIsCutAtTheLastByteThatIsStillUtf8) {
  // Plain ASCII, whole and then bounded.
  EXPECT_EQ(5u, mm::validUtf8PrefixLength("hello", 32));
  EXPECT_EQ(3u, mm::validUtf8PrefixLength("hello", 3));

  // "Grenå" is six bytes: the å is two, and a bound of five must not cut it in half.
  const char* danish = "Gren\xc3\xa5";
  EXPECT_EQ(6u, mm::validUtf8PrefixLength(danish, 32));
  EXPECT_EQ(4u, mm::validUtf8PrefixLength(danish, 5));

  // A lone continuation byte, an overlong encoding and a surrogate are not UTF-8.
  EXPECT_EQ(0u, mm::validUtf8PrefixLength("\x80zz", 32));
  EXPECT_EQ(0u, mm::validUtf8PrefixLength("\xc0\xaf", 32));
  EXPECT_EQ(0u, mm::validUtf8PrefixLength("\xed\xa0\x80", 32));

  // A control character ends it: a name is one line or it is nothing.
  EXPECT_EQ(2u, mm::validUtf8PrefixLength("ab\ncd", 32));
  EXPECT_EQ(0u, mm::validUtf8PrefixLength(nullptr, 32));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
