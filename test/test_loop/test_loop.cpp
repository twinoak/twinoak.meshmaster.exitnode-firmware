// The agent loop, end to end, on a host (task p1.5-17).
//
// `AgentLoop` is driven here exactly as `main.cpp` drives it - `onLinkUp()` when the link comes
// up and `loop()` from the Arduino loop - against the three fakes of `test/support`. The order
// inside one pass is EXIT-NODES §5's and `docs/ops/agent.md` §4's, in those words: the
// heartbeat, then **the offline buffer**, then the claim, then the observation batch; and what a
// failure costs is `agent/ServerPolicy.h`'s, which test/test_server_policy holds code by code and
// this holds as the loop's own behaviour - the wait, the counters, and the one drastic thing a
// board does to itself after twelve unanswered calls over ninety minutes.
//
// Nothing sleeps and nothing restarts: the clock is the test's (support/FakePlatform.h) and
// `rebootForHealth` records its reason and throws, because the method may not return and a test
// that saw the guard fire must not fall through into the rest of the loop.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <agent/AgentLoop.h>
#include <agent/Envelopes.h>
#include <agent/Iso8601.h>
#include <agent/JobRunner.h>
#include <agent/Observations.h>
#include <agent/ServerPolicy.h>

#include <support/FakeMesh.h>
#include <support/FakePlatform.h>
#include <support/FakeUplink.h>
#include <support/Fixtures.h>
#include <support/Jobs.h>

namespace {

constexpr uint32_t kNow = 1790000000;
constexpr uint32_t kLease = kNow + 1800;

const std::string& repeater() { return fake::repeaterKey(); }
const std::string& self() { return fake::selfKey(); }

// The pump the board hands the runner (`main.cpp`'s `pumpDuringAJob`): a plain function pointer,
// so the loop it serves is a file static here as it is there.
mm::AgentLoop* g_loop = nullptr;
void pumpDuringAJob() {
  if (g_loop != nullptr) g_loop->heartbeatIfOverdue();
}

// Two invented types, to see the extension's types in the heartbeat (agent/IJobExtension.h).
class FakeExtension : public mm::IJobExtension {
 public:
  int typeCount() const override { return 2; }
  const char* type(int i) const override { return i == 0 ? "walter.sleep" : "walter.firmware"; }
  bool run(const mm::ClaimedJob&, mm::JobContext&, mm::ResultWriter& out) override {
    out.outcome(mm::Outcome::Ok);
    return true;
  }
};

// One heard advert, for the observation batch.
mm::HeardAdvert advert(uint8_t n, uint32_t heardAtMs) {
  mm::HeardAdvert a;
  for (size_t i = 0; i < mm::kPublicKeyLen; i++) a.publicKey[i] = static_cast<uint8_t>(n + i);
  a.heardAtMillis = heardAtMs;
  a.pathBytes[0] = 0xa1;
  a.pathByteLen = 1;
  a.pathHashSize = 1;
  a.hops = 1;
  a.snrQ4 = 25;
  a.rssi = -97;
  a.advType = 2;  // ADV_TYPE_REPEATER
  std::snprintf(a.name, sizeof a.name, "Repeater %u", static_cast<unsigned>(n));
  return a;
}

std::string memberOf(const std::string& json, const std::string& key) {
  const std::string needle = "\"" + key + "\":";
  const size_t found = json.find(needle);
  if (found == std::string::npos) return "";
  size_t at = found + needle.size();
  if (json[at] == '"') {
    const size_t end = json.find('"', at + 1);
    return end == std::string::npos ? "" : json.substr(at + 1, end - at - 1);
  }
  if (json[at] == '{' || json[at] == '[') {
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (size_t i = at; i < json.size(); i++) {
      const char c = json[i];
      if (inString) {
        if (escaped) {
          escaped = false;
        } else if (c == '\\') {
          escaped = true;
        } else if (c == '"') {
          inString = false;
        }
        continue;
      }
      if (c == '"') {
        inString = true;
      } else if (c == '{' || c == '[') {
        depth++;
      } else if (c == '}' || c == ']') {
        depth--;
        if (depth == 0) return json.substr(at, i - at + 1);
      }
    }
    return "";
  }
  size_t end = at;
  while (end < json.size() && json[end] != ',' && json[end] != '}') end++;
  return json.substr(at, end - at);
}

class LoopTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fake::LogCapture::the().begin();
    platform.fakeClock().setFromNetwork(kNow);
    mesh.setKey(repeater());
    mesh.setSelfKey(self());
    mesh.setAdminPassword("adm1n-pass");
    mesh.grant(self(), mm::kPermReadOnly);
    server.setLinkUp(true);
    server.setServerTime(kNow);

