// The flood scope, on a host (task p1.8-32; meshmaster.dk's D-47 and its task p4.20-151).
//
// Four things are held here, and the first two are the ones a wrong answer would put on the air:
//
//   the arithmetic   `src/mesh/TransportScope.cpp` is MeshCore's `getAutoKeyFor` and
//                    `TransportKey::calcTransportCode` written out, so this suite checks it
//                    against the crypto rather than against a second copy of itself: SHA-256
//                    against the FIPS 180-4 vector for "abc", HMAC-SHA256 against RFC 4231's
//                    test case 2, the key of `dk` against the library's own hash of `#dk`, and
//                    the transport code against an HMAC built here out of the library's **plain**
//                    SHA-256 (the two padded blocks, by hand) - which is an independent
//                    derivation and not the same six lines twice. The pinned source itself is
//                    read at the end, so a bump that moves either function fails here
//   the name rule    what `set flood.scope` takes: `RegionMap::is_name_char`, thirty characters,
//                    `*`, a leading `#` dropped, and never a private region
//   the policy       `agent/FloodScope.h`: the first flood of a transaction scoped, the second
//                    unscoped while `flood.unscoped` is `retry`, and one of the three counts per
//                    flooded transaction
//   the two together the job runner over the fake repeater (support/FakeMesh.h), which is where
//                    "try 1 scoped, try 2 unscoped, and the counts moved" is a run and not a
//                    unit test: a repeater that drops `dk` floods answers the unscoped try, and
//                    the counts and the job's own log say so
//
// MeshCore's `TransportKeyStore.cpp` cannot be compiled here - its header pulls in `Arduino.h`,
// `Packet.h` and `IdentityStore.h`, and the last of those does not even define `FILESYSTEM` off a
// board - so the hashing is the **same library** MeshCore hashes with (rweather/Crypto at the
// version `[libs]` pins, three of its files compiled into this build by
// `tools/pio/meshcore_private_libs.py`) and the rest is this suite.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <SHA256.h>

#include <agent/FloodScope.h>
#include <agent/JobRunner.h>

#include <support/FakeMesh.h>
#include <support/FakePlatform.h>
#include <support/FakeUplink.h>
#include <support/Jobs.h>

#include "mesh/TransportScope.h"

#ifndef PROJECT_DIR
#error "PROJECT_DIR must be defined; platformio.ini's [env:native] does it"
#endif

namespace {

namespace fs = std::filesystem;

constexpr uint32_t kNow = 1790000000;
constexpr uint32_t kLease = kNow + 1800;

std::string hex(const uint8_t* bytes, size_t len) {
  static const char* digits = "0123456789abcdef";
  std::string out;
  for (size_t i = 0; i < len; i++) {
    out.push_back(digits[bytes[i] >> 4]);
    out.push_back(digits[bytes[i] & 0x0f]);
  }
  return out;
}

std::string readWhole(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::ostringstream all;
  all << in.rdbuf();
  return all.str();
}

// SHA-256 of a byte range, with the library's plain hash and nothing else.
void sha256(const void* data, size_t len, uint8_t out[32]) {
  SHA256 sha;
  sha.update(data, len);
  sha.finalize(out, 32);
}

// HMAC-SHA256 built here from the plain hash: K zero-padded to the 64-byte block, exclusive-ored
// with 0x36 and 0x5c, and two passes - RFC 2104's own description. This is the independent
// derivation the transport-code test holds `transportCodeFor` against, so that the two cannot be
// wrong in the same way.
void hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* data, size_t dataLen, uint8_t out[32]) {
  uint8_t block[64] = {0};
  if (keyLen > sizeof block) {
    sha256(key, keyLen, block);  // a key longer than the block is hashed first
  } else {
    std::memcpy(block, key, keyLen);
  }
  uint8_t inner[64];
  uint8_t outer[64];
  for (size_t i = 0; i < sizeof block; i++) {
    inner[i] = static_cast<uint8_t>(block[i] ^ 0x36);
    outer[i] = static_cast<uint8_t>(block[i] ^ 0x5c);
  }
  uint8_t innerHash[32];
  {
    SHA256 sha;
    sha.update(inner, sizeof inner);
    if (dataLen > 0) sha.update(data, dataLen);
    sha.finalize(innerHash, sizeof innerHash);
  }
  SHA256 sha;
  sha.update(outer, sizeof outer);
  sha.update(innerHash, sizeof innerHash);
  sha.finalize(out, 32);
}

}  // namespace

