// The two calls the protocol has for a job that runs past its lease (API §3), on the uplink seam
// since task p1.5-15: POST /agent/v1/jobs/{id}/extend and POST /agent/v1/jobs/{id}/progress. The
// bodies this firmware would send and the replies it reads are held against MeshMaster.Contracts'
// fixtures byte for byte, as test_claim holds the claim and the result; and AgentClient.cpp makes
// the two calls in the same shape as the five before them. Nothing on the Heltec calls them yet.

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <agent/JobEnvelopes.h>

#ifndef FIXTURES_DIR
#error "FIXTURES_DIR must be defined; platformio.ini's [env:native] does it"
#endif
#ifndef PROJECT_DIR
#error "PROJECT_DIR must be defined; platformio.ini's [env:native] does it"
#endif

namespace {

namespace fs = std::filesystem;

std::string readAll(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  EXPECT_TRUE(in.good()) << "no such file: " << p;
  std::ostringstream all;
  all << in.rdbuf();
  return all.str();
}

std::string fixture(const char* name) {
  std::string text = readAll(fs::path(FIXTURES_DIR) / (std::string(name) + ".json"));
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
  return text;
}

}  // namespace

// ---- the bodies, against the .NET bytes ---------------------------------------------------------

TEST(Extend, TheRequestIsTheFixtureByteForByte) {
  char out[32];
  const size_t n = mm::writeExtendRequest(120, out, sizeof out);
  ASSERT_GT(n, 0u);
  EXPECT_EQ(fixture("ExtendRequest"), std::string(out, n));
}

TEST(Extend, TheReplyParsesToTheIdAndTheNewLease) {
  const std::string json = fixture("ExtendReply");
  mm::ExtendReply reply;
  char err[64] = {0};
  ASSERT_TRUE(mm::parseExtendReply(json.c_str(), json.size(), reply, err, sizeof err)) << err;
  EXPECT_EQ(12345LL, reply.jobId);
  EXPECT_STREQ("2026-09-18T10:04:03Z", reply.leaseUntil);
  EXPECT_EQ(1789725843u, reply.leaseUntilUnix);
}

TEST(Progress, TheReportIsTheFixtureByteForByte) {
  char out[256];
  const size_t n = mm::writeProgressReport("{\"neighbours\":[{\"prefix\":\"a1\",\"lastHeardS\":42,\"snr\":7.25}]}",
                                           "step 3 of 12", out, sizeof out);
  ASSERT_GT(n, 0u);
  EXPECT_EQ(fixture("ProgressReport"), std::string(out, n));
}

TEST(Progress, AReportMayCarryEitherFieldOrNeither) {
  char out[64];
  size_t n = mm::writeProgressReport(nullptr, "half way", out, sizeof out);
  EXPECT_EQ(std::string("{\"note\":\"half way\"}"), std::string(out, n));
  n = mm::writeProgressReport("{\"step\":3}", nullptr, out, sizeof out);
  EXPECT_EQ(std::string("{\"data\":{\"step\":3}}"), std::string(out, n));
  n = mm::writeProgressReport(nullptr, nullptr, out, sizeof out);
  EXPECT_EQ(std::string("{}"), std::string(out, n));
  // Too small a buffer is 0, never a cut body.
  char tiny[8];
  EXPECT_EQ(0u, mm::writeProgressReport(nullptr, "a note that does not fit", tiny, sizeof tiny));
}

TEST(Progress, TheReplyParsesToTheIdAndWhenItWasRecorded) {
  const std::string json = fixture("ProgressReply");
  mm::ProgressReply reply;
  char err[64] = {0};
  ASSERT_TRUE(mm::parseProgressReply(json.c_str(), json.size(), reply, err, sizeof err)) << err;
  EXPECT_EQ(12345LL, reply.jobId);
  EXPECT_STREQ("2026-09-18T10:03:00Z", reply.recordedAt);
  EXPECT_EQ(1789725780u, reply.recordedAtUnix);
}

TEST(Progress, AReplyThatIsNotTheShapeIsRefusedWithAReason) {
  mm::ExtendReply extend;
  mm::ProgressReply progress;
  char err[64] = {0};
  const char* noId = "{\"leaseUntil\":\"2026-09-18T10:04:03Z\"}";
  EXPECT_FALSE(mm::parseExtendReply(noId, std::strlen(noId), extend, err, sizeof err));
  EXPECT_NE(std::string(err).find("jobId"), std::string::npos) << err;
  const char* badWhen = "{\"jobId\":1,\"recordedAt\":\"yesterday\"}";
  EXPECT_FALSE(mm::parseProgressReply(badWhen, std::strlen(badWhen), progress, err, sizeof err));
  EXPECT_NE(std::string(err).find("recordedAt"), std::string::npos) << err;
  const char* notJson = "<html>";
  EXPECT_FALSE(mm::parseExtendReply(notJson, std::strlen(notJson), extend, err, sizeof err));
  EXPECT_NE(std::string(err).find("not JSON"), std::string::npos) << err;
}

// ---- the uplink's shape -------------------------------------------------------------------------

// The two calls are on the seam, and the WiFi client makes them as it makes the result: the same
// per-job POST, the reply parsed, the failure logged with the job's id and never the body as text.
TEST(Uplink, TheWifiClientMakesTheTwoCallsInTheShapeOfTheFive) {
  const std::string iface = readAll(fs::path(PROJECT_DIR) / "lib/agent-core/include/agent/IUplink.h");
  EXPECT_NE(iface.find("virtual bool extendLease(long long jobId, int seconds, uint32_t& leaseUntil) = 0;"),
            std::string::npos);
  EXPECT_NE(iface.find("virtual bool postProgress(long long jobId, const char* body, size_t len) = 0;"),
            std::string::npos);

  const std::string client = readAll(fs::path(PROJECT_DIR) / "src/agent/AgentClient.cpp");
  EXPECT_NE(client.find("\"/agent/v1/jobs/%lld/%s\""), std::string::npos) << "one per-job POST for the three";
  const size_t result = client.find("postToJob(jobId, \"result\"");
  const size_t extend = client.find("postToJob(jobId, \"extend\"");
  const size_t progress = client.find("postToJob(jobId, \"progress\"");
  EXPECT_NE(result, std::string::npos);
  EXPECT_NE(extend, std::string::npos);
  EXPECT_NE(progress, std::string::npos);
  EXPECT_NE(client.find("writeExtendRequest(seconds, request, sizeof request)"), std::string::npos);
  EXPECT_NE(client.find("parseExtendReply(g_jobReply, bodyLen, r, err, sizeof err)"), std::string::npos);
  EXPECT_NE(client.find("parseProgressReply(g_jobReply, bodyLen, r, err, sizeof err)"), std::string::npos);

  // Nothing on the Heltec calls them: the runner and the loop name neither.
  const std::string runner = readAll(fs::path(PROJECT_DIR) / "lib/agent-core/src/JobRunner.cpp");
  const std::string loop = readAll(fs::path(PROJECT_DIR) / "lib/agent-core/src/AgentLoop.cpp");
  EXPECT_EQ(runner.find("extendLease("), std::string::npos);
  EXPECT_EQ(runner.find("postProgress("), std::string::npos);
  EXPECT_EQ(loop.find("extendLease("), std::string::npos);
  EXPECT_EQ(loop.find("postProgress("), std::string::npos);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