    identity.agentVersion = "0.2.0";
    identity.board = "heltec_v3";
    identity.companionFirmware = "companion-v1.17.1";
    identity.radio.freqMHz = 869.618;
    identity.radio.bwKHz = 62.5;
    identity.radio.sf = 8;
    identity.radio.cr = 5;

    jobs.begin(mesh, server, platform, &pumpDuringAJob);
    loop.begin(server, platform, mesh, jobs, identity);
    loop.setIdentity(self().c_str());
    g_loop = &loop;
  }
  void TearDown() override { g_loop = nullptr; }

  // The link comes up and the first config call falls due: what `main.cpp` does when WiFi
  // associates, and the 1500 ms the loop gives DNS and SNTP before it asks.
  void linkUp() {
    loop.onLinkUp();
    platform.advance(1600);
  }

  // Turn the Arduino loop until `server.calls()` grows, or give up. Nothing sleeps: a turn that
  // finds nothing due costs one millisecond of the fake clock.
  bool untilACall(uint32_t withinMs = 200000) {
    const size_t before = server.calls().size();
    const uint32_t deadline = platform.millis() + withinMs;
    while (platform.millis() < deadline) {
      loop.loop();
      if (server.calls().size() > before) return true;
      platform.advance(250);
    }
    return false;
  }

  // The budget most of these tests want: wide, so the pacing is not what is being measured.
  void wideBudget() { server.answerHeartbeat(60, 1, 30, true, platform.clock().now()); }

  fake::FakePlatform platform;
  fake::FakeMesh mesh{platform};
  fake::FakeUplink server{platform};
  mm::JobRunner jobs;
  mm::HeartbeatIdentity identity;
  mm::AgentLoop loop;
};

// ---------------------------------------------------------------------------------------------
// One pass, in the order EXIT-NODES §5 gives it

TEST_F(LoopTest, ThePassIsConfigThenHeartbeatThenTheClaimThenTheBatch) {
  wideBudget();
  server.queue(fake::Job(13000, "status")
                   .atClass("acl")
                   .leasedUntil(kLease)
                   .withAclLevel(mm::kPermReadOnly)
                   .withPayload("{\"version\":false}")
                   .json());
  for (uint8_t i = 0; i < mm::kObservationBatchSize; i++) loop.ring().push(advert(i, platform.millis()));

  linkUp();
  for (int pass = 0; pass < 6 && untilACall(); pass++) {
  }

  const std::vector<std::string>& calls = server.calls();
  ASSERT_GE(calls.size(), 5u) << "only " << calls.size() << " call(s) were made";
  EXPECT_EQ(calls[0], "config") << "GET /agent/v1/config, once, when the link comes up";
  EXPECT_EQ(calls[1], "heartbeat");
  EXPECT_EQ(calls[2], "claim") << "the heartbeat said work was waiting";
  EXPECT_EQ(calls[3], "result") << "each result is posted as its job ends";
  EXPECT_EQ(calls[4], "observations") << "the batch is last";
  EXPECT_GE(loop.counters().heartbeats, 1u);
  EXPECT_EQ(loop.counters().batches, 1u);
  EXPECT_EQ(loop.counters().observationsPosted, mm::kObservationBatchSize);
  EXPECT_TRUE(loop.online());
  EXPECT_EQ(loop.retrySeconds(), 0u);
  EXPECT_EQ(jobs.ran(), 1u);
}