// ---- the crypto, against the standards ---------------------------------------------------------

// FIPS 180-4's own one-block example. If this fails, nothing below means anything.
TEST(Arithmetic, TheLibrarysSha256IsSha256) {
  uint8_t digest[32];
  sha256("abc", 3, digest);
  EXPECT_EQ(hex(digest, sizeof digest), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

// RFC 4231's test case 2 for HMAC-SHA-256, against the helper above and against the library's own
// resetHMAC/finalizeHMAC pair - which is what MeshCore uses and what `transportCodeFor` uses.
TEST(Arithmetic, HmacSha256IsHmacSha256BothWays) {
  const char* key = "Jefe";
  const char* data = "what do ya want for nothing?";
  const std::string expected = "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843";

  uint8_t byHand[32];
  hmacSha256(reinterpret_cast<const uint8_t*>(key), std::strlen(key), reinterpret_cast<const uint8_t*>(data),
             std::strlen(data), byHand);
  EXPECT_EQ(hex(byHand, sizeof byHand), expected);

  uint8_t byLibrary[32];
  SHA256 sha;
  sha.resetHMAC(key, std::strlen(key));
  sha.update(data, std::strlen(data));
  sha.finalizeHMAC(key, std::strlen(key), byLibrary, sizeof byLibrary);
  EXPECT_EQ(hex(byLibrary, sizeof byLibrary), expected);
}

// `TransportKeyStore::getAutoKeyFor`: a hashtag region's key is the first sixteen bytes of
// SHA-256 over `#name`. The expectation is hashed here, by the library, and never pasted.
TEST(Arithmetic, TheKeyOfDkIsTheFirstSixteenBytesOfTheHashOfHashDk) {
  uint8_t digest[32];
  sha256("#dk", 3, digest);

  uint8_t key[mm::kTransportKeyBytes];
  mm::floodScopeKey("dk", key);
  EXPECT_EQ(hex(key, sizeof key), hex(digest, mm::kTransportKeyBytes));
  EXPECT_EQ(std::memcmp(key, digest, sizeof key), 0);

  // A different region is a different key, and the `#` is part of what is hashed: the hash of the
  // bare name is **not** the key.
  uint8_t aarhus[mm::kTransportKeyBytes];
  mm::floodScopeKey("dk-aarhus", aarhus);
  EXPECT_NE(hex(aarhus, sizeof aarhus), hex(key, sizeof key));
  uint8_t bare[32];
  sha256("dk", 2, bare);
  EXPECT_NE(hex(key, sizeof key), hex(bare, mm::kTransportKeyBytes));
}

// `TransportKey::calcTransportCode`: HMAC-SHA256 with the region's key over the payload-type byte
// and the payload, the first two bytes little-endian. The expectation is the HMAC built above out
// of the plain hash, which is a second implementation and not a second copy.
TEST(Arithmetic, TheTransportCodeIsTheHmacOverThePayloadTypeAndThePayload) {
  uint8_t key[mm::kTransportKeyBytes];
  mm::floodScopeKey("dk", key);

  // A fixed packet: an ANON_REQ (payload type 2) of twenty bytes, as a flooded login is.
  const uint8_t payloadType = 2;
  uint8_t payload[20];
  for (size_t i = 0; i < sizeof payload; i++) payload[i] = static_cast<uint8_t>(0xA0 + i);

  std::vector<uint8_t> hashed;
  hashed.push_back(payloadType);
  hashed.insert(hashed.end(), payload, payload + sizeof payload);
  uint8_t mac[32];
  hmacSha256(key, sizeof key, hashed.data(), hashed.size(), mac);
  const uint16_t expected =
      mm::reserveTransportCode(static_cast<uint16_t>(mac[0] | (static_cast<uint16_t>(mac[1]) << 8)));

  EXPECT_EQ(mm::transportCodeFor(key, payloadType, payload, sizeof payload), expected);

  // The payload type is in the HMAC, not beside it: the same bytes under another type is another
  // code, which is why a repeater's match is per packet and not per region alone.
  EXPECT_NE(mm::transportCodeFor(key, 0, payload, sizeof payload),
            mm::transportCodeFor(key, 1, payload, sizeof payload));
  // And so is the payload.
  payload[0] = 0x00;
  EXPECT_NE(mm::transportCodeFor(key, payloadType, payload, sizeof payload), expected);
}

// 0000 and FFFF are reserved. The rule is its own function, so it is walked rather than hunted
// for among payloads.
TEST(Arithmetic, TheTwoReservedCodesAreNeverSent) {
  EXPECT_EQ(mm::reserveTransportCode(0x0000), 0x0001);
  EXPECT_EQ(mm::reserveTransportCode(0xFFFF), 0xFFFE);
  EXPECT_EQ(mm::reserveTransportCode(0x0001), 0x0001);
  EXPECT_EQ(mm::reserveTransportCode(0x4F2A), 0x4F2A);

  uint8_t key[mm::kTransportKeyBytes];
  mm::floodScopeKey("dk", key);
  for (int i = 0; i < 2048; i++) {
    const uint8_t payload[2] = {static_cast<uint8_t>(i & 0xFF), static_cast<uint8_t>(i >> 8)};
    const uint16_t code = mm::transportCodeFor(key, 0, payload, sizeof payload);
    ASSERT_NE(code, 0x0000) << "payload " << i;
    ASSERT_NE(code, 0xFFFF) << "payload " << i;
  }
}

// What `get flood.scope` prints a code for is fixed and documented (docs/console.md), so two
// boards given the same scope can be compared without a repeater in the middle.
TEST(Arithmetic, TheExampleCodeIsTheDocumentedPayload) {
  uint8_t key[mm::kTransportKeyBytes];
  mm::floodScopeKey("dk", key);
  uint8_t payload[mm::kExamplePayloadLen];
  for (size_t i = 0; i < sizeof payload; i++) payload[i] = static_cast<uint8_t>(i);
  EXPECT_EQ(mm::exampleTransportCode(key),
            mm::transportCodeFor(key, mm::kExamplePayloadType, payload, sizeof payload));
}

// ---- the name rule ---------------------------------------------------------------------------

TEST(Name, TheFirmwaresRegionNamesAndNothingElse) {
  EXPECT_TRUE(mm::isFloodScopeValue("dk"));
  EXPECT_TRUE(mm::isFloodScopeValue("dk-aarhus"));
  EXPECT_TRUE(mm::isFloodScopeValue("DK"));
  EXPECT_TRUE(mm::isFloodScopeValue("dk2"));
  EXPECT_TRUE(mm::isFloodScopeValue("#dk")) << "a leading # is how MeshCore writes one";
  EXPECT_TRUE(mm::isFloodScopeValue("*")) << "no scope at all";
  EXPECT_TRUE(mm::isFloodScopeValue(std::string(mm::kRegionNameCap, 'a').c_str()));

  // RegionMap::is_name_char excludes most punctuation: a space, a dot, a slash, a plus, a comma.
  EXPECT_FALSE(mm::isFloodScopeValue("dk aarhus"));
  EXPECT_FALSE(mm::isFloodScopeValue("dk.aarhus"));
  EXPECT_FALSE(mm::isFloodScopeValue("dk/aarhus"));
  EXPECT_FALSE(mm::isFloodScopeValue("dk+1"));
  EXPECT_FALSE(mm::isFloodScopeValue("dk,aarhus"));
  EXPECT_FALSE(mm::isFloodScopeValue("dk(1)"));
  // A private region's key lives in the node's own keystore: the setting takes names, never keys.
  EXPECT_FALSE(mm::isFloodScopeValue("$private"));
  EXPECT_FALSE(mm::isFloodScopeValue("dk$1"));
  // One leading # and no more.
  EXPECT_FALSE(mm::isFloodScopeValue("##dk"));
  EXPECT_FALSE(mm::isFloodScopeValue("dk#1"));
  // Nothing, and more than a RegionEntry::name holds.
  EXPECT_FALSE(mm::isFloodScopeValue(""));
  EXPECT_FALSE(mm::isFloodScopeValue("#"));
  EXPECT_FALSE(mm::isFloodScopeValue(nullptr));
  EXPECT_FALSE(mm::isFloodScopeValue(std::string(mm::kRegionNameCap + 1, 'a').c_str()));
}

TEST(Name, TheHashIsDroppedAndItIsTheNameThatIsHashed) {
  EXPECT_STREQ(mm::floodScopeBareName("#dk"), "dk");
  EXPECT_STREQ(mm::floodScopeBareName("dk"), "dk");
  EXPECT_STREQ(mm::floodScopeBareName("*"), "*");

  uint8_t typed[mm::kTransportKeyBytes];
  uint8_t bare[mm::kTransportKeyBytes];
  mm::floodScopeKey(mm::floodScopeBareName("#dk"), typed);
  mm::floodScopeKey("dk", bare);
  EXPECT_EQ(hex(typed, sizeof typed), hex(bare, sizeof bare)) << "`#dk` and `dk` are one region";
}

// ---- the policy ------------------------------------------------------------------------------

TEST(Policy, TheFirstFloodIsScopedAndTheSecondIsNot) {
  EXPECT_EQ(mm::floodModeFor(1, mm::UnscopedRetry::Retry), mm::FloodMode::Scoped);
  EXPECT_EQ(mm::floodModeFor(2, mm::UnscopedRetry::Retry), mm::FloodMode::Unscoped);
  EXPECT_EQ(mm::floodModeFor(3, mm::UnscopedRetry::Retry), mm::FloodMode::Unscoped);
  EXPECT_EQ(mm::floodModeFor(1, mm::UnscopedRetry::Never), mm::FloodMode::Scoped);
  EXPECT_EQ(mm::floodModeFor(2, mm::UnscopedRetry::Never), mm::FloodMode::Scoped);
}

TEST(Policy, AnUnscopedSettingHasNothingToDropAndSaysSo) {
  mm::FloodPolicy policy;
  EXPECT_EQ(policy.modeForNextFlood("*"), mm::FloodMode::Unscoped);
  EXPECT_EQ(policy.modeForNextFlood("dk"), mm::FloodMode::Scoped);

  EXPECT_TRUE(policy.unscopedRetryFor("dk"));
  EXPECT_FALSE(policy.unscopedRetryFor("*")) << "there is no scope to drop for one send";
  EXPECT_FALSE(policy.unscopedRetryFor(nullptr)) << "this mesh says nothing about scope";
  policy.setRetry(mm::UnscopedRetry::Never);
  EXPECT_FALSE(policy.unscopedRetryFor("dk"));
}

TEST(Policy, OneTransactionMovesOneCount) {
  mm::FloodPolicy policy;

  // The scoped flood was answered.
  policy.begin();
  policy.sent(true, policy.modeForNextFlood("dk"));
  policy.answered();
  policy.end();
  EXPECT_EQ(policy.counts().answeredScoped, 1u);
  EXPECT_EQ(policy.counts().answeredUnscoped, 0u);
  EXPECT_EQ(policy.counts().unanswered, 0u);

  // Scoped silent, unscoped answered: the retry earned its airtime, once.
  policy.begin();
  policy.sent(true, policy.modeForNextFlood("dk"));
  policy.sent(true, policy.modeForNextFlood("dk"));
  policy.answered();
  policy.end();
  EXPECT_EQ(policy.counts().answeredScoped, 1u);
  EXPECT_EQ(policy.counts().answeredUnscoped, 1u);

  // Both silent.
  policy.begin();
  policy.sent(true, policy.modeForNextFlood("dk"));
  policy.sent(true, policy.modeForNextFlood("dk"));
  policy.end();
  EXPECT_EQ(policy.counts().unanswered, 1u);

  // A transaction that never flooded moves nothing, which is every transaction of an exit node
  // whose repeaters all have paths.
  policy.begin();
  policy.sent(false, policy.modeForNextFlood("dk"));
  policy.answered();
  policy.end();
  EXPECT_EQ(policy.counts().answeredScoped, 1u);
  EXPECT_EQ(policy.counts().answeredUnscoped, 1u);
  EXPECT_EQ(policy.counts().unanswered, 1u);
}

TEST(Policy, ANestedRetryLoopFloodsInsideTheOneAlreadyRunning) {
  mm::FloodPolicy policy;
  policy.begin();
  policy.sent(true, policy.modeForNextFlood("dk"));  // flood 1, scoped, silent
  policy.begin();                                     // a re-login inside the request's retry
  policy.sent(true, policy.modeForNextFlood("dk"));   // flood 2 of the same transaction
  policy.answered();
  policy.end();
  EXPECT_EQ(policy.counts().answeredScoped + policy.counts().answeredUnscoped + policy.counts().unanswered, 0u)
      << "the inner end() settles nothing";
  policy.end();
  EXPECT_EQ(policy.counts().answeredUnscoped, 1u);
  EXPECT_EQ(policy.counts().answeredScoped, 0u);
}

TEST(Policy, TheWordsAJobsLogPrints) {
  char out[48];
  mm::FloodPolicy::describeSend(true, mm::FloodMode::Scoped, "dk", out, sizeof out);
  EXPECT_STREQ(out, "direct");
  mm::FloodPolicy::describeSend(false, mm::FloodMode::Scoped, "dk", out, sizeof out);
  EXPECT_STREQ(out, "flood, scope dk");
  mm::FloodPolicy::describeSend(false, mm::FloodMode::Unscoped, "dk", out, sizeof out);
  EXPECT_STREQ(out, "flood, unscoped");
  mm::FloodPolicy::describeSend(false, mm::FloodMode::Scoped, "*", out, sizeof out);
  EXPECT_STREQ(out, "flood, unscoped");
  mm::FloodPolicy::describeSend(false, mm::FloodMode::Scoped, nullptr, out, sizeof out);
  EXPECT_STREQ(out, "flood, unscoped");
}

// ---- the two together, over the fake repeater -------------------------------------------------

class FloodTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fake::LogCapture::the().begin();
    platform.fakeClock().setFromNetwork(kNow);
    mesh.setKey(fake::repeaterKey());
    mesh.setSelfKey(fake::selfKey());
    mesh.setFloodScope("dk");  // what the board's key table falls back to
    mesh.setAdminPassword("adm1n-pass");
    mesh.setRepeaterClock(kNow - 600);
    server.setLinkUp(true);
    server.setServerTime(kNow);
    runner.begin(mesh, server, platform, nullptr);
    runner.setIdentity(fake::selfKey().c_str());
    mm::AgentBudget budget;
    budget.maxTransactionsPerMinute = 60;
    budget.minGapSeconds = 1;
    runner.setBudget(budget);
  }

  std::string run(const fake::Job& job) {
    server.queue(job.json());
    EXPECT_EQ(runner.claimAndRun(), 1);
    EXPECT_FALSE(server.results().empty());
    return server.results().empty() ? std::string() : server.results().back().body;
  }

  std::string floodLine() const {
    char out[240];
    runner.describeFlood(out, sizeof out);
    return out;
  }

  fake::FakePlatform platform;
  fake::FakeMesh mesh{platform};
  fake::FakeUplink server{platform};
  mm::JobRunner runner;
};

