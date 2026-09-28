// The config-reply parser round-trips the .NET fixture JSON (tasks/phase-1/01): the same bytes
// MeshMaster.Contracts' tests wrote for HeartbeatReply are read here into the struct that mirrors
// the record, and GET /agent/v1/config's AgentConfig is read from the shape spec/API.md section 3
// gives (there is no fixture for it - it is not an envelope of the job protocol).

#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "agent/ConfigReply.h"
#include "agent/Iso8601.h"

#ifndef FIXTURES_DIR
#error "FIXTURES_DIR must be defined; platformio.ini's [env:native] does it"
#endif

namespace {

std::string readFixture(const char* name) {
  std::ifstream in(std::string(FIXTURES_DIR) + "/" + name, std::ios::binary);
  std::ostringstream all;
  all << in.rdbuf();
  return all.str();
}

}  // namespace

TEST(HeartbeatReply, TheDotNetFixtureReadsIntoTheStructFieldForField) {
  const std::string json = readFixture("HeartbeatReply.json");
  ASSERT_FALSE(json.empty()) << "tests/fixtures/contracts/HeartbeatReply.json is missing - run tools/sync-fixtures.ps1";

  mm::HeartbeatReply r;
  char err[128] = {0};
  ASSERT_TRUE(mm::parseHeartbeatReply(json.data(), json.size(), r, err, sizeof err)) << err;
  EXPECT_STREQ("2026-09-23T14:00:01Z", r.serverTime);
  EXPECT_EQ(1790172001u, r.serverTimeUnix);
  EXPECT_EQ(2, r.budget.maxTransactionsPerMinute);
  EXPECT_EQ(20, r.budget.minGapSeconds);
  EXPECT_EQ(30, r.pollIntervalS);
  EXPECT_TRUE(r.jobsWaiting);

  // And the fixture is exactly the record: nothing the struct knows is absent from the bytes.
  EXPECT_NE(std::string::npos, json.find("\"serverTime\""));
  EXPECT_NE(std::string::npos, json.find("\"maxTransactionsPerMinute\""));
  EXPECT_NE(std::string::npos, json.find("\"minGapSeconds\""));
  EXPECT_NE(std::string::npos, json.find("\"pollIntervalS\""));
  EXPECT_NE(std::string::npos, json.find("\"jobsWaiting\""));
}

TEST(HeartbeatReply, AMissingFieldIsRefusedByName) {
  const char* json = R"({"serverTime":"2026-09-23T14:00:01Z","budget":{"maxTransactionsPerMinute":2},"pollIntervalS":30,"jobsWaiting":true})";
  mm::HeartbeatReply r;
  char err[128] = {0};
  EXPECT_FALSE(mm::parseHeartbeatReply(json, std::strlen(json), r, err, sizeof err));
  EXPECT_STREQ("missing or not a whole number: minGapSeconds", err);

  const char* noTime = R"({"budget":{"maxTransactionsPerMinute":2,"minGapSeconds":20},"pollIntervalS":30,"jobsWaiting":true})";
  EXPECT_FALSE(mm::parseHeartbeatReply(noTime, std::strlen(noTime), r, err, sizeof err));
  EXPECT_STREQ("missing: serverTime", err);

  const char* notJson = "<html>502 Bad Gateway</html>";
  EXPECT_FALSE(mm::parseHeartbeatReply(notJson, std::strlen(notJson), r, err, sizeof err));
  EXPECT_EQ(0, std::strncmp("not JSON: ", err, 10)) << err;
}

TEST(HeartbeatReply, AnUnknownMemberIsSkippedLikeTheDotNetSideDoes) {
  const char* json = R"({"serverTime":"2026-09-23T14:00:01Z","budget":{"maxTransactionsPerMinute":2,"minGapSeconds":20,"future":1},"pollIntervalS":120,"jobsWaiting":false,"newField":{"a":1}})";
  mm::HeartbeatReply r;
  char err[128] = {0};
  ASSERT_TRUE(mm::parseHeartbeatReply(json, std::strlen(json), r, err, sizeof err)) << err;
  EXPECT_EQ(120, r.pollIntervalS);
  EXPECT_FALSE(r.jobsWaiting);
}

TEST(AgentConfig, TheConfigAnswerOfApiSection3ReadsIntoTheStruct) {
  // AgentConfig(long AgentId, string Label, string Transport, AgentBudget Budget, int PollIntervalS,
  // DateTime ServerTime), serialised camelCase like every envelope.
  const char* json = R"({"agentId":17,"label":"Window at Grenaa","transport":"onboard","budget":{"maxTransactionsPerMinute":2,"minGapSeconds":20},"pollIntervalS":30,"serverTime":"2026-09-28T12:03:14.5000000Z"})";
  mm::AgentConfig c;
  char err[128] = {0};
  ASSERT_TRUE(mm::parseAgentConfig(json, std::strlen(json), c, err, sizeof err)) << err;
  EXPECT_EQ(17, c.agentId);
  EXPECT_STREQ("Window at Grenaa", c.label);
  EXPECT_STREQ("onboard", c.transport);
  EXPECT_EQ(2, c.budget.maxTransactionsPerMinute);
  EXPECT_EQ(20, c.budget.minGapSeconds);
  EXPECT_EQ(30, c.pollIntervalS);
  EXPECT_STREQ("2026-09-28T12:03:14.5000000Z", c.serverTime);
  EXPECT_EQ(1790596994u, c.serverTimeUnix);
}

