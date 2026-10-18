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
//
// Since task p1.6-21 the wake cases also hold **the budget over every call a wake makes**: the
// fake server charges every call the same milliseconds on that clock and records the bytes of
// every body it was handed, so "the uplink's seconds" and "the bytes posted" are arithmetic and
// not an estimate - and the last two cases of the file read the library's own text for the rule
// that cannot be reached from outside: that no call to the server is made anywhere but through
// the wake's ledger.

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
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

#ifndef PROJECT_DIR
#error "PROJECT_DIR must be defined; platformio.ini's [env:native] does it"
#endif

namespace {

namespace fs = std::filesystem;

std::string readFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

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


// ---------------------------------------------------------------------------------------------
// One wake (task p1.5-18): the same steps, in the same order, inside a budget
//
// `loop()` above is the Heltec. Everything below is `wake()`: what a device that sleeps does
// with one bounded slice of time and energy (proposal 01 parts B and C of the repeater-v2
// design, `spec/EXIT-NODES.md` §6 *An agent that paces itself*, and the Walter's
// `docs/ARCHITECTURE.md` §10). No test here calls `loop()` and none of the tests above calls
// `wake()`, so the two ways of being driven are held apart as well as together.

// The intent of a unit that checks in hourly and has room for a dozen transactions this wake.
mm::WakeIntent hourly(int cap = 12) {
  mm::WakeIntent intent;
  intent.nextCheckInS = 3600;
  intent.maxTransactionsThisWake = cap;
  return intent;
}

TEST_F(LoopTest, AWakeWithNothingWaitingIsAConfigCallAHeartbeatAndNothingElse) {
  server.answerHeartbeat(60, 1, 3600, false, platform.clock().now());

  const mm::WakeReport first = loop.wake(hourly(), mm::Budget());

  ASSERT_EQ(server.calls().size(), 2u) << "a wake with nothing waiting is two calls";
  EXPECT_EQ(server.calls()[0], "config") << "the first wake of a boot has no config yet";
  EXPECT_EQ(server.calls()[1], "heartbeat");
  EXPECT_EQ(first.config, mm::StepOutcome::Done);
  EXPECT_EQ(first.heartbeat, mm::StepOutcome::Done);
  EXPECT_EQ(first.results, mm::StepOutcome::Skipped);
  EXPECT_EQ(first.claim, mm::StepOutcome::Skipped) << "the heartbeat said no work was waiting";
  EXPECT_EQ(first.jobs, mm::StepOutcome::Skipped);
  EXPECT_EQ(first.observations, mm::StepOutcome::Skipped) << "the ring is empty";
  EXPECT_STREQ(first.why, "");
  EXPECT_EQ(first.acceptedIntervalS, 3600) << "the reply's pollIntervalS is what the caller sleeps by";
  EXPECT_EQ(first.serverTime, kNow);
  EXPECT_EQ(first.sleepAtLeastS, 0u);
  EXPECT_EQ(first.transactionsSpent, 0) << "nothing went on the air";
  EXPECT_FALSE(first.halted);
  EXPECT_FALSE(first.rebootWanted);
  EXPECT_EQ(loop.counters().heartbeats, 1u);

  // The second wake of the same boot asks for no config: this uplink has one. A device that
  // deep-sleeps loses the object with its RAM and asks again on the next boot, which is what
  // "the config if stale" means for something that sleeps.
  const mm::WakeReport again = loop.wake(hourly(), mm::Budget());
  EXPECT_EQ(again.config, mm::StepOutcome::Skipped);
  EXPECT_EQ(again.heartbeat, mm::StepOutcome::Done);
  EXPECT_EQ(server.configCalls(), 1);
}

TEST_F(LoopTest, TheWakesHeartbeatCarriesTheIntentAndTheReplysIntervalIsTheAcceptedOne) {
  // The server accepted a quarter of an hour where the agent asked for one (AgentLimits clamps
  // rather than refuses, meshmaster.dk's p4.14-120), and says so in `pollIntervalS`.
  server.answerHeartbeat(60, 1, 900, false, platform.clock().now());

  const mm::WakeReport report = loop.wake(hourly(6), mm::Budget());

  ASSERT_EQ(server.heartbeats().size(), 1u);
  const std::string hb = server.heartbeats()[0];
  EXPECT_NE(hb.find("\"nextCheckInS\":3600"), std::string::npos) << hb;
  EXPECT_NE(hb.find("\"maxTransactionsThisWake\":6"), std::string::npos) << hb;
  EXPECT_TRUE(fake::followsShapeOf(hb, fake::contract("AgentHeartbeat"))) << hb;
  EXPECT_EQ(report.acceptedIntervalS, 900) << "the accepted proposal, and never 30 or 120";
}

TEST_F(LoopTest, TheHeltecsHeartbeatCarriesNeitherOfTheTwoWakeFields) {
  // The other half of the same rule: `loop()` sets no intent, so an agent that proposes nothing
  // writes the bytes it wrote before meshmaster.dk's task 120 (test_envelopes holds them against
  // the fixture).
  linkUp();
  ASSERT_TRUE(untilACall());  // config
  ASSERT_TRUE(untilACall());  // heartbeat
  ASSERT_EQ(server.heartbeats().size(), 1u);
  EXPECT_EQ(server.heartbeats()[0].find("nextCheckInS"), std::string::npos) << server.heartbeats()[0];
  EXPECT_EQ(server.heartbeats()[0].find("maxTransactionsThisWake"), std::string::npos) << server.heartbeats()[0];
}

TEST_F(LoopTest, AWakeAsksForNoMoreJobsThanItsCapCanRunAndLeavesTheRestOnTheServer) {
  // Three traces waiting and a cap of two transactions: a trace is one transaction and no login
  // (EXIT-NODES §7), but the claim is sized before any type is known, so the cap buys one job at
  // the cheapest shape there is - a login and a request. One is claimed and run; the other two
  // stay queued and bound to nothing, for the next wake or another exit node (task p4.14-121).
  server.answerHeartbeat(60, 1, 3600, true, platform.clock().now());
  server.queue(std::vector<std::string>{
      fake::Job(14001, "trace").leasedUntil(kLease).json(),
      fake::Job(14002, "trace").leasedUntil(kLease).json(),
      fake::Job(14003, "trace").leasedUntil(kLease).json(),
  });

  const mm::WakeReport report = loop.wake(hourly(2), mm::Budget());

  ASSERT_EQ(server.claimMaxes().size(), 1u) << "one claim per wake";
  EXPECT_EQ(server.claimMaxes()[0], 1) << "two transactions buy one job at the cheapest price there is";
  EXPECT_EQ(report.jobsClaimed, 1);
  EXPECT_EQ(report.jobsRun, 1);
  EXPECT_EQ(report.jobsNotRun, 0);
  EXPECT_EQ(report.jobs, mm::StepOutcome::Done);
  EXPECT_EQ(report.transactionsSpent, 1) << "a trace is one transaction and asks nobody for a session";
  ASSERT_EQ(server.results().size(), 1u);
  EXPECT_EQ(server.results()[0].jobId, 14001);
  EXPECT_EQ(server.queuedJobs(), 2u) << "the other two were never claimed";
  EXPECT_EQ(server.claimCalls(), 1);
}

TEST_F(LoopTest, AWakeWithNoRoomLeftMakesNoClaimAtAll) {
  // A cap below the cheapest job is a wake that reads the server and does not ask for work:
  // nothing is claimed, so nothing is leased and nothing is left silent.
  server.answerHeartbeat(60, 1, 3600, true, platform.clock().now());
  server.queue(fake::Job(14010, "trace").leasedUntil(kLease).json());

  const mm::WakeReport report = loop.wake(hourly(1), mm::Budget());

  EXPECT_EQ(report.claim, mm::StepOutcome::NoBudget);
  EXPECT_EQ(report.jobsClaimed, 0);
  EXPECT_EQ(server.claimCalls(), 0);
  EXPECT_EQ(server.queuedJobs(), 1u);
  EXPECT_TRUE(fake::LogCapture::the().holds("which is less than a job: no claim is made"));
}

TEST_F(LoopTest, AJobTheWakeHasNoTimeForIsFailedWithWakeBudgetAndItsResultIsPosted) {
  // A config read of the default list is thirty-three transactions, which at the conservative
  // budget of §8 is the better part of half an hour: a wake with a minute cannot finish it. It
  // is **not** put on the air and it is **not** left leased and silent - the result goes back
  // with *wake budget* in its log, which is what the server reads.
  server.answerHeartbeat(mm::kDefaultMaxTransactionsPerMinute, mm::kDefaultMinGapSeconds, 3600, true,
                         platform.clock().now());
  server.queue(fake::Job(14020, "config.read")
                   .atClass("admin")
                   .leasedUntil(kNow + 1800)
                   .withAdminPassword("adm1n-pass")
                   .withAclLevel(mm::kPermReadOnly)
                   .json());
  mm::Budget budget;
  budget.wakeSeconds = 60;

  const mm::WakeReport report = loop.wake(hourly(60), budget);

  EXPECT_EQ(report.jobsClaimed, 1);
  EXPECT_EQ(report.jobsRun, 0);
  EXPECT_EQ(report.jobsNotRun, 1);
  EXPECT_EQ(report.jobs, mm::StepOutcome::NoTime);
  EXPECT_EQ(report.transactionsSpent, 0) << "nothing went on the air for it";
  EXPECT_EQ(mesh.sends().size(), 0u) << "and the radio was never asked";
  ASSERT_EQ(server.results().size(), 1u) << "a job claimed is always answered";
  const std::string body = server.results()[0].body;
  EXPECT_EQ(server.results()[0].jobId, 14020);
  EXPECT_EQ(memberOf(body, "outcome"), "failed") << body;
  EXPECT_EQ(memberOf(body, "transactions"), "0") << body;
  EXPECT_NE(body.find("wake budget"), std::string::npos) << body;
  EXPECT_TRUE(fake::followsShapeOf(body, fake::contract("JobResultReport"))) << body;
  EXPECT_EQ(loop.counters().consecutiveFailures, 0u) << "the budget is not a failure of the server's";
}

TEST_F(LoopTest, AJobTheWakesTransactionCapHasNoRoomForIsFailedTheSameWay) {
  // The same rule, counted in transactions rather than seconds: the first job spends what the
  // wake had, and the second - claimed in the same breath - is answered without being sent.
  server.answerHeartbeat(60, 1, 3600, true, platform.clock().now());
  server.queue(std::vector<std::string>{
      fake::Job(14030, "status")
          .atClass("acl")
          .leasedUntil(kLease)
          .withAclLevel(mm::kPermReadOnly)
          .withPayload("{\"version\":false}")
          .groupedWith(14031)
          .json(),
      fake::Job(14031, "telemetry")
          .atClass("acl")
          .leasedUntil(kLease)
          .withAclLevel(mm::kPermReadOnly)
          .json(),
  });
  // Four transactions buy two jobs at the cheapest price, and the first spends three of them -
  // a login, the status request and the ACL restore - so the second will not fit.
  const mm::WakeReport report = loop.wake(hourly(4), mm::Budget());

  ASSERT_EQ(server.claimMaxes().size(), 1u);
  EXPECT_EQ(report.jobsClaimed, 2);
  EXPECT_EQ(report.jobsRun, 1);
  EXPECT_EQ(report.jobsNotRun, 1);
  EXPECT_EQ(report.jobs, mm::StepOutcome::NoBudget);
  ASSERT_EQ(server.results().size(), 2u) << "both jobs are answered, whatever happened to them";
  EXPECT_EQ(memberOf(server.results()[1].body, "outcome"), "failed");
  EXPECT_NE(server.results()[1].body.find("wake budget"), std::string::npos) << server.results()[1].body;
  EXPECT_LE(report.transactionsSpent, 4) << "the cap was not exceeded";
}

TEST_F(LoopTest, NoWakeEverLeavesAJobLeasedAndSilent) {
  // The property, over a spread of budgets, transaction caps, byte caps and claims: **every job a
  // claim handed over is answered** - posted in the wake that ran it, or kept in the offline
  // buffer and posted by a later one - and the results reach the server **in the order they were
  // made**, under any cap. Nothing is dropped and nothing is left leased and silent. The
  // randomness is seeded, so a failure is the same failure twice.
  //
  // Since task p1.6-21 the byte cap is part of it: every other round is metered, which also means
  // every other round starts with results in the buffer from the one before. `owed` is what has
  // been claimed and not yet posted, and the invariant that ties the two halves together is
  // `owed.size() == jobs.queueDepth()` - what is owed is exactly what the buffer holds.
  uint32_t seed = 0x1f35c6d1u;
  const auto next = [&seed]() {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
  };
  const char* const shapes[] = {"trace", "status", "telemetry", "config.read", "cli"};

  std::set<long long> owed;             // claimed, not yet posted: the buffer's contents
  std::vector<long long> postedInOrder; // every result the server was handed, in the order it came

  for (int round = 0; round < 24; round++) {
    server.forgetCalls();
    server.forgetQueued();
    const uint32_t lease = platform.clock().now() + 1800;
    std::vector<std::string> batch;
    std::vector<long long> queuedIds;
    const int howMany = 1 + static_cast<int>(next() % 3u);
    for (int i = 0; i < howMany; i++) {
      const char* type = shapes[next() % (sizeof shapes / sizeof shapes[0])];
      fake::Job job(20000 + round * 10 + i, type);
      job.leasedUntil(lease);
      if (std::strcmp(type, "trace") != 0) {
        job.atClass(std::strcmp(type, "config.read") == 0 || std::strcmp(type, "cli") == 0 ? "admin" : "acl")
            .withAclLevel(mm::kPermReadOnly)
            .withAdminPassword("adm1n-pass");
      }
      if (std::strcmp(type, "cli") == 0) job.withPayload("{\"command\":\"get name\"}");
      queuedIds.push_back(20000 + round * 10 + i);
      batch.push_back(job.json());
    }
    server.queue(batch);
    server.answerHeartbeat(mm::kDefaultMaxTransactionsPerMinute, mm::kDefaultMinGapSeconds, 3600, true,
                           platform.clock().now());

    mm::Budget budget;
    budget.wakeSeconds = 1 + next() % 400u;
    // Every other round is on a metered plan, and a cap of a few hundred to a few thousand bytes
    // is one result's worth or two: so some rounds post everything, some post what fits and keep
    // the rest, and the round after a metered one begins with a buffer to drain.
    const bool metered = round % 2 == 0;
    if (metered) budget.postBytes = 300 + next() % 3000u;
    const mm::WakeReport report = loop.wake(hourly(1 + static_cast<int>(next() % 8u)), budget);

    ASSERT_EQ(report.jobsRun + report.jobsNotRun, report.jobsClaimed)
        << "round " << round << ": a claimed job was neither run nor answered";
    // A claim takes the oldest of what is queued, in order, so the first `jobsClaimed` ids of
    // this round are the ones this wake is answerable for.
    ASSERT_LE(static_cast<size_t>(report.jobsClaimed), queuedIds.size());
    for (int i = 0; i < report.jobsClaimed; i++) owed.insert(queuedIds[static_cast<size_t>(i)]);
    for (const fake::PostedResult& posted : server.results()) {
      postedInOrder.push_back(posted.jobId);
      owed.erase(posted.jobId);
    }
    // What is owed is exactly what the buffer holds: nothing claimed is forgotten, and nothing
    // is waiting that was not claimed.
    EXPECT_EQ(owed.size(), static_cast<size_t>(jobs.queueDepth())) << "round " << round;
    EXPECT_EQ(jobs.resultsDropped(), 0u) << "round " << round << ": a result was lost";
    if (metered) {
      EXPECT_LE(report.bytesPosted, budget.postBytes) << "round " << round << ": the byte cap was passed";
    }
    // And whatever was left behind is still queued and bound to nothing, for the next wake.
    EXPECT_EQ(server.queuedJobs(), queuedIds.size() - static_cast<size_t>(report.jobsClaimed)) << "round " << round;
    // And the cap was honoured, which is the other half of the promise to the server.
    EXPECT_LE(report.transactionsSpent, 8 + 33) << "round " << round;
  }

  // One wake with no cap at the end: the buffer drains, and then **every** job any round claimed
  // has a posted result.
  server.forgetCalls();
  server.forgetQueued();
  server.answerHeartbeat(mm::kDefaultMaxTransactionsPerMinute, mm::kDefaultMinGapSeconds, 3600, false,
                         platform.clock().now());
  const mm::WakeReport drain = loop.wake(hourly(), mm::Budget());
  EXPECT_EQ(drain.resultsKept, 0);
  for (const fake::PostedResult& posted : server.results()) {
    postedInOrder.push_back(posted.jobId);
    owed.erase(posted.jobId);
  }
  EXPECT_TRUE(owed.empty()) << owed.size() << " claimed job(s) were never answered, the first being " << *owed.begin();
  EXPECT_EQ(jobs.queueDepth(), 0);
  EXPECT_EQ(jobs.resultsDropped(), 0u);
  // The order: ids were handed out in increasing order and a result is made as its job ends, so
  // the server saw them in the order they were made if and only if this list is increasing.
  ASSERT_FALSE(postedInOrder.empty());
  for (size_t i = 1; i < postedInOrder.size(); i++) {
    EXPECT_GT(postedInOrder[i], postedInOrder[i - 1])
        << "result " << i << " (job " << postedInOrder[i] << ") reached the server out of order";
  }
}

TEST_F(LoopTest, AWakeTakesTheServersTimeBeforeAnyResultIsStamped) {
  // The Walter sets the radio's clock before anything else and has the server's time from the
  // heartbeat: so a board with no clock of its own still stamps its results with a real time,
  // because the heartbeat comes before the first job in a wake as it does in a pass.
  platform.fakeClock().unset();
  ASSERT_FALSE(platform.clock().hasTime());
  server.answerHeartbeat(60, 1, 3600, true, kNow);
  server.setServerTime(kNow);
  server.queue(fake::Job(14040, "trace").leasedUntil(kNow + 1800).json());

  const mm::WakeReport report = loop.wake(hourly(), mm::Budget());

  EXPECT_EQ(report.serverTime, kNow);
  EXPECT_TRUE(platform.clock().hasTime()) << "the heartbeat's reply is where the clock came from";
  EXPECT_EQ(platform.fakeClock().serverTimesTaken(), 1);
  ASSERT_EQ(server.results().size(), 1u);
  uint32_t startedAt = 0;
  ASSERT_TRUE(mm::parseUtc(memberOf(server.results()[0].body, "startedAt").c_str(), startedAt))
      << server.results()[0].body;
  EXPECT_GE(startedAt, kNow) << "the result's timestamps are the server's and not 1970";
  EXPECT_LE(startedAt, kNow + 600);
}

TEST_F(LoopTest, AByteCapPostsAsManyBatchesAsItFitsAndTheRestWaitForTheNextWake) {
  server.answerHeartbeat(60, 1, 3600, false, platform.clock().now());

  // What one batch of twenty costs is learned rather than guessed.
  for (uint8_t i = 0; i < mm::kObservationBatchSize; i++) loop.ring().push(advert(i, platform.millis()));
  const mm::WakeReport measuring = loop.wake(hourly(), mm::Budget());
  ASSERT_EQ(measuring.batchesPosted, 1);
  ASSERT_EQ(server.observations().size(), 1u);
  const size_t oneBatch = server.observations()[0].size();
  EXPECT_EQ(measuring.bytesPosted, oneBatch);

  // Forty adverts and a cap that fits one batch and not a byte more: twenty go, twenty stay.
  server.forgetCalls();
  const uint32_t heardAt = platform.millis();
  const uint32_t heardAtEpoch = platform.clock().now();
  // The same names as the batch that was measured, so that the first twenty of these are that
  // batch to the byte: "Repeater 100" is a longer line than "Repeater 0" and would not be.
  for (uint8_t i = 0; i < 40; i++) loop.ring().push(advert(i, heardAt));
  mm::Budget budget;
  budget.postBytes = oneBatch + 1;

  const mm::WakeReport capped = loop.wake(hourly(), budget);

  EXPECT_EQ(capped.batchesPosted, 1) << "as many batches as the byte cap allows, and no more";
  EXPECT_EQ(capped.observationsPosted, static_cast<int>(mm::kObservationBatchSize));
  EXPECT_EQ(capped.observationsLeft, 20);
  EXPECT_EQ(capped.observations, mm::StepOutcome::NoBudget);
  EXPECT_LE(capped.bytesPosted, budget.postBytes);
  EXPECT_EQ(loop.ring().size(), 20u) << "the rest stay in the ring; nothing is dropped";
  EXPECT_EQ(loop.ring().dropped(), 0u);

  // An hour later, a wake with room posts them **with the moment they were heard** - the ring's
  // own rule, and what makes a late batch worth keeping at all.
  server.forgetCalls();
  platform.advanceSeconds(3600);
  const mm::WakeReport later = loop.wake(hourly(), mm::Budget());
  EXPECT_EQ(later.observationsPosted, 20);
  EXPECT_EQ(loop.ring().size(), 0u);
  ASSERT_EQ(server.observations().size(), 1u);
  const std::string items = memberOf(server.observations()[0], "items");
  uint32_t at = 0;
  ASSERT_TRUE(mm::parseUtc(memberOf(items, "at").c_str(), at)) << items;
  EXPECT_LE(at, heardAtEpoch + 2) << "an hour late, and still stamped with when it was heard";
  EXPECT_GE(at + 2, heardAtEpoch);
  EXPECT_GE(platform.clock().now() - at, 3500u);
}

// ---------------------------------------------------------------------------------------------
// A wake keeps its budget (task p1.6-21): every call it makes, and every byte it posts
//
// 18 gave a wake four bounds and two of them leaked at the job step: `JobRunner::post()` began
// with the **whole** offline buffer flushed under no cap, and none of the job step's posts was in
// `report.bytesPosted` or in the uplink's seconds. One claimed job could post a day of kept
// results on a plan priced by the byte. Now every call either object makes goes through one
// ledger (agent/UplinkMeter.h), and these cases are its arithmetic.

TEST_F(LoopTest, EveryCallOfAWakeIsInItsUplinkSecondsAndEveryPostedByteInItsBytes) {
  // The fake charges every call the same hundred milliseconds of the test's clock, so "the
  // uplink's seconds" is a multiplication - and the job's own waits on the radio, which are the
  // rest of the wake, are deliberately not in it.
  server.setCallMs(100);
  server.answerHeartbeat(60, 1, 3600, true, platform.clock().now());
  server.failResults(fake::FakeUplink::Fault::Unreachable, 1);
  server.queue(fake::Job(14100, "trace").leasedUntil(kLease).json());

  // The first wake of the boot: the config call, the heartbeat, the claim, and a result the
  // server would not take - four calls, and nothing of it arrived.
  const mm::WakeReport first = loop.wake(hourly(), mm::Budget());
  ASSERT_EQ(first.jobsRun, 1);
  ASSERT_EQ(jobs.queueDepth(), 1) << "the result the server refused";
  EXPECT_EQ(server.calls().size(), 4u) << "config, heartbeat, claim, result";
  EXPECT_EQ(first.uplinkMs, 100u * server.calls().size()) << "the config call is in the wake's seconds too";
  EXPECT_EQ(first.bytesPosted, 0u) << "the result did not arrive, so no byte of it was posted";

  // The second: the kept result, then the claim, the new result and the batch. Five calls of four
  // kinds, and the bytes are the bodies the fake was handed - from every step, which is the half
  // of this that leaked.
  server.forgetCalls();
  server.queue(fake::Job(14101, "trace").leasedUntil(platform.clock().now() + 1800).json());
  for (uint8_t i = 0; i < mm::kObservationBatchSize; i++) loop.ring().push(advert(i, platform.millis()));

  const mm::WakeReport second = loop.wake(hourly(), mm::Budget());

  ASSERT_EQ(server.calls().size(), 5u) << "heartbeat, result, claim, result, observations";
  EXPECT_EQ(server.calls()[1], "result") << "the buffer, before the claim";
  EXPECT_EQ(second.uplinkMs, 500u);
  EXPECT_EQ(second.resultsFlushed, 1);
  EXPECT_EQ(second.jobsRun, 1);
  EXPECT_EQ(second.batchesPosted, 1);
  size_t bodies = 0;
  for (const fake::PostedResult& posted : server.results()) bodies += posted.body.size();
  for (const std::string& batch : server.observations()) bodies += batch.size();
  ASSERT_EQ(server.results().size(), 2u) << "the kept one and this wake's own";
  EXPECT_EQ(second.bytesPosted, bodies) << "every byte of body, from every step";
  EXPECT_GT(second.bytesPosted, 0u);
  EXPECT_GT(second.elapsedMs, second.uplinkMs) << "the job's own waits are not the server's seconds";
}

TEST_F(LoopTest, AKeptResultGoesFirstAndANewOneWaitsBehindItUnderACapThatFitsOne) {
  // A cap that fits one of the two: the **older** result goes and the new one is kept behind it.
  // Posting the new one because it happened to fit would put the server's view of this exit node
  // out of order, which is the one thing the offline buffer exists to prevent (EXIT-NODES §5).
  server.answerHeartbeat(60, 1, 3600, true, platform.clock().now());
  server.failResults(fake::FakeUplink::Fault::Unreachable, 1);
  server.queue(fake::Job(14110, "trace").leasedUntil(kLease).json());

  ASSERT_EQ(loop.wake(hourly(), mm::Budget()).jobsRun, 1);
  ASSERT_EQ(jobs.queueDepth(), 1);
  // What the kept result weighs, exactly: the buffer's own figure, not an estimate.
  const size_t kept = jobs.results().bytes();
  ASSERT_GT(kept, 0u);

  server.forgetCalls();
  server.queue(fake::Job(14111, "trace").leasedUntil(platform.clock().now() + 1800).json());
  mm::Budget budget;
  budget.postBytes = kept;  // the one that waited, and not a byte more

  const mm::WakeReport report = loop.wake(hourly(), budget);

  EXPECT_EQ(report.results, mm::StepOutcome::Done);
  EXPECT_EQ(report.resultsFlushed, 1);
  EXPECT_EQ(report.jobsRun, 1) << "the job still ran: the cap is on what is posted";
  EXPECT_EQ(report.resultsKept, 1) << "and its result did not fit: kept: budget";
  EXPECT_EQ(report.bytesPosted, kept) << "to the byte, and not one past the cap";
  ASSERT_EQ(server.results().size(), 1u);
  EXPECT_EQ(server.results()[0].jobId, 14110) << "the older result went first";
  EXPECT_EQ(jobs.queueDepth(), 1) << "and the new one is in the buffer, behind nothing";
  EXPECT_EQ(jobs.postFailures(), 1u) << "a byte cap of ours is not a failure of the server's";
  EXPECT_EQ(report.counters.consecutiveFailures, 0u) << "and nothing backs off for it";
  EXPECT_TRUE(fake::LogCapture::the().holds("kept: budget"));

  // The next wake, with room, posts it - with the timestamps it was made with.
  server.forgetCalls();
  platform.advanceSeconds(3600);
  const mm::WakeReport after = loop.wake(hourly(), mm::Budget());
  EXPECT_EQ(after.resultsFlushed, 1);
  ASSERT_EQ(server.results().size(), 1u);
  EXPECT_EQ(server.results()[0].jobId, 14111);
  EXPECT_EQ(jobs.queueDepth(), 0);
  uint32_t finishedAt = 0;
  ASSERT_TRUE(mm::parseUtc(memberOf(server.results()[0].body, "finishedAt").c_str(), finishedAt));
  EXPECT_GE(platform.clock().now() - finishedAt, 3500u) << "an hour late, with its own timestamps";
}

TEST_F(LoopTest, AnUplinkBudgetSpentAtTheJobStepKeepsTheResultRatherThanPostingPastIt) {
  // The other bound, at the same place: a second of uplink per call and three seconds to spend,
  // which the config call, the heartbeat and the claim use up. The job runs - its airtime is not
  // the uplink's - and its result is kept rather than posted outside the budget.
  server.setCallMs(1000);
  server.answerHeartbeat(60, 1, 3600, true, platform.clock().now());
  server.queue(fake::Job(14120, "trace").leasedUntil(kLease).json());
  mm::Budget budget;
  budget.uplinkSeconds = 3;

  const mm::WakeReport report = loop.wake(hourly(), budget);

  EXPECT_EQ(report.claim, mm::StepOutcome::Done);
  EXPECT_EQ(report.jobsRun, 1);
  EXPECT_EQ(report.resultsKept, 1);
  EXPECT_EQ(report.uplinkMs, 3000u) << "three calls of a second, and the fourth was not made";
  EXPECT_EQ(report.bytesPosted, 0u);
  EXPECT_TRUE(server.results().empty()) << "the wake's seconds at the server were spent";
  EXPECT_EQ(jobs.queueDepth(), 1);
  EXPECT_EQ(jobs.postFailures(), 0u) << "nothing failed: the wake stopped posting";
  EXPECT_GT(report.elapsedMs, report.uplinkMs);

  // And the next wake posts it, which is what makes keeping it an answer and not a loss.
  server.forgetCalls();
  const mm::WakeReport next = loop.wake(hourly(), mm::Budget());
  EXPECT_EQ(next.resultsFlushed, 1);
  ASSERT_EQ(server.results().size(), 1u);
  EXPECT_EQ(server.results()[0].jobId, 14120);
}

TEST_F(LoopTest, AWakeWhoseCapNoPostCanFitPostsNothingAndKeepsEverything) {
  // A cap of nothing, spelled one byte: `Budget::postBytes` of 0 is "no bound", as every bound in
  // a Budget is, and no result and no batch this firmware writes is one byte long. The heartbeat
  // still goes - the byte cap is on what the library's own steps post, and a wake that could not
  // say hello would be a wake that learns nothing - and everything else waits.
  server.answerHeartbeat(60, 1, 3600, true, platform.clock().now());
  server.failResults(fake::FakeUplink::Fault::Unreachable, 1);
  server.queue(fake::Job(14130, "trace").leasedUntil(kLease).json());
  ASSERT_EQ(loop.wake(hourly(), mm::Budget()).jobsRun, 1);
  ASSERT_EQ(jobs.queueDepth(), 1);

  server.forgetCalls();
  server.queue(fake::Job(14131, "trace").leasedUntil(platform.clock().now() + 1800).json());
  for (uint8_t i = 0; i < mm::kObservationBatchSize; i++) loop.ring().push(advert(i, platform.millis()));
  mm::Budget budget;
  budget.postBytes = 1;

  const mm::WakeReport report = loop.wake(hourly(), budget);

  EXPECT_EQ(report.heartbeat, mm::StepOutcome::Done) << "the heartbeat is not what a byte cap stops";
  EXPECT_EQ(report.results, mm::StepOutcome::NoBudget);
  EXPECT_EQ(report.resultsFlushed, 0);
  EXPECT_EQ(report.jobsRun, 1);
  EXPECT_EQ(report.resultsKept, 1);
  EXPECT_EQ(report.observations, mm::StepOutcome::NoBudget);
  EXPECT_EQ(report.batchesPosted, 0);
  EXPECT_EQ(report.bytesPosted, 0u);
  EXPECT_TRUE(server.results().empty());
  EXPECT_TRUE(server.observations().empty());
  EXPECT_EQ(jobs.queueDepth(), 2) << "both results wait, in the order they were made";
  EXPECT_EQ(jobs.resultsDropped(), 0u);
  EXPECT_EQ(loop.ring().size(), mm::kObservationBatchSize) << "and every advert is still in the ring";
  EXPECT_EQ(loop.ring().dropped(), 0u);
  EXPECT_EQ(report.observationsLeft, static_cast<int>(mm::kObservationBatchSize));

  // Nothing of it was lost: a wake with room posts the two results, oldest first, and the batch.
  server.forgetCalls();
  const mm::WakeReport after = loop.wake(hourly(), mm::Budget());
  EXPECT_EQ(after.resultsFlushed, 2);
  ASSERT_EQ(server.results().size(), 2u);
  EXPECT_EQ(server.results()[0].jobId, 14130);
  EXPECT_EQ(server.results()[1].jobId, 14131);
  EXPECT_EQ(after.batchesPosted, 1);
  EXPECT_EQ(loop.ring().size(), 0u);
}

TEST_F(LoopTest, AServerThatIsAwayIsABackoffTheCallerSleepsThroughAndTheNextWakeHonoursIt) {
  server.failAlways(fake::FakeUplink::Fault::Unreachable);
  server.setRetryAfter(0);

  const mm::WakeReport away = loop.wake(hourly(), mm::Budget());

  EXPECT_EQ(away.config, mm::StepOutcome::Failed);
  EXPECT_EQ(away.heartbeat, mm::StepOutcome::Failed);
  EXPECT_EQ(away.claim, mm::StepOutcome::Skipped) << "nothing else goes on a link that just failed";
  EXPECT_EQ(away.jobs, mm::StepOutcome::Skipped);
  EXPECT_EQ(away.observations, mm::StepOutcome::Skipped);
  EXPECT_GE(away.sleepAtLeastS, 22u) << "half a minute with a quarter of jitter (agent/Backoff.h)";
  EXPECT_LE(away.sleepAtLeastS, 90u);
  EXPECT_GE(away.counters.consecutiveFailures, 1u);
  EXPECT_FALSE(away.rebootWanted);

  // The state goes to retained storage as bytes, and comes back into a loop that has just booted.
  const mm::WakeState saved = loop.state();
  EXPECT_GT(saved.holdForMs, 0u);
  unsigned char bytes[sizeof(mm::WakeState)];
  std::memcpy(bytes, &saved, sizeof bytes);
  mm::WakeState back;
  std::memcpy(&back, bytes, sizeof bytes);

  std::unique_ptr<mm::JobRunner> runner(new mm::JobRunner());
  std::unique_ptr<mm::AgentLoop> woken(new mm::AgentLoop());
  runner->begin(mesh, server, platform, nullptr);
  woken->begin(server, platform, mesh, *runner, identity);
  woken->setIdentity(self().c_str());
  ASSERT_TRUE(woken->restore(back));
  EXPECT_EQ(woken->counters().consecutiveFailures, saved.consecutiveFailures);
  EXPECT_EQ(woken->counters().httpFailures, saved.counters.httpFailures);
  EXPECT_GE(woken->retrySeconds() + 1, saved.holdForMs / 1000);

  // A sleep shorter than the wait: the wake that follows sends nothing and says so.
  server.forgetCalls();
  platform.advance(saved.holdForMs / 2);
  const mm::WakeReport tooSoon = woken->wake(hourly(), mm::Budget());
  EXPECT_TRUE(server.calls().empty()) << "the wait the server asked for was not honoured";
  EXPECT_STREQ(tooSoon.why, "a wait the server asked for is still running");
  EXPECT_GT(tooSoon.sleepAtLeastS, 0u);

  // And once it is over, the backoff is where it was: the second failure is the second step and
  // not the first, because the state carried it across the sleep.
  platform.advance(saved.holdForMs);
  const mm::WakeReport again = woken->wake(hourly(), mm::Budget());
  EXPECT_FALSE(server.calls().empty());
  EXPECT_GE(again.counters.consecutiveFailures, saved.consecutiveFailures + 1);
  EXPECT_GE(again.sleepAtLeastS, 45u) << "a minute at the second step, jittered - not thirty again";
}

TEST_F(LoopTest, TheWakeStateRoundTripsThroughBytesAndARestoreOfAnotherVersionIsRefused) {
  server.answerHeartbeat(4, 10, 1800, false, platform.clock().now());
  ASSERT_EQ(loop.wake(hourly(), mm::Budget()).heartbeat, mm::StepOutcome::Done);

  const mm::WakeState saved = loop.state();
  EXPECT_EQ(saved.version, mm::WakeState::kVersion);
  EXPECT_EQ(saved.pollIntervalS, 1800) << "the interval the server accepted";
  EXPECT_EQ(saved.budget.maxTransactionsPerMinute, 4) << "and the budget it named";
  EXPECT_EQ(saved.budget.minGapSeconds, 10);
  EXPECT_TRUE(saved.everHeartbeated);
  EXPECT_EQ(saved.lastHeartbeatAtEpoch, kNow);
  EXPECT_EQ(saved.counters.heartbeats, 1u);
  EXPECT_FALSE(saved.halted);

  std::unique_ptr<mm::JobRunner> runner(new mm::JobRunner());
  std::unique_ptr<mm::AgentLoop> woken(new mm::AgentLoop());
  runner->begin(mesh, server, platform, nullptr);
  woken->begin(server, platform, mesh, *runner, identity);
  ASSERT_TRUE(woken->restore(saved));

  const mm::WakeState again = woken->state();
  EXPECT_EQ(again.version, saved.version);
  EXPECT_EQ(again.backoffMs, saved.backoffMs);
  EXPECT_EQ(again.consecutiveFailures, saved.consecutiveFailures);
  EXPECT_EQ(again.holdForMs, saved.holdForMs);
  EXPECT_EQ(again.failingForMs, saved.failingForMs);
  EXPECT_EQ(again.pollIntervalS, saved.pollIntervalS);
  EXPECT_EQ(again.budget.maxTransactionsPerMinute, saved.budget.maxTransactionsPerMinute);
  EXPECT_EQ(again.budget.minGapSeconds, saved.budget.minGapSeconds);
  EXPECT_EQ(again.halted, saved.halted);
  EXPECT_EQ(again.everHeartbeated, saved.everHeartbeated);
  EXPECT_EQ(again.lastHeartbeatAtEpoch, saved.lastHeartbeatAtEpoch);
  EXPECT_EQ(again.counters.heartbeats, saved.counters.heartbeats);
  EXPECT_EQ(again.counters.httpFailures, saved.counters.httpFailures);

  // A blob written by another build is not read into this one.
  mm::WakeState older = saved;
  older.version = mm::WakeState::kVersion + 1;
  EXPECT_FALSE(woken->restore(older));
  EXPECT_EQ(woken->state().pollIntervalS, saved.pollIntervalS) << "and nothing of it was taken";
}

TEST_F(LoopTest, AHaltedLoopWakesAndSendsNothing) {
  server.failAlways(fake::FakeUplink::Fault::Unauthorized);
  const mm::WakeReport refused = loop.wake(hourly(), mm::Budget());
  EXPECT_TRUE(refused.halted);
  EXPECT_TRUE(loop.halted());

  // And a 401 survives the sleep: nothing is sent until a person sets the token again.
  const mm::WakeState saved = loop.state();
  EXPECT_TRUE(saved.halted);
  server.answerAgain();
  server.forgetCalls();
  const mm::WakeReport next = loop.wake(hourly(), mm::Budget());
  EXPECT_TRUE(server.calls().empty());
  EXPECT_STREQ(next.why, "halted: the server refused the token (http 401)");
  EXPECT_TRUE(next.halted);
}

TEST_F(LoopTest, TheRebootGuardIsToldAndNotTakenInOneWakeMode) {
  // Twelve unanswered calls over ninety minutes is what makes the board restart itself in
  // `loop()`. A device that sleeps has a supervisor of its own, so the wake **says** the
  // platform would have been asked and nothing restarts.
  server.failAlways(fake::FakeUplink::Fault::Unreachable);
  server.setRetryAfter(0);
  bool wanted = false;
  std::string why;
  for (int wake = 0; wake < 12 && !wanted; wake++) {
    platform.advanceSeconds(600);
    const mm::WakeReport report = loop.wake(hourly(), mm::Budget());
    wanted = report.rebootWanted;
    why = report.rebootWhy;
  }
  EXPECT_TRUE(wanted) << loop.counters().consecutiveFailures << " failure(s) and the guard said nothing";
  EXPECT_NE(why.find("server calls unanswered"), std::string::npos) << why;
  EXPECT_TRUE(platform.reboots().empty()) << "a wake does not restart the device";
  EXPECT_EQ(platform.selfReboots(), 0u);
  EXPECT_TRUE(fake::LogCapture::the().holds("in one-wake mode it is not"));
}

TEST_F(LoopTest, AWakeFlushesWhatTheLastWakeCouldNotPostBeforeItClaimsAnything) {
  // "Offline is normal", in a wake: the result of a job whose POST the server would not take
  // waits in RAM, and the next wake posts it **before** it asks for more work - which is the
  // order of EXIT-NODES §5 and the order of `loop()`.
  server.answerHeartbeat(60, 1, 3600, true, platform.clock().now());
  server.failResults(fake::FakeUplink::Fault::Unreachable, 1);
  server.queue(fake::Job(14050, "trace").leasedUntil(kLease).json());

  ASSERT_EQ(loop.wake(hourly(), mm::Budget()).jobsRun, 1);
  ASSERT_EQ(jobs.queueDepth(), 1) << "the result the server would not take";

  server.forgetCalls();
  platform.advanceSeconds(3600);
  server.queue(fake::Job(14051, "trace").leasedUntil(platform.clock().now() + 1800).json());
  const mm::WakeReport second = loop.wake(hourly(), mm::Budget());

  EXPECT_EQ(second.results, mm::StepOutcome::Done);
  EXPECT_EQ(second.resultsFlushed, 1);
  ASSERT_GE(server.calls().size(), 3u);
  EXPECT_EQ(server.calls()[0], "heartbeat");
  EXPECT_EQ(server.calls()[1], "result") << "the buffer, before the claim";
  EXPECT_EQ(server.calls()[2], "claim");
  EXPECT_EQ(jobs.queueDepth(), 0);
  // And the late result still carries the timestamps it was made with.
  ASSERT_GE(server.results().size(), 1u);
  uint32_t finishedAt = 0;
  ASSERT_TRUE(mm::parseUtc(memberOf(server.results()[0].body, "finishedAt").c_str(), finishedAt));
  EXPECT_GE(platform.clock().now() - finishedAt, 3500u);
}

TEST_F(LoopTest, AWakeWithNoUplinkOrNoTokenSendsNothingAndSaysWhich) {
  server.setLinkUp(false);
  const mm::WakeReport down = loop.wake(hourly(), mm::Budget());
  EXPECT_TRUE(server.calls().empty());
  EXPECT_STREQ(down.why, "the uplink is down");

  server.setLinkUp(true);
  server.setConfigured(false);
  const mm::WakeReport noToken = loop.wake(hourly(), mm::Budget());
  EXPECT_TRUE(server.calls().empty());
  EXPECT_STREQ(noToken.why, "not configured: the server's url and a token are needed");
}

TEST_F(LoopTest, TheAirtimeGateStillPacesAWakeWithRoomForSixTransactions) {
  // Pacing is untouched: a cap of six transactions is still spaced by the budget the server
  // named, because the gate is where every send goes through (agent/Airtime.h).
  server.answerHeartbeat(2, 20, 3600, true, platform.clock().now());
  server.queue(fake::Job(14060, "status")
                   .atClass("acl")
                   .leasedUntil(kNow + 1800)
                   .withAclLevel(mm::kPermReadOnly)
                   .json());

  const uint32_t began = platform.millis();
  const mm::WakeReport report = loop.wake(hourly(6), mm::Budget());

  ASSERT_EQ(report.jobsRun, 1) << "the job did not run at all";
  EXPECT_GE(report.transactionsSpent, 2);
  // Every gap between two sends is the budget's twenty seconds, on the fake clock.
  const std::vector<fake::Send>& sends = mesh.sends();
  ASSERT_GE(sends.size(), 2u);
  for (size_t i = 1; i < sends.size(); i++) {
    EXPECT_GE(sends[i].atMs - sends[i - 1].atMs, 20000u) << "send " << i << " came too soon after " << (i - 1);
  }
  EXPECT_GE(platform.millis() - began, 20000u) << "a paced wake takes the time the pacing costs";
}

// ---------------------------------------------------------------------------------------------
// The rule the fakes cannot reach: there is one place a call to the server is made from
//
// Every case above holds what a wake spends. These two hold that it *cannot* spend anything
// uncounted: the library's own text, read for the seven calls `IUplink` has and for where they
// are made. A call added to a step tomorrow and counted nowhere is a red build here, which is
// what "through one place" has to mean to be worth saying.

// The calls of IUplink: its virtual methods that are not `const`. The seven of API §3 cost a
// request each; the accessors beside them (`configured`, `link`, `lastCall`, ...) are reads of
// what the last one left and cost nothing. The list comes out of the header rather than being
// typed here, so a call added to the interface is a call this test looks for.
std::vector<std::string> uplinkCalls() {
  const std::string header = readFile(fs::path(PROJECT_DIR) / "lib/agent-core/include/agent/IUplink.h");
  std::vector<std::string> names;
  size_t at = 0;
  while ((at = header.find("virtual ", at)) != std::string::npos) {
    const size_t eol = header.find('\n', at);
    const size_t open = header.find('(', at);
    at += 8;
    if (open == std::string::npos || eol == std::string::npos || open > eol) continue;
    const size_t close = header.find(')', open);
    if (close == std::string::npos) continue;
    if (header.compare(close, 7, ") const") == 0) continue;  // an accessor: no request, no time
    size_t start = open;
    while (start > 0) {
      const char c = header[start - 1];
      if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '~')) break;
      start--;
    }
    const std::string name = header.substr(start, open - start);
    if (name.empty() || name[0] == '~') continue;
    names.push_back(name);
  }
  return names;
}