// A repeater with no region map drops a `dk` flood it would have forwarded unscoped. The login's
// first try is scoped and silent, its second unscoped and answered, the job's log names the shape
// of each, and the transaction counts as *answered unscoped*.
TEST_F(FloodTest, AFloodedLoginSendsTryOneScopedAndTryTwoUnscoped) {
  mesh.setFloodsReaching(/*scoped=*/false, /*unscoped=*/true);
  mesh.grant(fake::selfKey(), mm::kPermReadOnly);
  const std::string body = run(fake::Job(12400, "status").atClass("acl").leasedUntil(kLease).withNoPath());

  const std::vector<bool> shapes = mesh.floodShapes();
  ASSERT_GE(shapes.size(), 2u) << body;
  EXPECT_TRUE(shapes[0]) << "try 1 is scoped";
  EXPECT_FALSE(shapes[1]) << "try 2 drops the scope";

  EXPECT_NE(body.find("try 1: no reply (flood, scope dk)"), std::string::npos) << body;
  EXPECT_NE(body.find("try 2: logged in as"), std::string::npos) << body;
  EXPECT_NE(body.find("(flood, unscoped)"), std::string::npos) << body;

  EXPECT_GE(runner.floodCounts().answeredUnscoped, 1u);
  EXPECT_EQ(runner.floodCounts().answeredScoped, 0u);
  EXPECT_NE(floodLine().find("scope dk, answered scoped 0 / unscoped"), std::string::npos) << floodLine();
  EXPECT_NE(floodLine().find("the second flood of a transaction goes unscoped"), std::string::npos) << floodLine();
  EXPECT_TRUE(runner.floodUnscopedRetry());
}