TEST(AgentConfig, AMissingFieldIsRefusedByName) {
  const char* json = R"({"agentId":17,"label":"x","budget":{"maxTransactionsPerMinute":2,"minGapSeconds":20},"pollIntervalS":30,"serverTime":"2026-09-28T12:03:14Z"})";
  mm::AgentConfig c;
  char err[128] = {0};
  EXPECT_FALSE(mm::parseAgentConfig(json, std::strlen(json), c, err, sizeof err));
  EXPECT_STREQ("missing: transport", err);

  const char* badTime = R"({"agentId":17,"label":"x","transport":"onboard","budget":{"maxTransactionsPerMinute":2,"minGapSeconds":20},"pollIntervalS":30,"serverTime":"yesterday"})";
  EXPECT_FALSE(mm::parseAgentConfig(badTime, std::strlen(badTime), c, err, sizeof err));
  EXPECT_STREQ("serverTime is not a UTC timestamp", err);
}

TEST(Iso8601, ParsesTheServersShapesAndRefusesOthers) {
  uint32_t t = 0;
  EXPECT_TRUE(mm::parseUtc("1970-01-01T00:00:00Z", t));
  EXPECT_EQ(0u, t);
  EXPECT_TRUE(mm::parseUtc("2026-09-23T14:00:01Z", t));
  EXPECT_EQ(1790172001u, t);
  EXPECT_TRUE(mm::parseUtc("2026-09-23T14:00:01.1234567Z", t));
  EXPECT_EQ(1790172001u, t);
  EXPECT_TRUE(mm::parseUtc("2026-09-23T14:00:01+00:00", t));
  EXPECT_EQ(1790172001u, t);
  EXPECT_TRUE(mm::parseUtc("2024-02-29T23:59:59Z", t));
  EXPECT_EQ(1709251199u, t);

  EXPECT_FALSE(mm::parseUtc("2026-09-23 14:00:01Z", t));
  EXPECT_FALSE(mm::parseUtc("2026-09-23T14:00:01", t));
  EXPECT_FALSE(mm::parseUtc("2026-09-23T14:00:01+02:00", t));
  EXPECT_FALSE(mm::parseUtc("2026-13-23T14:00:01Z", t));
  EXPECT_FALSE(mm::parseUtc("2026-02-30T14:00:01Z", t));
  EXPECT_FALSE(mm::parseUtc("2026-09-23T24:00:01Z", t));
  EXPECT_FALSE(mm::parseUtc("2026-09-23T14:00:01.Z", t));
  EXPECT_FALSE(mm::parseUtc("", t));
  EXPECT_FALSE(mm::parseUtc(nullptr, t));
}

TEST(ObservationsReply, TheAnswerToABatchIsHowManyWereTakenAndWhetherItWasSeenBefore) {
  const char* json = R"({"accepted":17,"duplicate":false})";
  mm::ObservationsReply r;
  char err[128] = {0};
  ASSERT_TRUE(mm::parseObservationsReply(json, std::strlen(json), r, err, sizeof err)) << err;
  EXPECT_EQ(17, r.accepted);
  EXPECT_FALSE(r.duplicate);

  const char* again = R"({"accepted":0,"duplicate":true})";
  ASSERT_TRUE(mm::parseObservationsReply(again, std::strlen(again), r, err, sizeof err)) << err;
  EXPECT_EQ(0, r.accepted);
  EXPECT_TRUE(r.duplicate);

  const char* missing = R"({"accepted":0})";
  EXPECT_FALSE(mm::parseObservationsReply(missing, std::strlen(missing), r, err, sizeof err));
  EXPECT_STREQ("missing or not a boolean: duplicate", err);
}

TEST(Iso8601, FormatsRoundTrip) {
  char out[32];
  mm::formatUtc(1790172001u, out, sizeof out);
  EXPECT_STREQ("2026-09-23T14:00:01Z", out);
  mm::formatUtc(0, out, sizeof out);
  EXPECT_STREQ("1970-01-01T00:00:00Z", out);
  mm::formatClock(1790172001u, out, sizeof out);
  EXPECT_STREQ("14:00:01", out);

  for (uint32_t t = 1700000000u; t < 1900000000u; t += 86399u * 7u + 13u) {
    mm::formatUtc(t, out, sizeof out);
    uint32_t back = 0;
    ASSERT_TRUE(mm::parseUtc(out, back)) << out;
    EXPECT_EQ(t, back) << out;
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