TEST_F(LoopTest, TheOfflineBufferIsPostedBeforeAnythingNew) {
  wideBudget();
  server.failResults(fake::FakeUplink::Fault::Unreachable, 1);
  server.queue(fake::Job(13001, "status")
                   .atClass("acl")
                   .leasedUntil(kLease)
                   .withAclLevel(mm::kPermReadOnly)
                   .withPayload("{\"version\":false}")
                   .json());

  linkUp();
  ASSERT_TRUE(untilACall());  // config
  ASSERT_TRUE(untilACall());  // heartbeat
  ASSERT_TRUE(untilACall());  // the claim, the job, and the result the server would not take
  ASSERT_EQ(jobs.queueDepth(), 1) << "the result the server would not take";
  const size_t afterTheClaim = server.calls().size();

  // An hour of a server that is away, and then it is back. The heartbeat of the new pass goes
  // first - it is the call the interval is measured by - and then **the buffer, before the claim
  // the same heartbeat asked for** (EXIT-NODES §5, docs/ops/agent.md §4).
  platform.advanceSeconds(3600);
  ASSERT_TRUE(untilACall());
  ASSERT_TRUE(untilACall());
  ASSERT_GT(server.calls().size(), afterTheClaim + 1);
  EXPECT_EQ(server.calls()[afterTheClaim], "heartbeat");
  EXPECT_EQ(server.calls()[afterTheClaim + 1], "result") << "the buffer, before the claim";
  EXPECT_EQ(jobs.queueDepth(), 0);
  EXPECT_EQ(loop.counters().resultsPosted, 1u);
  ASSERT_EQ(server.results().size(), 1u);
  uint32_t finishedAt = 0;
  ASSERT_TRUE(mm::parseUtc(memberOf(server.results()[0].body, "finishedAt").c_str(), finishedAt));
  EXPECT_GE(platform.clock().now() - finishedAt, 3600u) << "with the timestamps it was made with";
}

TEST_F(LoopTest, AHeartbeatThatIsDueInsideALongJobGoesFromTheJobsOwnWait) {
  // Two transactions a minute: a config read of three keys is minutes of waiting, and the
  // heartbeat that falls due inside it goes from the pump rather than after the job (task 06).
  server.answerHeartbeat(mm::kDefaultMaxTransactionsPerMinute, mm::kDefaultMinGapSeconds, 30, true,
                         platform.clock().now());
  server.queue(fake::Job(13002, "config.read")
                   .atClass("admin")
                   .leasedUntil(kNow + 1800)
                   .withAdminPassword("adm1n-pass")
                   .withPayload("{\"keys\":[\"name\",\"tx\",\"repeat\"]}")
                   .json());

  linkUp();
  for (int pass = 0; pass < 4 && untilACall(); pass++) {
  }

  const std::vector<std::string>& calls = server.calls();
  ASSERT_GE(calls.size(), 4u);
  EXPECT_EQ(calls[0], "config");
  EXPECT_EQ(calls[1], "heartbeat");
  EXPECT_EQ(calls[2], "claim");
  // Between the claim and the result there is at least one more heartbeat: the job was six
  // transactions at two a minute, which is past the thirty-second poll interval several times.
  int heartbeatsInsideTheJob = 0;
  for (size_t i = 3; i < calls.size() && calls[i] != "result"; i++) {
    if (calls[i] == "heartbeat") heartbeatsInsideTheJob++;
  }
  EXPECT_GE(heartbeatsInsideTheJob, 1) << "the exit node's page went quiet while the board worked (task 06)";
  EXPECT_GE(loop.counters().heartbeats, 2u);
  EXPECT_GE(jobs.ran(), 1u);
  // And nothing else of the loop ran in there: between the claim and the result every call is a
  // heartbeat. No claim, no batch - one blocking call inside a wait that was going to be spent
  // waiting, and nothing more.
  for (size_t i = 3; i < calls.size() && calls[i] != "result"; i++) {
    EXPECT_EQ(calls[i], "heartbeat") << "call " << i << " came from inside a job";
  }
}

// ---------------------------------------------------------------------------------------------
// The heartbeat's body