// `flood.unscoped never` is the day unscoped floods stop being forwarded at all: both tries are
// scoped, and on a repeater that holds no region nothing comes back.
TEST_F(FloodTest, UnderNeverBothTriesAreScoped) {
  runner.setFloodRetry(mm::UnscopedRetry::Never);
  mesh.setFloodsReaching(/*scoped=*/false, /*unscoped=*/true);
  mesh.grant(fake::selfKey(), mm::kPermReadOnly);
  const std::string body = run(fake::Job(12401, "status").atClass("acl").leasedUntil(kLease).withNoPath());

  for (bool scoped : mesh.floodShapes()) EXPECT_TRUE(scoped) << "every flood is scoped under `never`";
  EXPECT_NE(body.find("try 1: no reply (flood, scope dk)"), std::string::npos) << body;
  EXPECT_NE(body.find("try 2: no reply (flood, scope dk)"), std::string::npos) << body;
  EXPECT_GE(runner.floodCounts().unanswered, 1u);
  EXPECT_EQ(runner.floodCounts().answeredUnscoped, 0u);
  EXPECT_FALSE(runner.floodUnscopedRetry());
  EXPECT_NE(floodLine().find("every flood is scoped (flood.unscoped never)"), std::string::npos) << floodLine();
}

// A repeater that holds `dk` answers the first try, and the unscoped retry never goes out: the
// count that matters - how often the retry earned its airtime - stays at nought.
TEST_F(FloodTest, AScopedFloodThatIsAnsweredNeedsNoRetry) {
  mesh.grant(fake::selfKey(), mm::kPermReadOnly);
  const std::string body = run(fake::Job(12402, "status").atClass("acl").leasedUntil(kLease).withNoPath());

  for (bool scoped : mesh.floodShapes()) EXPECT_TRUE(scoped);
  EXPECT_NE(body.find("(flood, scope dk"), std::string::npos) << body;
  EXPECT_GE(runner.floodCounts().answeredScoped, 1u);
  EXPECT_EQ(runner.floodCounts().answeredUnscoped, 0u);
  EXPECT_EQ(runner.floodCounts().unanswered, 0u);
}