TEST(TheWakesLedger, EveryCallToTheServerInTheLibraryIsMadeThroughIt) {
  const std::vector<std::string> calls = uplinkCalls();
  ASSERT_GE(calls.size(), 7u) << "IUplink's calls could not be read out of the header";
  for (const char* expected : {"fetchConfig", "postHeartbeat", "postObservations", "claimJobs", "postJobResult",
                               "extendLease", "postProgress"}) {
    EXPECT_NE(std::find(calls.begin(), calls.end(), std::string(expected)), calls.end())
        << expected << " is not in the list this test checks";
  }

  int found = 0;
  for (const char* file : {"lib/agent-core/src/AgentLoop.cpp", "lib/agent-core/src/JobRunner.cpp"}) {
    const std::string text = readFile(fs::path(PROJECT_DIR) / file);
    ASSERT_FALSE(text.empty()) << file;
    size_t from = 0;
    int line = 1;
    while (from < text.size()) {
      size_t eol = text.find('\n', from);
      if (eol == std::string::npos) eol = text.size();
      const std::string one = text.substr(from, eol - from);
      for (const std::string& call : calls) {
        if (one.find("server_->" + call + "(") == std::string::npos) continue;
        found++;
        EXPECT_NE(one.find("spend("), std::string::npos)
            << file << ":" << line << " calls the server outside the wake's ledger:" << one;
      }
      from = eol + 1;
      line++;
    }
  }
  EXPECT_GE(found, 5) << "no call site was found at all: this test has stopped checking anything";
}