TEST_F(LoopTest, TheHeartbeatsBodyIsTheContractsShapeAndThisBoardsIdentity) {
  linkUp();
  ASSERT_TRUE(untilACall());  // config
  ASSERT_TRUE(untilACall());  // heartbeat
  ASSERT_EQ(server.heartbeats().size(), 1u);
  const std::string hb = server.heartbeats()[0];

  EXPECT_TRUE(fake::followsShapeOf(hb, fake::contract("AgentHeartbeat"))) << hb;
  EXPECT_NE(hb.find("\"transport\":\"onboard\""), std::string::npos) << hb;
  EXPECT_NE(hb.find("\"agentVersion\":\"0.2.0\""), std::string::npos) << hb;
  EXPECT_NE(hb.find("\"board\":\"heltec_v3\""), std::string::npos) << hb;
  EXPECT_NE(hb.find("\"companionFirmware\":\"companion-v1.17.1\""), std::string::npos) << hb;
  EXPECT_NE(hb.find("\"companionPublicKey\":\"" + self() + "\""), std::string::npos) << hb;
  EXPECT_NE(hb.find("\"companionConnected\":true"), std::string::npos) << hb;
  EXPECT_NE(hb.find("\"queueDepth\":0"), std::string::npos) << hb;
  EXPECT_EQ(memberOf(hb, "radio"), "{\"freqMHz\":869.618,\"bwKHz\":62.5,\"sf\":8,\"cr\":5}") << hb;
  // `at` is this board's clock as the server writes a DateTime.
  char at[24];
  mm::formatUtc(platform.clock().now(), at, sizeof at);
  EXPECT_NE(hb.find(std::string("\"at\":\"") + at + "\""), std::string::npos) << hb;

  // The thirteen, in JobTypes.All's order, and nothing else: routing requires the type advertised.
  std::string types = "[";
  for (int i = 0; i < mm::kJobTypeCount; i++) {
    types += std::string(i > 0 ? "," : "") + "\"" + mm::kJobTypes[i] + "\"";
  }
  types += "]";
  EXPECT_EQ(memberOf(hb, "jobTypes"), types) << hb;

  // The six counters the heartbeat carries, by the names the server shows.
  const std::string errors = memberOf(hb, "errors");
  for (const char* name : {"overheard", "wifiReconnects", "httpFailures", "observationsDropped", "resultsDropped",
                           "reboots"}) {
    EXPECT_NE(errors.find(std::string("\"") + name + "\":"), std::string::npos) << name << " in " << errors;
  }
}

TEST_F(LoopTest, AnExtensionsTypesFollowTheThirteenInTheHeartbeat) {
  FakeExtension extension;
  jobs.setExtension(&extension);

  linkUp();
  ASSERT_TRUE(untilACall());  // config
  ASSERT_TRUE(untilACall());  // heartbeat
  ASSERT_EQ(server.heartbeats().size(), 1u);
  const std::string types = memberOf(server.heartbeats()[0], "jobTypes");

  EXPECT_NE(types.find("\"reboot\",\"walter.sleep\",\"walter.firmware\"]"), std::string::npos)
      << "the thirteen first, then the extension's: " << types;
}

TEST_F(LoopTest, TheObservationBatchIsTheContractsShapeAndKeepsItsClientIdAcrossARetry) {
  // No jobs waiting: the batch is what the pass after the heartbeat has to do.
  server.answerHeartbeat(60, 1, 120, false, platform.clock().now());
  for (uint8_t i = 0; i < mm::kObservationBatchSize; i++) loop.ring().push(advert(i, platform.millis()));

  linkUp();
  ASSERT_TRUE(untilACall());  // config
  ASSERT_TRUE(untilACall());  // heartbeat

  // The batch is refused once with a 5xx, and the retry is the same batch with the same id.
  server.failWith(fake::FakeUplink::Fault::ServerError, 1);
  ASSERT_TRUE(untilACall());
  ASSERT_EQ(server.observations().size(), 1u);
  const std::string first = server.observations()[0];
  EXPECT_EQ(loop.ring().size(), mm::kObservationBatchSize) << "a batch the server refused is not consumed";

  platform.advanceSeconds(120);
  for (int pass = 0; pass < 4 && server.observations().size() < 2 && untilACall(); pass++) {
  }
  ASSERT_EQ(server.observations().size(), 2u);
  EXPECT_EQ(memberOf(server.observations()[1], "clientId"), memberOf(first, "clientId"))
      << "the same batch is the same client id, or the server would store nothing and the items would go unsent";
  EXPECT_TRUE(fake::followsShapeOf(server.observations()[1], fake::contract("ObservationBatch")));
  EXPECT_EQ(loop.ring().size(), 0u);
  EXPECT_EQ(loop.counters().batches, 1u);
}