// `set flood.scope *`: every flood goes out with no transport code at all, whatever the runner
// tells the mesh, and the heartbeat's `unscopedRetry` is false because there is nothing to drop.
TEST_F(FloodTest, AnUnscopedBoardFloodsUnscopedAndCountsItAsSuch) {
  mesh.setFloodScope("*");
  mesh.setFloodsReaching(/*scoped=*/false, /*unscoped=*/true);
  mesh.grant(fake::selfKey(), mm::kPermReadOnly);
  const std::string body = run(fake::Job(12403, "status").atClass("acl").leasedUntil(kLease).withNoPath());

  for (bool scoped : mesh.floodShapes()) EXPECT_FALSE(scoped);
  EXPECT_NE(body.find("try 1: logged in as"), std::string::npos) << body;
  EXPECT_NE(body.find("(flood, unscoped)"), std::string::npos) << body;
  EXPECT_GE(runner.floodCounts().answeredUnscoped, 1u);
  EXPECT_FALSE(runner.floodUnscopedRetry());
  EXPECT_NE(floodLine().find("scope *"), std::string::npos) << floodLine();
}

// A discover with no path floods twice - the login for the path its reply comes back along, and
// the path-discovery request after it - and that is **one** transaction: the login scoped, the
// discovery unscoped, one count.
TEST_F(FloodTest, ADiscoverThatFloodsTwiceSendsTheSecondUnscoped) {
  mesh.setGuestPassword("something-else");  // the blank login is the silence it is meant to be
  const std::string body = run(fake::Job(12404, "discover").atClass("guest").leasedUntil(kLease).withNoPath());

  ASSERT_EQ(mesh.sendCount(), 2) << body;
  const std::vector<bool> shapes = mesh.floodShapes();
  ASSERT_EQ(shapes.size(), 2u);
  EXPECT_TRUE(shapes[0]) << "the flooded login is the transaction's first flood";
  EXPECT_FALSE(shapes[1]) << "the path-discovery request is its second";
  EXPECT_EQ(runner.floodCounts().unanswered, 1u) << "two floods, one transaction, one count";
  EXPECT_EQ(runner.floodCounts().answeredScoped + runner.floodCounts().answeredUnscoped, 0u);
  EXPECT_NE(body.find("the last resort"), std::string::npos) << body;
  EXPECT_NE(body.find("(flood, unscoped)"), std::string::npos) << body;
}

