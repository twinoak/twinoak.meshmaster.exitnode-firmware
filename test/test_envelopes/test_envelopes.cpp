// The contract is the .NET agent's (tasks/phase-1/02): what this firmware POSTs to
// /agent/v1/heartbeat is held against tests/fixtures/contracts/AgentHeartbeat.json **byte for
// byte**, for the input MeshMaster.Contracts' own round-trip test wrote that fixture from. A
// difference is a bug in the firmware, not a second dialect.
//
// POST /agent/v1/observations has no fixture - ObservationBatch is not one of the four envelopes
// the .NET test writes files for - so it is held to the record in src/MeshMaster.Contracts/
// Envelopes.cs instead: the field order, the camelCase, the nulls left out and the numbers as
// System.Text.Json writes them.

#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "agent/Envelopes.h"
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
// which is the order the fixture carries. When meshmaster.dk adds a job type the fixture moves,
// this list has to move with it, and that is the point.
const char* const kAllJobTypes[] = {"status",   "telemetry", "neighbours",     "config.read", "config.apply",
                                    "cli",      "trace",     "discover",       "advert.request",
                                    "clock.sync", "password.set", "acl.set",    "reboot"};

std::string heartbeatJson(const mm::Heartbeat& hb) {
  char out[2048];
  const size_t n = mm::writeHeartbeat(hb, out, sizeof out);
  EXPECT_GT(n, 0u);
  EXPECT_EQ(n, std::strlen(out));
  return std::string(out, n);
}

}  // namespace

// ---- the heartbeat, against the .NET bytes ---------------------------------------------------

TEST(Heartbeat, TheSameInputMakesTheDotNetFixtureByteForByte) {
  const std::string fixture = readFixture("AgentHeartbeat.json");
  ASSERT_FALSE(fixture.empty()) << "tests/fixtures/contracts/AgentHeartbeat.json is missing - run tools/sync-fixtures.ps1";

  // The sample MeshMaster.Contracts' ContractsTests builds the fixture from, field for field.
  const mm::ErrorCounter errors[] = {{"uplink", 0}};
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
  hb.jobTypeCount = static_cast<int>(sizeof kAllJobTypes / sizeof kAllJobTypes[0]);
  hb.hasBudget = true;
  hb.budget.maxTransactionsPerMinute = 2;  // AgentBudget.Default
  hb.budget.minGapSeconds = 20;
  hb.companionConnected = true;
  hb.queueDepth = 0;
  hb.errors = errors;
  hb.errorCount = 1;
  hb.board = "heltec_v3";

  EXPECT_EQ(fixture, heartbeatJson(hb));
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

TEST(Observations, ABatchIsTheRecordsFieldOrderWithTheNullsLeftOut) {
  const char* hops[] = {"a1", "7f"};
  mm::ObservationItem item;
  item.at = 1790172000u;
  item.kind = "advert";
  item.publicKey = "a1b2c3d4";
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
  item.fields.latE6 = 56416666;
  item.fields.lonE6 = -10876500;
  item.fields.role = "repeater";

  char out[1024];
  const size_t n = mm::writeObservationBatch("obs-0123456789abcdef0123456789abcdef", &item, 1, out, sizeof out);
  ASSERT_GT(n, 0u);
  EXPECT_EQ(
      "{\"clientId\":\"obs-0123456789abcdef0123456789abcdef\",\"items\":["
      "{\"at\":\"2026-09-23T14:00:00Z\",\"kind\":\"advert\",\"publicKey\":\"a1b2c3d4\",\"path\":[\"a1\",\"7f\"],"
      "\"hops\":2,\"snr\":6.25,\"rssi\":-97,"
      "\"fields\":{\"name\":\"Grenaa Chimney #1\",\"lat\":56.416666,\"lon\":-10.8765,\"role\":\"repeater\"}}]}",
      std::string(out, n));
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