TEST(TheWakesLedger, TheRunnerHasNoUnboundedFlushAndAsksBeforeEveryPost) {
  const std::string runner = readFile(fs::path(PROJECT_DIR) / "lib/agent-core/src/JobRunner.cpp");
  ASSERT_FALSE(runner.empty());

  // One flush, and it asks the budget about every body before it goes. The pair of overloads 18
  // left - one bounded, one not - is what let the job step flush a day of results under no cap.
  EXPECT_EQ(runner.find("flushResults(size_t"), std::string::npos) << "the unbounded flush is back";
  const size_t flush = runner.find("int JobRunner::flushResults() {");
  ASSERT_NE(flush, std::string::npos);
  const size_t flushEnd = runner.find("\n}", flush);
  ASSERT_NE(flushEnd, std::string::npos);
  const std::string flushBody = runner.substr(flush, flushEnd - flush);
  EXPECT_NE(flushBody.find("if (!mayPost(len))"), std::string::npos) << "the flush does not ask the budget";

  // And `post()` - which every job's result goes through - flushes the queue first, asks before
  // its own body goes out, and keeps it when the answer is no.
  const size_t post = runner.find("bool JobRunner::post(long long jobId, const char* body, size_t len) {");
  ASSERT_NE(post, std::string::npos);
  const size_t postEnd = runner.find("\n}", post);
  ASSERT_NE(postEnd, std::string::npos);
  const std::string postBody = runner.substr(post, postEnd - post);
  const size_t queueFirst = postBody.find("flushResults();");
  const size_t asks = postBody.find("mayPost(len)");
  const size_t sends = postBody.find("server_->postJobResult(");
  const size_t keeps = postBody.find("results_.push(");
  ASSERT_NE(queueFirst, std::string::npos) << "post() no longer posts the queue ahead of this result first";
  ASSERT_NE(asks, std::string::npos) << "post() does not ask the budget";
  ASSERT_NE(sends, std::string::npos);
  ASSERT_NE(keeps, std::string::npos);
  EXPECT_LT(queueFirst, sends) << "the queue goes first, or the server sees results out of order";
  EXPECT_LT(asks, sends) << "the budget is asked before the body goes out";
  EXPECT_NE(postBody.find("kept: budget"), std::string::npos) << "and a result the budget kept says so";
}

}  // namespace

// PlatformIO's googletest integration links no gtest_main, so every test file brings its own.
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