// **Direct sends are unchanged.** A path the server holds means every send of the job is DIRECT:
// the mesh is still told a mode before each one and ignores it, nothing carries a scope, the
// counts do not move, and the log says `direct` as it did before this task.
TEST_F(FloodTest, ADirectSendCarriesNoScopeAndCountsNothing) {
  mesh.grant(fake::selfKey(), mm::kPermReadOnly);
  const std::string body = run(fake::Job(12405, "status")
                                   .atClass("acl")
                                   .leasedUntil(kLease)
                                   .alongPath({"1122", "3344"}));

  ASSERT_GT(mesh.sendCount(), 0);
  for (const fake::Send& s : mesh.sends()) {
    EXPECT_TRUE(s.direct) << s.kindName();
    EXPECT_FALSE(s.scoped) << "a direct send carries no transport code";
  }
  EXPECT_TRUE(mesh.floodShapes().empty());
  EXPECT_EQ(runner.floodCounts().answeredScoped, 0u);
  EXPECT_EQ(runner.floodCounts().answeredUnscoped, 0u);
  EXPECT_EQ(runner.floodCounts().unanswered, 0u);
  EXPECT_NE(body.find("(direct)"), std::string::npos) << body;
  EXPECT_EQ(body.find("flood,"), std::string::npos) << "no line of a direct job's log names a flood's shape";
}