// ---------------------------------------------------------------------------------------------
// What the server's answers cost (agent/ServerPolicy.h)

TEST_F(LoopTest, A401HaltsEverythingUntilAPersonFixesIt) {
  linkUp();
  server.failAlways(fake::FakeUplink::Fault::Unauthorized);
  ASSERT_TRUE(untilACall());  // the config call

  EXPECT_TRUE(loop.halted());
  EXPECT_FALSE(loop.online());
  const size_t calls = server.calls().size();
  platform.advanceSeconds(600);
  for (int i = 0; i < 100; i++) loop.loop();
  EXPECT_EQ(server.calls().size(), calls) << "nothing more is sent after a 401";

  // `set meshmaster.token` (or a reboot) is what starts it again.
  server.answerAgain();
  loop.reconfigure();
  platform.advance(10);
  EXPECT_FALSE(loop.halted());
  EXPECT_TRUE(untilACall());
}

TEST_F(LoopTest, A429IsHeldForItsRetryAfterToTheSecond) {
  linkUp();
  ASSERT_TRUE(untilACall());  // config
  server.failWith(fake::FakeUplink::Fault::RateLimited, 1);
  server.setRetryAfter(45);
  ASSERT_TRUE(untilACall());  // the heartbeat, answered 429

  EXPECT_EQ(loop.retrySeconds(), 45u) << "Retry-After's own number, to the second";
  EXPECT_FALSE(loop.online()) << "a server that said *come back in 45 s* has not answered (task p1.3-12)";
  EXPECT_EQ(loop.counters().httpFailures, 0u) << "a 429 is not an http failure (EXIT-NODES §6)";
  EXPECT_EQ(loop.counters().consecutiveFailures, 0u) << "nor one of the twelve the reboot guard counts";
}

TEST_F(LoopTest, A503ThatSaysWhenToComeBackIsBelievedAndNotDoubledPast) {
  linkUp();
  ASSERT_TRUE(untilACall());  // config
  server.failWith(fake::FakeUplink::Fault::ServerError, 1);
  server.setRetryAfter(20);
  ASSERT_TRUE(untilACall());  // the heartbeat, answered 503 with a Retry-After

  EXPECT_LE(loop.retrySeconds(), 20u);
  EXPECT_GE(loop.retrySeconds(), 18u) << "the deploy's own number, not thirty doubling (task p1.2-10)";
  EXPECT_EQ(loop.counters().httpFailures, 1u) << "a 5xx still shows in the counters";
  EXPECT_EQ(loop.counters().consecutiveFailures, 0u);
}

TEST_F(LoopTest, A5xxWithNoRetryAfterBacksOffFromHalfAMinuteWithJitter) {
  linkUp();
  ASSERT_TRUE(untilACall());  // config
  server.failAlways(fake::FakeUplink::Fault::ServerError);
  server.setRetryAfter(0);
  ASSERT_TRUE(untilACall());  // the heartbeat

  EXPECT_EQ(loop.counters().consecutiveFailures, 1u);
  // 30 s plus or minus a quarter (agent/Backoff.h), so a hundred boards drift apart.
  EXPECT_GE(loop.retrySeconds(), 22u);
  EXPECT_LE(loop.retrySeconds(), 38u);

  // The second step is a minute, the third two - and the ceiling is two minutes while the link is
  // proven (the clock came off the network), which is what task p1.2-10 lowered it to.
  uint32_t worst = 0;
  for (int i = 0; i < 8; i++) {
    platform.advanceSeconds(loop.retrySeconds() + 31);
    ASSERT_TRUE(untilACall());
    if (loop.retrySeconds() > worst) worst = loop.retrySeconds();
  }
  EXPECT_LE(worst, static_cast<uint32_t>(mm::kMaxBackoffWhenLinkUpMs / 1000 * 125 / 100))
      << "the proven link's ceiling is two minutes";
  EXPECT_GE(loop.counters().consecutiveFailures, 8u);
  EXPECT_FALSE(loop.online());
}

