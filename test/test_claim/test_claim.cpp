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
  std::string reply = recordedClaim();
  mm::ClaimedJob jobs[mm::kClaimMax];
  int count = 0;
  uint32_t serverTime = 0;
  char err[96] = {0};
  ASSERT_TRUE(mm::parseClaimResponse(&reply[0], reply.size(), jobs, mm::kClaimMax, count, serverTime, err,
                                     sizeof err))
      << err;
  EXPECT_EQ(count, 2) << "the reply carried three; the claim asked for two";
  EXPECT_EQ(serverTime, at("2026-09-18T10:00:03Z"));
  EXPECT_EQ(jobs[0].id, 12345);
  EXPECT_EQ(jobs[1].id, 12346);
}

TEST(Claim, TheFixtureJobReadsFieldForField) {
  std::string reply = recordedClaim();
  mm::ClaimedJob jobs[mm::kClaimMax];
  int count = 0;
  uint32_t serverTime = 0;
  char err[96] = {0};
  ASSERT_TRUE(mm::parseClaimResponse(&reply[0], reply.size(), jobs, mm::kClaimMax, count, serverTime, err,
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
  std::string reply = recordedClaim();
  mm::ClaimedJob jobs[mm::kClaimMax];
  int count = 0;
  uint32_t serverTime = 0;
  char err[96] = {0};
  ASSERT_TRUE(mm::parseClaimResponse(&reply[0], reply.size(), jobs, mm::kClaimMax, count, serverTime, err,
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
  std::string reply =
      "{\"serverTime\":\"2026-09-18T10:00:03Z\",\"jobs\":[{\"id\":7,\"type\":\"status\","
      "\"repeater\":{\"id\":1,\"label\":\"x\",\"publicKey\":\"ab\"},\"credentialClass\":\"acl\","
      "\"leaseUntil\":\"2026-09-18T10:02:03Z\",\"timeouts\":{\"perHopMs\":4000,\"loginMs\":8000},"
      "\"payload\":{},\"credential\":{\"acl\":true}}]}";
  mm::ClaimedJob jobs[mm::kClaimMax];
  int count = 0;
  uint32_t serverTime = 0;
  char err[96] = {0};
  ASSERT_TRUE(mm::parseClaimResponse(&reply[0], reply.size(), jobs, mm::kClaimMax, count, serverTime, err,
                                     sizeof err))
      << err;
  ASSERT_EQ(count, 1);
  EXPECT_TRUE(jobs[0].hasAclLevel);
  EXPECT_EQ(jobs[0].aclLevel, mm::kPermReadOnly);
}

// `groupWith` is what makes a claimed pair one login: the two jobs of the recorded claim name
// each other, and a job for another repeater is nobody's group.
TEST(Claim, GroupWithNamesTheJobsThatShareALogin) {
  std::string reply = recordedClaim();
  mm::ClaimedJob jobs[mm::kClaimMax];
  int count = 0;
  uint32_t serverTime = 0;
  char err[96] = {0};
  ASSERT_TRUE(mm::parseClaimResponse(&reply[0], reply.size(), jobs, mm::kClaimMax, count, serverTime, err,
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
  std::string reply = recordedClaim();
  mm::ClaimedJob jobs[mm::kClaimMax];
  int count = 0;
  uint32_t serverTime = 0;
  char err[96] = {0};
  ASSERT_TRUE(mm::parseClaimResponse(&reply[0], reply.size(), jobs, mm::kClaimMax, count, serverTime, err,
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
  std::string reply = "{\"serverTime\":\"2026-09-18T10:00:03Z\",\"jobs\":[]}";
  mm::ClaimedJob jobs[mm::kClaimMax];
  int count = 1;
  uint32_t serverTime = 0;
  char err[96] = {0};
  EXPECT_TRUE(mm::parseClaimResponse(&reply[0], reply.size(), jobs, mm::kClaimMax, count, serverTime, err,
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
    std::string body = c.json;
    mm::ClaimedJob jobs[mm::kClaimMax];
    int count = 0;
    uint32_t serverTime = 0;
    char err[96] = {0};
    EXPECT_FALSE(mm::parseClaimResponse(&body[0], body.size(), jobs, mm::kClaimMax, count, serverTime, err,
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
  std::string reply = recordedClaim();
  mm::ClaimedJob jobs[mm::kClaimMax];
  int count = 0;
  uint32_t serverTime = 0;
  char err[96] = {0};
  ASSERT_TRUE(mm::parseClaimResponse(&reply[0], reply.size(), jobs, mm::kClaimMax, count, serverTime, err,
                                     sizeof err))
      << err;
  ASSERT_EQ(count, 2);

  char why[200] = {0};
  // The fixture's job is guest class with a guest password (task 04): a repeater nobody has
  // associated, read with its guest password, which the claim carries.
  EXPECT_TRUE(mm::canRun(jobs[0], why, sizeof why)) << why;
  EXPECT_TRUE(jobs[0].credential.hasGuestPassword);

  // The acl-class telemetry beside it is exactly what this firmware does.
  EXPECT_TRUE(mm::canRun(jobs[1], why, sizeof why));
  EXPECT_STREQ(why, "");

  // The class table of JobTypes.CredentialClasses: a status at admin runs, a neighbours at guest
  // does not, a trace at anything but none does not, the three password types at admin alone.
  mm::ClaimedJob admin = jobs[1];
  admin.credentialClass = mm::CredentialClass::Admin;
  EXPECT_TRUE(mm::canRun(admin, why, sizeof why)) << why;
  mm::ClaimedJob guestNeighbours = jobs[1];
  std::snprintf(guestNeighbours.type, sizeof guestNeighbours.type, "neighbours");
  guestNeighbours.credentialClass = mm::CredentialClass::Guest;
  EXPECT_FALSE(mm::canRun(guestNeighbours, why, sizeof why));
  EXPECT_NE(std::string(why).find("class guest"), std::string::npos) << why;
  mm::ClaimedJob aclTrace = jobs[1];
  std::snprintf(aclTrace.type, sizeof aclTrace.type, "trace");
  EXPECT_FALSE(mm::canRun(aclTrace, why, sizeof why));
  for (const char* type : {"discover", "acl.set", "config.read"}) {
    mm::ClaimedJob job = jobs[1];
    std::snprintf(job.type, sizeof job.type, "%s", type);
    EXPECT_FALSE(mm::canRun(job, why, sizeof why)) << type << " at acl";
    job.credentialClass = mm::CredentialClass::Admin;
    EXPECT_TRUE(mm::canRun(job, why, sizeof why)) << type << " at admin: " << why;
  }

  mm::ClaimedJob unknown = jobs[1];
  std::snprintf(unknown.type, sizeof unknown.type, "config.write");
  EXPECT_FALSE(mm::canRun(unknown, why, sizeof why));
  EXPECT_NE(std::string(why).find("no handler"), std::string::npos) << why;

  mm::ClaimedJob keyless = jobs[1];
  keyless.repeaterKey[0] = 0;
  EXPECT_FALSE(mm::canRun(keyless, why, sizeof why));
  EXPECT_NE(std::string(why).find("public key"), std::string::npos) << why;
}

// Every type of API §3, in JobTypes.All's order - which is the order AgentHeartbeat.json carries
// them, so test_envelopes holds the heartbeat's bytes to the fixture with this very list.
TEST(Claim, TheThirteenTypesAreTheOnesThisFirmwareAdvertisesInTheFixturesOrder) {
  ASSERT_EQ(mm::kJobTypeCount, 13);
  const char* expected[] = {"status",   "telemetry",      "neighbours", "config.read",  "config.apply", "cli",   "trace",
                            "discover", "advert.request", "clock.sync", "password.set", "acl.set",      "reboot"};
  for (int i = 0; i < mm::kJobTypeCount; i++) {
    EXPECT_STREQ(mm::kJobTypes[i], expected[i]) << i;
    EXPECT_TRUE(mm::isKnownJobType(mm::kJobTypes[i]));
  }
  EXPECT_FALSE(mm::isKnownJobType("config.write"));
  EXPECT_FALSE(mm::isKnownJobType(nullptr));
}

// The six writes run at admin and at nothing less (JobTypes.CredentialClasses).
TEST(Claim, TheWritesRunAtAdminAlone) {
  std::string reply = recordedClaim();
  mm::ClaimedJob jobs[mm::kClaimMax];
  int count = 0;
  uint32_t serverTime = 0;
  char err[96] = {0};
  ASSERT_TRUE(mm::parseClaimResponse(&reply[0], reply.size(), jobs, mm::kClaimMax, count, serverTime, err, sizeof err))
      << err;
  char why[200] = {0};
  for (const char* type : {"config.apply", "cli", "advert.request", "clock.sync", "password.set", "reboot"}) {
    mm::ClaimedJob job = jobs[1];
    std::snprintf(job.type, sizeof job.type, "%s", type);
    EXPECT_FALSE(mm::canRun(job, why, sizeof why)) << type << " at acl";
    job.credentialClass = mm::CredentialClass::Guest;
    EXPECT_FALSE(mm::canRun(job, why, sizeof why)) << type << " at guest";
    job.credentialClass = mm::CredentialClass::Admin;
    EXPECT_TRUE(mm::canRun(job, why, sizeof why)) << type << " at admin: " << why;
  }
}

// The writes' payloads (task 05), each the contract fixture: ConfigApplyPayload.json's plan,
// CliPayload.json's command, AdvertRequestPayload.json's flood, PasswordSetPayload.json's field
// - and the new password beside the login material in the credential, never in the payload.
TEST(Claim, TheWritesPayloadsAreRead) {
  const std::string head = "{\"serverTime\":\"2026-09-18T10:00:03Z\",\"jobs\":[";
  const std::string common =
      ",\"repeater\":{\"id\":17,\"label\":\"x\",\"publicKey\":\"a1b2\"},\"credentialClass\":\"admin\","
      "\"leaseUntil\":\"2026-09-18T10:02:03Z\",\"timeouts\":{\"perHopMs\":4000,\"loginMs\":8000},\"payload\":";

  std::string reply = head + "{\"id\":30,\"type\":\"config.apply\"" + common + fixture("ConfigApplyPayload") +
                      ",\"credential\":{\"adminPassword\":\"hunter2\",\"acl\":1}}," + "{\"id\":31,\"type\":\"cli\"" + common +
                      fixture("CliPayload") + ",\"credential\":{\"adminPassword\":\"hunter2\"}}]}";
  mm::ClaimedJob jobs[mm::kClaimMax];
  int count = 0;
  uint32_t serverTime = 0;
  char err[96] = {0};
  ASSERT_TRUE(mm::parseClaimResponse(&reply[0], reply.size(), jobs, mm::kClaimMax, count, serverTime, err, sizeof err))
      << err;
  ASSERT_EQ(count, 2);

  const mm::ConfigApplyPlan& plan = jobs[0].apply;
  EXPECT_FALSE(plan.refused) << plan.refusedWhy;
  ASSERT_EQ(plan.commandCount, 2);
  EXPECT_STREQ(plan.commands[0].key, "advert.interval");
  EXPECT_STREQ(plan.commands[0].command, "set advert.interval 240");
  EXPECT_EQ(plan.commands[0].lineCount, 0) << "a plain set: one line, expecting OK";
  EXPECT_STREQ(plan.commands[1].key, "repeat");
  EXPECT_STREQ(plan.commands[1].command, "set repeat on");
  EXPECT_STREQ(plan.planHash, "3f2a9c0d1e4b5a6f7c8d9e0f1a2b3c4d");
  EXPECT_TRUE(jobs[0].hasAclLevel) << "the granted level rides beside the admin password";

  EXPECT_TRUE(jobs[1].hasCliCommand);
  EXPECT_STREQ(jobs[1].cliCommand, "neighbors");
  EXPECT_FALSE(jobs[1].cliCommandTooLong);
  EXPECT_FALSE(jobs[1].apply.refused) << "a job that is not an apply carries no plan and refuses nothing";

  // An apply's regions entry: its lines, with what each expects and the one that escapes.
  std::string regions =
      head + "{\"id\":32,\"type\":\"config.apply\"" + common +
      "{\"commands\":[{\"key\":\"regions\",\"command\":\"region load\",\"lines\":[{\"line\":\"region load\",\"expect\":null,"
      "\"escape\":false},{\"line\":\" dk F\",\"expect\":null,\"escape\":false},{\"line\":\" \",\"expect\":\"OK - loaded\","
      "\"escape\":true},{\"line\":\"region default dk\",\"expect\":\"default scope is now\"},{\"line\":\"region save\","
      "\"expect\":\"OK\"}]}],\"planHash\":\"abc\"}" +
      ",\"credential\":{\"adminPassword\":\"hunter2\"}}]}";
  ASSERT_TRUE(mm::parseClaimResponse(&regions[0], regions.size(), jobs, mm::kClaimMax, count, serverTime, err, sizeof err))
      << err;
  ASSERT_EQ(count, 1);
  ASSERT_EQ(jobs[0].apply.commandCount, 1);
  ASSERT_EQ(jobs[0].apply.commands[0].lineCount, 5);
  EXPECT_STREQ(jobs[0].apply.lineText(0), "region load");
  EXPECT_EQ(jobs[0].apply.lineExpect(0), nullptr);
  EXPECT_STREQ(jobs[0].apply.lineText(2), " ");
  EXPECT_STREQ(jobs[0].apply.lineExpect(2), "OK - loaded");
  EXPECT_TRUE(jobs[0].apply.lines[2].escape);
  EXPECT_STREQ(jobs[0].apply.lineExpect(3), "default scope is now");
  EXPECT_FALSE(jobs[0].apply.lines[3].escape);

  // advert.request's flood (true when absent), password.set's field and the new password, and
  // the candidate word of an unverified credential.
  std::string small = head + "{\"id\":33,\"type\":\"advert.request\"" + common + fixture("AdvertRequestPayload") +
                      ",\"credential\":{\"adminPassword\":\"hunter2\"}}," + "{\"id\":34,\"type\":\"password.set\"" + common +
                      fixture("PasswordSetPayload") +
                      ",\"credential\":{\"adminPassword\":\"hunter2\",\"newPassword\":\"Rotated-1\",\"candidate\":\"new\"}}]}";
  ASSERT_TRUE(mm::parseClaimResponse(&small[0], small.size(), jobs, mm::kClaimMax, count, serverTime, err, sizeof err))
      << err;
  ASSERT_EQ(count, 2);
  EXPECT_TRUE(jobs[0].advertFlood);
  EXPECT_EQ(jobs[1].passwordField, mm::PasswordField::Admin);
  EXPECT_STREQ(mm::passwordFieldName(jobs[1].passwordField), "adminPassword");
  EXPECT_TRUE(jobs[1].credential.hasNewPassword);
  EXPECT_STREQ(jobs[1].credential.newPassword, "Rotated-1");
  EXPECT_FALSE(jobs[1].credential.newPasswordCut);
  EXPECT_STREQ(jobs[1].credentialCandidate, "new");
  EXPECT_STREQ(jobs[0].credentialCandidate, "");

  std::string zerohop = head + "{\"id\":35,\"type\":\"advert.request\"" + common + "{\"flood\":false}" +
                        ",\"credential\":{\"adminPassword\":\"hunter2\"}}," + "{\"id\":36,\"type\":\"password.set\"" + common +
                        "{\"field\":\"guestPassword\"},\"credential\":{\"adminPassword\":\"hunter2\",\"newPassword\":\"sixteen-chars-xx\"}}]}";
  ASSERT_TRUE(mm::parseClaimResponse(&zerohop[0], zerohop.size(), jobs, mm::kClaimMax, count, serverTime, err, sizeof err))
      << err;
  EXPECT_FALSE(jobs[0].advertFlood);
  EXPECT_EQ(jobs[1].passwordField, mm::PasswordField::Guest);
  EXPECT_TRUE(jobs[1].credential.newPasswordCut) << "sixteen bytes: the firmware keeps fifteen, and the runner refuses to send it";

  // A plan this firmware cannot hold is refused by name, inside the job: the claim still parses.
  std::string big = head + "{\"id\":37,\"type\":\"config.apply\"" + common + "{\"commands\":[";
  for (int i = 0; i <= mm::kMaxApplyCommands; i++) {
    big += std::string(i > 0 ? "," : "") + "{\"key\":\"k" + std::to_string(i) + "\",\"command\":\"set k 1\"}";
  }
  big += "]},\"credential\":{\"adminPassword\":\"hunter2\"}}]}";
  ASSERT_TRUE(mm::parseClaimResponse(&big[0], big.size(), jobs, mm::kClaimMax, count, serverTime, err, sizeof err)) << err;
  EXPECT_TRUE(jobs[0].apply.refused);
  EXPECT_NE(std::string(jobs[0].apply.refusedWhy).find("more than"), std::string::npos) << jobs[0].apply.refusedWhy;

  std::string none = head + "{\"id\":38,\"type\":\"config.apply\"" + common + "{}" + ",\"credential\":{\"adminPassword\":\"hunter2\"}}]}";
  ASSERT_TRUE(mm::parseClaimResponse(&none[0], none.size(), jobs, mm::kClaimMax, count, serverTime, err, sizeof err)) << err;
  EXPECT_TRUE(jobs[0].apply.refused);
  EXPECT_NE(std::string(jobs[0].apply.refusedWhy).find("no plan"), std::string::npos);
}

// The two payloads that say something (task 04): a config.read's keys and an acl.set's entry.
// The samples are the contract fixtures, ConfigReadPayload.json and AclSetPayload.json.
TEST(Claim, TheConfigReadAndAclSetPayloadsAreRead) {
  std::string reply = std::string("{\"serverTime\":\"2026-09-18T10:00:03Z\",\"jobs\":[") +
                      "{\"id\":20,\"type\":\"config.read\",\"repeater\":{\"id\":17,\"label\":\"x\",\"publicKey\":\"a1b2\"},"
                      "\"credentialClass\":\"admin\",\"leaseUntil\":\"2026-09-18T10:02:03Z\",\"timeouts\":{\"perHopMs\":4000,"
                      "\"loginMs\":8000},\"payload\":" +
                      fixture("ConfigReadPayload") + ",\"credential\":{\"adminPassword\":\"hunter2\",\"acl\":1}}," +
                      "{\"id\":21,\"type\":\"acl.set\",\"repeater\":{\"id\":17,\"label\":\"x\",\"publicKey\":\"a1b2\"},"
                      "\"credentialClass\":\"admin\",\"leaseUntil\":\"2026-09-18T10:02:03Z\",\"timeouts\":{\"perHopMs\":4000,"
                      "\"loginMs\":8000},\"payload\":" +
                      fixture("AclSetPayload") + ",\"credential\":{\"adminPassword\":\"hunter2\"}}]}";
  mm::ClaimedJob jobs[mm::kClaimMax];
  int count = 0;
  uint32_t serverTime = 0;
  char err[96] = {0};
  ASSERT_TRUE(mm::parseClaimResponse(&reply[0], reply.size(), jobs, mm::kClaimMax, count, serverTime, err, sizeof err))
      << err;
  ASSERT_EQ(count, 2);

  EXPECT_TRUE(jobs[0].hasConfigKeys);
  ASSERT_EQ(jobs[0].configKeyCount, 3);
  EXPECT_STREQ(jobs[0].configKeys[0], "name");
  EXPECT_STREQ(jobs[0].configKeys[1], "advert.interval");
  EXPECT_STREQ(jobs[0].configKeys[2], "flood.max");
  EXPECT_TRUE(jobs[0].hasAclLevel);
  EXPECT_EQ(jobs[0].aclLevel, 1) << "the granted level travels beside the admin password (task 60)";
  EXPECT_FALSE(jobs[0].hasAclSet);

  EXPECT_TRUE(jobs[1].hasAclSet);
  EXPECT_STREQ(jobs[1].aclSetKey, "a1b2c3d4e5f60718");
  EXPECT_EQ(jobs[1].aclSetLevel, 1);
  EXPECT_FALSE(jobs[1].hasConfigKeys) << "an empty or absent list is the default list";

  // An empty `keys` is the default list too, and a key list past the table is refused by name.
  std::string empty =
      "{\"serverTime\":\"2026-09-18T10:00:03Z\",\"jobs\":[{\"id\":22,\"type\":\"config.read\",\"repeater\":{\"id\":17,"
      "\"label\":\"x\",\"publicKey\":\"a1b2\"},\"credentialClass\":\"admin\",\"leaseUntil\":\"2026-09-18T10:02:03Z\","
      "\"payload\":{\"keys\":[]}}]}";
  ASSERT_TRUE(mm::parseClaimResponse(&empty[0], empty.size(), jobs, mm::kClaimMax, count, serverTime, err, sizeof err))
      << err;
  EXPECT_FALSE(jobs[0].hasConfigKeys);
  std::string tooMany =
      "{\"serverTime\":\"2026-09-18T10:00:03Z\",\"jobs\":[{\"id\":23,\"type\":\"config.read\",\"repeater\":{\"id\":17,"
      "\"label\":\"x\",\"publicKey\":\"a1b2\"},\"credentialClass\":\"admin\",\"leaseUntil\":\"2026-09-18T10:02:03Z\","
      "\"payload\":{\"keys\":[";
  for (int i = 0; i <= mm::kMaxConfigKeys; i++) tooMany += std::string(i > 0 ? "," : "") + "\"k" + std::to_string(i) + "\"";
  tooMany += "]}}]}";
  EXPECT_FALSE(
      mm::parseClaimResponse(&tooMany[0], tooMany.size(), jobs, mm::kClaimMax, count, serverTime, err, sizeof err));
  EXPECT_NE(std::string(err).find("more keys"), std::string::npos) << err;
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