// ---- what the board does with it, read off the source ------------------------------------------

// `AgentMesh` has **one** place a flood of this board goes out, and it is the only place a
// transport code is computed: a direct send cannot carry one (MeshCore's `sendDirect` has no
// overload that takes any), and a flood that was told `Unscoped` goes out through the
// no-codes overload of `sendFlood`. A grep, because `AgentMesh.cpp` needs a board to run.
TEST(Board, EveryFloodGoesOutThroughTheOnePlaceAndNoDirectSendCarriesACode) {
  const std::string text = readWhole(fs::path(PROJECT_DIR) / "src" / "mesh" / "AgentMesh.cpp");
  ASSERT_FALSE(text.empty());

  // The two sendFlood calls are the two arms of floodOut, and nothing else calls it.
  size_t calls = 0;
  size_t at = 0;
  while ((at = text.find("sendFlood(", at)) != std::string::npos) {
    calls++;
    at += 10;
  }
  EXPECT_EQ(calls, 2u) << "sendFlood is called in floodOut's two arms and nowhere else";
  EXPECT_NE(text.find("sendFlood(pkt, 0, kFloodPathHashSize);"), std::string::npos)
      << "the unscoped arm is the overload that attaches no transport codes";
  EXPECT_NE(text.find("sendFlood(pkt, codes, 0, kFloodPathHashSize);"), std::string::npos);
  EXPECT_NE(text.find("codes[1] = 0;"), std::string::npos) << "code 1 is left 0, as sendFloodScoped leaves it";

  // And every other send of this board: the direct ones take a path and no codes.
  at = 0;
  while ((at = text.find("sendDirect(", at)) != std::string::npos) {
    const size_t end = text.find(';', at);
    ASSERT_NE(end, std::string::npos);
    const std::string call = text.substr(at, end - at);
    EXPECT_EQ(call.find("codes"), std::string::npos) << call << " - a direct send carries no transport code";
    at = end;
  }
  EXPECT_EQ(text.find("sendZeroHop("), std::string::npos) << "this firmware sends no zero-hop packet";
}