TEST_F(LoopTest, TwelveUnansweredCallsOverNinetyMinutesWithTheLinkUpAsksThePlatformToReboot) {
  linkUp();
  ASSERT_TRUE(untilACall());  // config
  server.failAlways(fake::FakeUplink::Fault::Unreachable);

  bool asked = false;
  try {
    // Ten minutes between attempts: past every backoff the ceiling allows, so twelve failures
    // are two hours end to end - the hour and a half the guard wants, and the count.
    for (int i = 0; i < 20 && !asked; i++) {
      platform.advanceSeconds(600);
      loop.loop();
    }
  } catch (const fake::FakePlatform::Reboot& reboot) {
    asked = true;
    EXPECT_NE(reboot.why.find("server calls unanswered"), std::string::npos) << reboot.why;
    EXPECT_NE(reboot.why.find("wifi up"), std::string::npos) << reboot.why;
  }
  EXPECT_TRUE(asked) << "the reboot guard never fired; " << loop.counters().consecutiveFailures << " failure(s)";
  EXPECT_GE(loop.counters().consecutiveFailures, mm::kRebootAfterFailures);
  ASSERT_EQ(platform.reboots().size(), 1u) << "asked once, and nothing restarted";
  EXPECT_EQ(platform.selfReboots(), 1u) << "which is the heartbeat's `reboots` counter";
}

TEST_F(LoopTest, TheGuardDoesNotFireWhileTheLinkIsDown) {
  linkUp();
  ASSERT_TRUE(untilACall());  // config
  server.failAlways(fake::FakeUplink::Fault::Unreachable);
  for (int i = 0; i < 6; i++) {
    platform.advanceSeconds(600);
    loop.loop();
  }
  ASSERT_GE(loop.counters().consecutiveFailures, 3u);

  // The link falls over: the loop stops calling at all, so the count stops rising and the guard
  // - which asks for the link to be *up* - cannot fire.
  server.setLinkUp(false);
  const uint32_t failures = loop.counters().consecutiveFailures;
  for (int i = 0; i < 30; i++) {
    platform.advanceSeconds(600);
    loop.loop();
  }
  EXPECT_EQ(loop.counters().consecutiveFailures, failures);
  EXPECT_TRUE(platform.reboots().empty());
}

// ---------------------------------------------------------------------------------------------
// What the console and the panel read of the loop

TEST_F(LoopTest, TheConsoleReportsReadBackWhatTheLoopDid) {
  class Out : public mm::TextOut {
   public:
    void write(const char* text, size_t len) override { out.append(text, len); }
    std::string out;
  };
  wideBudget();
  mesh.setOverheard(41);
  mesh.setAdvertsDropped(2);
  for (uint8_t i = 0; i < mm::kObservationBatchSize; i++) loop.ring().push(advert(i, platform.millis()));
  linkUp();
  for (int pass = 0; pass < 4 && untilACall(); pass++) {
  }

  Out report;
  loop.report(report);
  EXPECT_NE(report.out.find("state     running"), std::string::npos) << report.out;
  EXPECT_NE(report.out.find("heartbeat last "), std::string::npos) << report.out;
  EXPECT_NE(report.out.find("overheard 41"), std::string::npos) << report.out;

  Out errors;
  loop.reportErrors(errors);
  EXPECT_NE(errors.out.find("overheard            41"), std::string::npos) << errors.out;
  EXPECT_NE(errors.out.find("undecodable adverts  2"), std::string::npos) << errors.out;

  char line[200];
  loop.describe(line, sizeof line);
  EXPECT_NE(std::string(line).find("answered"), std::string::npos) << line;
  EXPECT_TRUE(loop.everHeartbeated());
  EXPECT_NE(loop.lastHeartbeatAtEpoch(), 0u);
}

TEST_F(LoopTest, NothingIsSentWhileTheLinkIsDownOrTheTokenIsMissing) {
  server.setLinkUp(false);
  loop.onLinkUp();
  platform.advanceSeconds(60);
  for (int i = 0; i < 50; i++) loop.loop();
  EXPECT_TRUE(server.calls().empty()) << "no link, no call";

  server.setConfigured(false);
  server.setLinkUp(true);
  loop.onLinkUp();
  platform.advanceSeconds(60);
  for (int i = 0; i < 50; i++) loop.loop();
  EXPECT_TRUE(server.calls().empty()) << "no token, no call";
  EXPECT_FALSE(loop.online());
}

}  // namespace

// PlatformIO's googletest integration links no gtest_main, so every test file brings its own.
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
