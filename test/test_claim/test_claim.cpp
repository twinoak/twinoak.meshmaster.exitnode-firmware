// The claim: `POST /agent/v1/jobs/claim {"max":2}` and what comes back.
//
// The recorded reply below is a claim of two jobs for one repeater - the status and telemetry
// pair the server schedules in one slot and hands out together - whose **first job is
// tests/fixtures/contracts/ClaimedJob.json verbatim**, so the shape this parser is held to is
// the one MeshMaster.Contracts' own round-trip test wrote. The three things the task asks for
// are the three things a claim has to honour: `max`, `leaseUntil` and `groupWith`.

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "agent/Iso8601.h"
#include "agent/JobEnvelopes.h"

#ifndef FIXTURES_DIR
#error "FIXTURES_DIR must be defined; platformio.ini's [env:native] does it"
#endif

namespace {

namespace fs = std::filesystem;

std::string fixture(const std::string& name) {
  std::ifstream in(fs::path(FIXTURES_DIR) / (name + ".json"), std::ios::binary);
  EXPECT_TRUE(in.good()) << "no such fixture: " << name;
  std::ostringstream all;
  all << in.rdbuf();
  std::string text = all.str();
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
  return text;
}

// The second job of the pair: telemetry for the same repeater, grouped with the first, at class
// `acl` - which is the material these four types ever carry, the granted level and no secret.
const char* kSecondJob =
    "{\"id\":12346,\"type\":\"telemetry\",\"repeater\":{\"id\":17,\"label\":\"Grenaa Chimney #1\","
    "\"publicKey\":\"a1b2\"},\"credentialClass\":\"acl\",\"leaseUntil\":\"2026-09-18T10:02:03Z\","
    "\"timeouts\":{\"perHopMs\":4000,\"loginMs\":8000},\"payload\":{},\"credential\":{\"acl\":1},"
    "\"path\":{\"hops\":[\"a1\",\"7f\"],\"source\":\"advert\",\"learnedAt\":\"2026-09-18T10:00:00Z\"},"
    "\"idempotencyKey\":\"schedule:17:telemetry:2026-09-18T10:00Z\",\"groupWith\":[12345]}";

// A third job the server sent anyway: a claim asked for two takes two.
const char* kThirdJob =
    "{\"id\":12999,\"type\":\"trace\",\"repeater\":{\"id\":18,\"label\":\"Risskov Water Tower\","
    "\"publicKey\":\"c3d4\"},\"credentialClass\":\"none\",\"leaseUntil\":\"2026-09-18T10:02:03Z\","
    "\"timeouts\":{\"perHopMs\":4000,\"loginMs\":8000},\"payload\":{}}";

std::string recordedClaim() {
  return std::string("{\"serverTime\":\"2026-09-18T10:00:03Z\",\"jobs\":[") + fixture("ClaimedJob") + "," +
         kSecondJob + "," + kThirdJob + "]}";
}

uint32_t at(const char* iso) {
  uint32_t epoch = 0;
  EXPECT_TRUE(mm::parseUtc(iso, epoch)) << iso;
  return epoch;
}

TEST(Claim, TheRequestIsTheOneFieldTheServerReads) {
  char out[32];
  const std::size_t len = mm::writeClaimRequest(mm::kClaimMax, out, sizeof out);
  ASSERT_GT(len, 0u);
  EXPECT_EQ(std::string(out, len), "{\"max\":2}");
}

TEST(Claim, AClaimOfTwoTakesTwoHoweverManyTheServerSent) {
  const std::string reply = recordedClaim();
  mm::ClaimedJob jobs[mm::kClaimMax];
  int count = 0;
  uint32_t serverTime = 0;
  char err[96] = {0};
  ASSERT_TRUE(mm::parseClaimResponse(reply.c_str(), reply.size(), jobs, mm::kClaimMax, count, serverTime, err,
                                     sizeof err))
      << err;
  EXPECT_EQ(count, 2) << "the reply carried three; the claim asked for two";
  EXPECT_EQ(serverTime, at("2026-09-18T10:00:03Z"));
  EXPECT_EQ(jobs[0].id, 12345);
  EXPECT_EQ(jobs[1].id, 12346);
}

TEST(Claim, TheFixtureJobReadsFieldForField) {
  const std::string reply = recordedClaim();
  mm::ClaimedJob jobs[mm::kClaimMax];
  int count = 0;
  uint32_t serverTime = 0;
  char err[96] = {0};
  ASSERT_TRUE(mm::parseClaimResponse(reply.c_str(), reply.size(), jobs, mm::kClaimMax, count, serverTime, err,
                                     sizeof err))
      << err;
  ASSERT_GE(count, 1);
  const mm::ClaimedJob& job = jobs[0];
  EXPECT_EQ(job.id, 12345);
  EXPECT_STREQ(job.type, "status");
  EXPECT_EQ(job.repeaterId, 17);
  EXPECT_STREQ(job.repeaterLabel, "Grenaa Chimney #1");
  EXPECT_STREQ(job.repeaterKey, "a1b2");
  EXPECT_EQ(job.credentialClass, mm::CredentialClass::Guest);
  EXPECT_EQ(job.leaseUntil, at("2026-09-18T10:02:03Z"));
  EXPECT_EQ(job.perHopMs, 4000);
  EXPECT_EQ(job.loginMs, 8000);
  EXPECT_TRUE(job.hasPath);
  ASSERT_EQ(job.pathCount, 2);
  EXPECT_STREQ(job.pathHops[0], "a1");
  EXPECT_STREQ(job.pathHops[1], "7f");
  EXPECT_STREQ(job.idempotencyKey, "schedule:17:status:2026-09-18T10:00Z");
  ASSERT_EQ(job.groupCount, 1);
  EXPECT_EQ(job.groupWith[0], 12346);
  // The fixture's credential is a guest password, which no job this firmware runs ever asks for:
  // the four read types run at none, guest or acl, and only the acl level ever travels here.
  EXPECT_FALSE(job.hasAclLevel);
}

TEST(Claim, TheAclLevelIsTheOnlyMaterialTheseTypesCarry) {
  const std::string reply = recordedClaim();
  mm::ClaimedJob jobs[mm::kClaimMax];
  int count = 0;
  uint32_t serverTime = 0;
  char err[96] = {0};
  ASSERT_TRUE(mm::parseClaimResponse(reply.c_str(), reply.size(), jobs, mm::kClaimMax, count, serverTime, err,
                                     sizeof err))
      << err;
  ASSERT_EQ(count, 2);
  EXPECT_EQ(jobs[1].credentialClass, mm::CredentialClass::Acl);
  EXPECT_TRUE(jobs[1].hasAclLevel);
  EXPECT_EQ(jobs[1].aclLevel, mm::kPermReadOnly);
}

// The pre-task-60 shape of an acl credential: a flag rather than a level. The level is what the
// restore of a later task needs, so a flag is read as the read-only a grant starts at.
TEST(Claim, TheOlderAclFlagIsReadAsTheLevelAGrantStartsAt) {
  const std::string reply =
      "{\"serverTime\":\"2026-09-18T10:00:03Z\",\"jobs\":[{\"id\":7,\"type\":\"status\","
      "\"repeater\":{\"id\":1,\"label\":\"x\",\"publicKey\":\"ab\"},\"credentialClass\":\"acl\","
      "\"leaseUntil\":\"2026-09-18T10:02:03Z\",\"timeouts\":{\"perHopMs\":4000,\"loginMs\":8000},"
      "\"payload\":{},\"credential\":{\"acl\":true}}]}";
  mm::ClaimedJob jobs[mm::kClaimMax];
  int count = 0;
  uint32_t serverTime = 0;
  char err[96] = {0};
  ASSERT_TRUE(mm::parseClaimResponse(reply.c_str(), reply.size(), jobs, mm::kClaimMax, count, serverTime, err,
                                     sizeof err))
      << err;
  ASSERT_EQ(count, 1);
  EXPECT_TRUE(jobs[0].hasAclLevel);
  EXPECT_EQ(jobs[0].aclLevel, mm::kPermReadOnly);
}

// `groupWith` is what makes a claimed pair one login: the two jobs of the recorded claim name
// each other, and a job for another repeater is nobody's group.
TEST(Claim, GroupWithNamesTheJobsThatShareALogin) {
  const std::string reply = recordedClaim();
  mm::ClaimedJob jobs[mm::kClaimMax];
  int count = 0;
  uint32_t serverTime = 0;
  char err[96] = {0};
  ASSERT_TRUE(mm::parseClaimResponse(reply.c_str(), reply.size(), jobs, mm::kClaimMax, count, serverTime, err,
                                     sizeof err))
      << err;
  ASSERT_EQ(count, 2);
  EXPECT_TRUE(mm::sharesLogin(jobs[0], jobs[1]));
  EXPECT_TRUE(mm::sharesLogin(jobs[1], jobs[0]));
  EXPECT_FALSE(mm::sharesLogin(jobs[0], jobs[0]));
  EXPECT_STREQ(jobs[0].repeaterKey, jobs[1].repeaterKey) << "one repeater, so one session either way";

  mm::ClaimedJob other;
  other.id = 999;
  EXPECT_FALSE(mm::sharesLogin(jobs[0], other));
}

// The lease is a deadline, and the board reads it as one: a claim taken at 10:00:03 with a lease
// to 10:02:03 has two minutes of work in it, and a lease already in the past has none.
TEST(Claim, TheLeaseIsWhatBoundsAJobsWork) {
  const std::string reply = recordedClaim();
  mm::ClaimedJob jobs[mm::kClaimMax];
  int count = 0;
  uint32_t serverTime = 0;
  char err[96] = {0};
  ASSERT_TRUE(mm::parseClaimResponse(reply.c_str(), reply.size(), jobs, mm::kClaimMax, count, serverTime, err,
                                     sizeof err))
      << err;
  ASSERT_EQ(count, 2);
  for (int i = 0; i < count; i++) {
    EXPECT_EQ(jobs[i].leaseUntil, at("2026-09-18T10:02:03Z"));
    EXPECT_GT(jobs[i].leaseUntil, serverTime);
    EXPECT_EQ(jobs[i].leaseUntil - serverTime, 120u);
  }
  EXPECT_LT(at("2026-09-18T10:02:03Z"), at("2026-09-18T10:05:00Z")) << "a lease that has run out is in the past";
}

TEST(Claim, AnEmptyClaimIsNotAFailure) {
  const std::string reply = "{\"serverTime\":\"2026-09-18T10:00:03Z\",\"jobs\":[]}";
  mm::ClaimedJob jobs[mm::kClaimMax];
  int count = 1;
  uint32_t serverTime = 0;
  char err[96] = {0};
  EXPECT_TRUE(mm::parseClaimResponse(reply.c_str(), reply.size(), jobs, mm::kClaimMax, count, serverTime, err,
                                     sizeof err))
      << err;
  EXPECT_EQ(count, 0);
}

TEST(Claim, AReplyThatIsNotAClaimIsRefusedByName) {
  struct Case {
    const char* json;
    const char* says;
  };
  const Case cases[] = {
      {"not json at all", "not JSON"},
      {"{\"jobs\":[]}", "serverTime"},
      {"{\"serverTime\":\"2026-09-18T10:00:03Z\"}", "jobs"},
      {"{\"serverTime\":\"2026-09-18T10:00:03Z\",\"jobs\":[{\"type\":\"status\"}]}", "id"},
      {"{\"serverTime\":\"2026-09-18T10:00:03Z\",\"jobs\":[{\"id\":1}]}", "type"},
      {"{\"serverTime\":\"2026-09-18T10:00:03Z\",\"jobs\":[{\"id\":1,\"type\":\"status\"}]}", "repeater"},
  };
  for (const Case& c : cases) {
    mm::ClaimedJob jobs[mm::kClaimMax];
    int count = 0;
    uint32_t serverTime = 0;
    char err[96] = {0};
    EXPECT_FALSE(mm::parseClaimResponse(c.json, std::strlen(c.json), jobs, mm::kClaimMax, count, serverTime, err,
                                        sizeof err))
        << c.json;
    EXPECT_NE(std::string(err).find(c.says), std::string::npos) << c.json << " said: " << err;
  }
}

// The claim is atomic and the server decides the class, so a job this firmware cannot run has to
// be recognised after it is claimed rather than before. It is answered `unsupported` at no
// transaction - "this firmware does not have what the job asked for" - which is also the one
// outcome that moves nothing in the server's reachability, and a transaction that never happened
// must teach the router nothing (EXIT-NODES §3).
TEST(Claim, AJobThisFirmwareCannotRunIsRecognisedBeforeAnythingIsSent) {
  const std::string reply = recordedClaim();
  mm::ClaimedJob jobs[mm::kClaimMax];
  int count = 0;
  uint32_t serverTime = 0;
  char err[96] = {0};
  ASSERT_TRUE(mm::parseClaimResponse(reply.c_str(), reply.size(), jobs, mm::kClaimMax, count, serverTime, err,
                                     sizeof err))
      << err;
  ASSERT_EQ(count, 2);

  char why[160] = {0};
  // The fixture's job is guest class, and a guest login carries a password - task 04's.
  EXPECT_FALSE(mm::canRun(jobs[0], why, sizeof why));
  EXPECT_NE(std::string(why).find("guest"), std::string::npos) << why;
  EXPECT_NE(std::string(why).find("no password"), std::string::npos) << why;

  // The acl-class telemetry beside it is exactly what this firmware does.
  EXPECT_TRUE(mm::canRun(jobs[1], why, sizeof why));
  EXPECT_STREQ(why, "");

  mm::ClaimedJob admin = jobs[1];
  admin.credentialClass = mm::CredentialClass::Admin;
  EXPECT_FALSE(mm::canRun(admin, why, sizeof why));

  mm::ClaimedJob unknown = jobs[1];
  std::snprintf(unknown.type, sizeof unknown.type, "config.read");
  EXPECT_FALSE(mm::canRun(unknown, why, sizeof why));
  EXPECT_NE(std::string(why).find("no handler"), std::string::npos) << why;

  mm::ClaimedJob keyless = jobs[1];
  keyless.repeaterKey[0] = 0;
  EXPECT_FALSE(mm::canRun(keyless, why, sizeof why));
  EXPECT_NE(std::string(why).find("public key"), std::string::npos) << why;
}

TEST(Claim, TheFourTypesAreTheOnesThisFirmwareAdvertises) {
  ASSERT_EQ(mm::kJobTypeCount, 4);
  EXPECT_STREQ(mm::kJobTypes[0], "status");
  EXPECT_STREQ(mm::kJobTypes[1], "telemetry");
  EXPECT_STREQ(mm::kJobTypes[2], "neighbours");
  EXPECT_STREQ(mm::kJobTypes[3], "trace");
  for (int i = 0; i < mm::kJobTypeCount; i++) EXPECT_TRUE(mm::isKnownJobType(mm::kJobTypes[i]));
  // The types a later task builds: routing does not send them here, and a claim that carried one
  // anyway would be answered `unsupported` rather than run.
  EXPECT_FALSE(mm::isKnownJobType("config.read"));
  EXPECT_FALSE(mm::isKnownJobType("discover"));
  EXPECT_FALSE(mm::isKnownJobType("cli"));
  EXPECT_FALSE(mm::isKnownJobType(nullptr));
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