// The citations this port rests on, held against the pinned source on disk - the half of the
// check a host build can do, and the one that catches a bump that moved either function
// (docs/upstream.md, tasks/sync/TEMPLATE.md). Skipped when MeshCore is not unpacked, as
// test_runner's citation test skips its own half: CI's native job installs the host dependencies
// alone.
TEST(Upstream, TheArithmeticIsStillWhatThePinnedSourceDoes) {
  const fs::path meshcore = fs::path(PROJECT_DIR) / ".pio" / "libdeps" / "native" / "MeshCore";
  if (!fs::is_directory(meshcore)) GTEST_SKIP() << meshcore << " is not unpacked";

  const std::string store = readWhole(meshcore / "src" / "helpers" / "TransportKeyStore.cpp");
  ASSERT_FALSE(store.empty());
  // getAutoKeyFor: the name, hashed whole, the first sizeof(key) bytes kept.
  EXPECT_NE(store.find("sha.update(name, strlen(name));"), std::string::npos);
  EXPECT_NE(store.find("sha.finalize(&dest.key, sizeof(dest.key));"), std::string::npos);
  // calcTransportCode: HMAC over the payload type and the payload, two bytes, 0000 and FFFF
  // reserved.
  EXPECT_NE(store.find("sha.resetHMAC(key, sizeof(key));"), std::string::npos);
  EXPECT_NE(store.find("sha.update(&type, 1);"), std::string::npos);
  EXPECT_NE(store.find("sha.update(packet->payload, packet->payload_len);"), std::string::npos);
  EXPECT_NE(store.find("sha.finalizeHMAC(key, sizeof(key), &code, 2);"), std::string::npos);
  EXPECT_NE(store.find("reserve codes 0000 and FFFF"), std::string::npos);
  // And the `#` is the caller's: a region stored without one is hashed with one added.
  const std::string map = readWhole(meshcore / "src" / "helpers" / "RegionMap.cpp");
  ASSERT_FALSE(map.empty());
  EXPECT_NE(map.find("tmp[0] = '#';"), std::string::npos);
  EXPECT_NE(map.find("return c == '-' || c == '$' || c == '#' || (c >= '0' && c <= '9') || c >= 'A';"),
            std::string::npos)
      << "RegionMap::is_name_char moved; src/mesh/TransportScope.cpp copies this rule";

  // The name's size, which is where kRegionNameCap comes from.
  const std::string header = readWhole(meshcore / "src" / "helpers" / "RegionMap.h");
  ASSERT_FALSE(header.empty());
  EXPECT_NE(header.find("char name[31];"), std::string::npos)
      << "RegionEntry::name is no longer char[31]; mm::kRegionNameCap is " << mm::kRegionNameCap;

  // The two overloads this firmware floods through, and what each puts in the header: the one
  // with no codes is `ROUTE_TYPE_FLOOD`, which is what `flood.scope *` sends, and the one with
  // them is `ROUTE_TYPE_TRANSPORT_FLOOD`. And `sendDirect` has no overload that takes any codes
  // at all, which is why a direct send of this board cannot carry one.
  const std::string meshHeader = readWhole(meshcore / "src" / "Mesh.h");
  const std::string meshSource = readWhole(meshcore / "src" / "Mesh.cpp");
  ASSERT_FALSE(meshHeader.empty());
  ASSERT_FALSE(meshSource.empty());
  EXPECT_NE(meshHeader.find("void sendFlood(Packet* packet, uint32_t delay_millis=0, uint8_t path_hash_size=1);"),
            std::string::npos);
  EXPECT_NE(meshHeader.find("void sendFlood(Packet* packet, uint16_t* transport_codes, uint32_t delay_millis=0, "
                            "uint8_t path_hash_size=1);"),
            std::string::npos);
  EXPECT_NE(meshHeader.find("void sendDirect(Packet* packet, const uint8_t* path, uint8_t path_len, uint32_t "
                            "delay_millis=0);"),
            std::string::npos);
  {
    size_t at = 0;
    int directDeclarations = 0;
    while ((at = meshHeader.find("sendDirect(", at)) != std::string::npos) {
      const size_t end = meshHeader.find(')', at);
      ASSERT_NE(end, std::string::npos);
      EXPECT_EQ(meshHeader.substr(at, end - at).find("transport_codes"), std::string::npos)
          << "a sendDirect overload now takes transport codes";
      directDeclarations++;
      at = end;
    }
    EXPECT_EQ(directDeclarations, 1) << "one sendDirect, and it takes a path";
  }
  const size_t unscopedArm = meshSource.find("void Mesh::sendFlood(Packet* packet, uint32_t delay_millis");
  const size_t scopedArm = meshSource.find("void Mesh::sendFlood(Packet* packet, uint16_t* transport_codes");
  ASSERT_NE(unscopedArm, std::string::npos);
  ASSERT_NE(scopedArm, std::string::npos);
  EXPECT_NE(meshSource.find("packet->header |= ROUTE_TYPE_FLOOD;", unscopedArm), std::string::npos);
  EXPECT_LT(meshSource.find("packet->header |= ROUTE_TYPE_FLOOD;", unscopedArm), scopedArm)
      << "the overload with no codes is the one that sends ROUTE_TYPE_FLOOD";
  EXPECT_NE(meshSource.find("packet->header |= ROUTE_TYPE_TRANSPORT_FLOOD;", scopedArm), std::string::npos);
  EXPECT_NE(meshSource.find("packet->transport_codes[0] = transport_codes[0];", scopedArm), std::string::npos);

  // The companion's own default scope hashes `#` and the name, which is what floodScopeKey does.
  const std::string companion = readWhole(meshcore / "examples" / "companion_radio" / "MyMesh.cpp");
  ASSERT_FALSE(companion.empty());
  EXPECT_NE(companion.find("getAutoKeyFor(0, \"#\" DEFAULT_FLOOD_SCOPE_NAME, key)"), std::string::npos);
  // sendFloodScoped: code 0 from the scope over the packet, code 1 left 0.
  EXPECT_NE(companion.find("codes[0] = scope.calcTransportCode(pkt);"), std::string::npos);
  EXPECT_NE(companion.find("codes[1] = 0;"), std::string::npos);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
