// A repeater's password on this board (task 04; agent/Credentials.h): held in a fixed buffer
// for the job, zeroed after, never in NVS or on the file system, never printed. Each of those is
// a test here and not a promise - the buffer is read back after the wipe, the claim is parsed
// in place and its bytes shown gone, the modules the password passes through are grepped for
// the store and file APIs, and the login frame's redaction is held to the .NET agent's shape.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <agent/Credentials.h>
#include <agent/JobEnvelopes.h>

#ifndef PROJECT_DIR
#error "PROJECT_DIR must be defined; platformio.ini's [env:native] does it"
#endif

namespace {

namespace fs = std::filesystem;

std::string readFile(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  EXPECT_TRUE(in.good()) << "cannot read " << p.generic_string();
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The claim of MeshMaster.Agent.Tests' CredentialTests, five jobs and three secrets: a guest
// password with a non-ASCII letter and a quote, an admin password, a rotation's two.
const char* kDotNetClaim =
    "{\"serverTime\":\"2026-09-23T12:00:00Z\",\"jobs\":["
    "{\"id\":1,\"type\":\"status\",\"repeater\":{\"id\":17,\"label\":\"A\",\"publicKey\":\"aa\"},"
    "\"credentialClass\":\"guest\",\"leaseUntil\":\"2026-09-23T12:02:00Z\",\"timeouts\":{\"perHopMs\":4000,"
    "\"loginMs\":8000},\"payload\":{},\"credential\":{\"guestPassword\":\"g\\u00e6st \\\"1\\\"\"}},"
    "{\"id\":2,\"type\":\"acl.set\",\"repeater\":{\"id\":18,\"label\":\"B\",\"publicKey\":\"bb\"},"
    "\"credentialClass\":\"admin\",\"leaseUntil\":\"2026-09-23T12:02:00Z\",\"timeouts\":{\"perHopMs\":4000,"
    "\"loginMs\":8000},\"payload\":{\"publicKey\":\"cc\",\"level\":1},\"credential\":{\"adminPassword\":\"hunter2\"},"
    "\"groupWith\":[3]},"
    "{\"id\":3,\"type\":\"neighbours\",\"repeater\":{\"id\":18,\"label\":\"B\",\"publicKey\":\"bb\"},"
    "\"credentialClass\":\"acl\",\"leaseUntil\":\"2026-09-23T12:02:00Z\",\"timeouts\":{\"perHopMs\":4000,"
    "\"loginMs\":8000},\"payload\":{},\"credential\":{\"acl\":true}},"
    "{\"id\":4,\"type\":\"trace\",\"repeater\":{\"id\":19,\"label\":\"C\",\"publicKey\":\"dd\"},"
    "\"credentialClass\":\"none\",\"leaseUntil\":\"2026-09-23T12:02:00Z\",\"timeouts\":{\"perHopMs\":4000,"
    "\"loginMs\":8000},\"payload\":{}},"
    "{\"id\":5,\"type\":\"password.set\",\"repeater\":{\"id\":17,\"label\":\"A\",\"publicKey\":\"aa\"},"
    "\"credentialClass\":\"admin\",\"leaseUntil\":\"2026-09-23T12:02:00Z\",\"timeouts\":{\"perHopMs\":4000,"
    "\"loginMs\":8000},\"payload\":{\"field\":\"adminPassword\"},\"credential\":{\"adminPassword\":\"hunter2\","
    "\"newPassword\":\"Rotated-Admin-9\"}}"
    "]}";

bool allZero(const void* p, size_t len) {
  const uint8_t* b = static_cast<const uint8_t*>(p);
  for (size_t i = 0; i < len; i++) {
    if (b[i] != 0) return false;
  }
  return true;
}

// ------------------------------------------------------------------ the buffer

TEST(Credential, ThePasswordIsHeldInAFixedBufferAndWipedToZeros) {
  mm::JobCredential c;
  c.setAdminPassword("hunter2");
  c.setGuestPassword("hello");
  EXPECT_TRUE(c.hasAdminPassword);
  EXPECT_TRUE(c.hasGuestPassword);
  EXPECT_STREQ(c.adminPassword, "hunter2");
  EXPECT_STREQ(c.guestPassword, "hello");
  EXPECT_FALSE(c.isWiped());

  c.wipe();

  // Every byte, read back: not a flag that says so, the bytes themselves.
  EXPECT_TRUE(c.isWiped());
  EXPECT_TRUE(allZero(c.adminPassword, sizeof c.adminPassword));
  EXPECT_TRUE(allZero(c.guestPassword, sizeof c.guestPassword));
  EXPECT_FALSE(c.hasAdminPassword);
  EXPECT_FALSE(c.hasGuestPassword);
}

TEST(Credential, AtMostFifteenBytesAreKeptAsACompanionSendsThem) {
  mm::JobCredential c;
  c.setAdminPassword("0123456789abcdefghij");
  EXPECT_STREQ(c.adminPassword, "0123456789abcde");
  EXPECT_TRUE(c.adminPasswordCut);
  c.setAdminPassword("exactly15chars!");
  EXPECT_STREQ(c.adminPassword, "exactly15chars!");
  EXPECT_FALSE(c.adminPasswordCut);
  c.setGuestPassword("");
  EXPECT_FALSE(c.hasGuestPassword);
  c.setGuestPassword(nullptr);
  EXPECT_FALSE(c.hasGuestPassword);
}

TEST(Credential, ANewPasswordOverwritesTheOldOneWhole) {
  mm::JobCredential c;
  c.setAdminPassword("a-long-password");
  c.setAdminPassword("x");
  EXPECT_STREQ(c.adminPassword, "x");
  EXPECT_TRUE(allZero(c.adminPassword + 2, sizeof c.adminPassword - 2)) << "the tail of the old one is gone";
}

// ------------------------------------------------------------------ the claim

TEST(Credential, TheClaimIsParsedInPlaceAndThePasswordLandsInTheJobsBufferAlone) {
  std::string body = kDotNetClaim;
  mm::ClaimedJob jobs[5];
  int count = 0;
  uint32_t serverTime = 0;
  char err[96] = {0};
  ASSERT_TRUE(mm::parseClaimResponse(&body[0], body.size(), jobs, 5, count, serverTime, err, sizeof err)) << err;
  ASSERT_EQ(count, 5);

  EXPECT_TRUE(jobs[0].credential.hasGuestPassword);
  EXPECT_STREQ(jobs[0].credential.guestPassword, "g\xC3\xA6st \"1\"");
  EXPECT_FALSE(jobs[0].credential.hasAdminPassword);

  EXPECT_TRUE(jobs[1].credential.hasAdminPassword);
  EXPECT_STREQ(jobs[1].credential.adminPassword, "hunter2");
  EXPECT_FALSE(jobs[1].credential.hasGuestPassword);
  EXPECT_TRUE(jobs[1].hasAclSet);
  EXPECT_STREQ(jobs[1].aclSetKey, "cc");
  EXPECT_EQ(jobs[1].aclSetLevel, 1);

  EXPECT_TRUE(jobs[2].hasAclLevel);
  EXPECT_EQ(jobs[2].aclLevel, 1) << "`true` from a server before task 60 is read-only";
  EXPECT_TRUE(jobs[2].credential.isWiped());

  EXPECT_TRUE(jobs[3].credential.isWiped());
  EXPECT_STREQ(jobs[4].credential.adminPassword, "hunter2");

  // The parse is zero-copy: the caller's buffer is the one other place the password is, and
  // the caller wipes it. After that, the password is in the jobs and nowhere this test can see.
  mm::wipeBytes(&body[0], body.size());
  EXPECT_EQ(body.find("hunter2"), std::string::npos);
  EXPECT_EQ(body.find("Rotated"), std::string::npos);
  EXPECT_STREQ(jobs[1].credential.adminPassword, "hunter2") << "the job's own copy does not point into the buffer";

  // And when the job is over, nothing.
  for (mm::ClaimedJob& job : jobs) job.credential.wipe();
  for (const mm::ClaimedJob& job : jobs) EXPECT_TRUE(job.credential.isWiped());
}

TEST(Credential, ThePasswordIsNeverInTheJobsPrintableFields) {
  std::string body = kDotNetClaim;
  mm::ClaimedJob jobs[5];
  int count = 0;
  uint32_t serverTime = 0;
  char err[96] = {0};
  ASSERT_TRUE(mm::parseClaimResponse(&body[0], body.size(), jobs, 5, count, serverTime, err, sizeof err)) << err;
  // What the job's log line prints is the type, the repeater's id and label and the class; the
  // label and the type are the only free text of a job that reaches a printf, and neither holds
  // the password.
  for (int i = 0; i < count; i++) {
    EXPECT_EQ(std::string(jobs[i].repeaterLabel).find("hunter2"), std::string::npos);
    EXPECT_EQ(std::string(jobs[i].type).find("hunter2"), std::string::npos);
    EXPECT_EQ(std::string(jobs[i].idempotencyKey).find("hunter2"), std::string::npos);
  }
}

// Belt beside the claim's braces (task p4.1-60): the guest password is never used through a
// pair whose probes rely on the ACL entry.
TEST(Credential, TheGuestPasswordIsRefusedOnAPairThatHoldsAGrant) {
  std::string body = kDotNetClaim;
  mm::ClaimedJob jobs[5];
  int count = 0;
  uint32_t serverTime = 0;
  char err[96] = {0};
  ASSERT_TRUE(mm::parseClaimResponse(&body[0], body.size(), jobs, 5, count, serverTime, err, sizeof err)) << err;

  char why[200] = {0};
  EXPECT_TRUE(mm::canRun(jobs[0], why, sizeof why)) << why;  // guest, no grant: a repeater nobody associated

  mm::ClaimedJob associated = jobs[0];
  associated.hasAclLevel = true;
  associated.aclLevel = 1;
  EXPECT_FALSE(mm::canRun(associated, why, sizeof why));
  EXPECT_NE(std::string(why).find("guest password"), std::string::npos) << why;
  EXPECT_NE(std::string(why).find("task p4.1-60"), std::string::npos) << why;
  EXPECT_EQ(std::string(why).find(associated.credential.guestPassword), std::string::npos) << "the sentence names no value";
}

// ------------------------------------------------------------------ no store, no file

// The password passes through five modules: the credential itself, the claim envelope that
// fills it, the login policy that chooses which class sends which (task p1.4-14), the runner that
// uses it and the mesh that sends it. None of them may name the NVS API, the file systems or the
// settings store; a grep says so, the way the TLS policy's does.
TEST(Credential, NoModuleThePasswordPassesThroughNamesTheStoreOrAFileSystem) {
  const char* const files[] = {"lib/agent-core/include/agent/Credentials.h",
                               "lib/agent-core/src/Credentials.cpp",
                               "lib/agent-core/include/agent/JobEnvelopes.h",
                               "lib/agent-core/src/JobEnvelopes.cpp",
                               "lib/agent-core/include/agent/LoginSequence.h",
                               "lib/agent-core/src/LoginSequence.cpp",
                               "lib/agent-core/include/agent/JobRunner.h",
                               "lib/agent-core/src/JobRunner.cpp",
                               "src/mesh/AgentMesh.h",
                               "src/mesh/AgentMesh.cpp"};
  // Split, so this file does not contain what it forbids.
  const std::vector<std::string> forbidden = {
      std::string("Prefer") + "ences",      // the Arduino NVS wrapper
      std::string("nvs") + "_",             // the ESP-IDF NVS API
      std::string("SPI") + "FFS",           // the file systems
      std::string("Little") + "FS",
      std::string("FFat") + "",
      std::string("fop") + "en(",
      std::string("Identity") + "Store",    // MeshCore's identity file
      std::string("Settings") + "Store",    // this firmware's NVS store (settings/Store.h)
      std::string("settings/") + "Store.h",
      std::string("#include <") + "FS.h>",
  };
  std::vector<std::string> hits;
  for (const char* rel : files) {
    const fs::path p = fs::path(PROJECT_DIR) / rel;
    std::ifstream in(p, std::ios::binary);
    ASSERT_TRUE(in.good()) << "cannot read " << p.generic_string();
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
      n++;
      for (const std::string& pat : forbidden) {
        if (line.find(pat) != std::string::npos) hits.push_back(std::string(rel) + ":" + std::to_string(n) + ": " + pat);
      }
    }
  }
  EXPECT_TRUE(hits.empty()) << "a module the password passes through names a store or a file system:\n" +
                                   [&] {
                                     std::string all;
                                     for (const std::string& h : hits) all += "  " + h + "\n";
                                     return all;
                                   }();
}

// The wipe is on the one path every job takes, whatever it came to: after the handler, before
// the result is posted, and once more for every slot when the claim is over.
TEST(Credential, TheRunnerWipesTheCredentialAfterEveryJob) {
  const std::string runner = readFile(fs::path(PROJECT_DIR) / "lib/agent-core/src/JobRunner.cpp");
  const size_t run = runner.find("Outcome outcome = runJob(job, ctx, dataLen);");
  const size_t wipe = runner.find("job.credential.wipe();", run);
  const size_t post = runner.find("posted = post(job.id, g_body, len);", run);
  ASSERT_NE(run, std::string::npos);
  ASSERT_NE(wipe, std::string::npos);
  ASSERT_NE(post, std::string::npos);
  EXPECT_LT(wipe, post) << "the password is wiped before the result leaves the board";
  EXPECT_NE(runner.find("for (int i = 0; i < kClaimMax; i++) g_jobs[i].credential.wipe();"), std::string::npos);

  // And the mesh wipes the plaintext the moment the packet is built.
  const std::string mesh = readFile(fs::path(PROJECT_DIR) / "src/mesh/AgentMesh.cpp");
  const size_t built = mesh.find("createAnonDatagram(PAYLOAD_TYPE_ANON_REQ, self_id, peer_, sharedSecret(), temp, 4 + len)");
  const size_t wiped = mesh.find("wipeBytes(temp, sizeof temp);", built);
  ASSERT_NE(built, std::string::npos);
  ASSERT_NE(wiped, std::string::npos);

  // And the client wipes the claim's bytes as soon as they are parsed, took or not.
  const std::string client = readFile(fs::path(PROJECT_DIR) / "src/agent/AgentClient.cpp");
  const size_t parsed = client.find("parseClaimResponse(body, bodyLen, jobs, max, count, serverTime, err, sizeof err)");
  const size_t cleared = client.find("wipe(body, sizeof body);", parsed);
  ASSERT_NE(parsed, std::string::npos);
  ASSERT_NE(cleared, std::string::npos);
  EXPECT_EQ(client.find("getString()"), std::string::npos) << "no String is made of a reply body";
}

// ------------------------------------------------------------------ the frame log

// The .NET agent's --log-frames writes CMD_SEND_LOGIN with every password byte replaced by `*`.
// On the air a login is an ANON_REQ: the destination's hash, our public key, and the encrypted
// body that was made from the password - so the console prints the first 33 bytes as hex and
// one star per byte of the body.
TEST(Credential, TheLoginFrameIsPrintedWithItsBodyAsStars) {
  std::vector<uint8_t> payload;
  payload.push_back(0xEF);                                   // the repeater's hash
  for (int i = 0; i < 32; i++) payload.push_back(static_cast<uint8_t>(i));  // our key
  for (int i = 0; i < 18; i++) payload.push_back(0xAB);      // the cipher: MAC and one block, say
  char out[256];
  const size_t len = mm::redactLoginPayloadHex(payload.data(), payload.size(), out, sizeof out);
  const std::string text(out, len);
  EXPECT_EQ(text.size(), 2u * 33 + 18);
  EXPECT_EQ(text.substr(0, 4), "ef00");
  EXPECT_EQ(text.substr(2 * 33), std::string(18, '*'));
  EXPECT_EQ(text.find("ab"), std::string::npos) << "not one byte of the body is printed";
}

// The value a password.set sets lives in the same buffer, under the same rules (task 05).
TEST(Credential, TheNewPasswordIsHeldBesideTheOthersAndWipedWithThem) {
  mm::JobCredential c;
  c.setAdminPassword("hunter2");
  c.setNewPassword("Rotated-1");
  EXPECT_TRUE(c.hasNewPassword);
  EXPECT_STREQ(c.newPassword, "Rotated-1");
  EXPECT_FALSE(c.newPasswordCut);
  EXPECT_FALSE(c.isWiped());
  c.wipe();
  EXPECT_TRUE(c.isWiped());
  for (size_t i = 0; i < sizeof c.newPassword; i++) EXPECT_EQ(c.newPassword[i], 0) << i;
  EXPECT_FALSE(c.hasNewPassword);

  // Sixteen bytes: the firmware keeps fifteen (`char password[16]`), and a cut-off password is a
  // lock-out, so the flag is what the runner refuses on before anything is sent.
  c.setNewPassword("sixteen-chars-xx");
  EXPECT_TRUE(c.newPasswordCut);
  EXPECT_EQ(std::strlen(c.newPassword), mm::kMaxPasswordLen);
  c.setNewPassword("");
  EXPECT_FALSE(c.hasNewPassword);
  c.setNewPassword(nullptr);
  EXPECT_FALSE(c.hasNewPassword);
  EXPECT_FALSE(c.newPasswordCut);
}

// A `password` line on the air is a TXT_MSG datagram: the destination's hash, our hash, and the
// MAC and cipher of the text (Mesh::createDatagram at d9296435). The frame log prints the two
// hashes as hex and one star per byte after them.
TEST(Credential, ThePasswordLineFrameIsPrintedWithItsBodyAsStars) {
  std::vector<uint8_t> payload;
  payload.push_back(0xEF);  // the repeater's hash
  payload.push_back(0x7F);  // ours
  for (int i = 0; i < 2 + 32; i++) payload.push_back(0xCD);  // the MAC and two blocks
  char out[256];
  const size_t len = mm::redactPayloadHex(payload.data(), payload.size(), mm::kDatagramClearBytes, out, sizeof out);
  const std::string text(out, len);
  EXPECT_EQ(text, "ef7f" + std::string(34, '*'));
  EXPECT_EQ(text.find("cd"), std::string::npos);
  EXPECT_EQ(mm::kDatagramClearBytes, 2u);
}

TEST(Credential, TheRedactionIsBoundedAndNeverCutsAHexPairInHalf) {
  std::vector<uint8_t> payload(60, 0x5A);
  char out[24];
  const size_t len = mm::redactLoginPayloadHex(payload.data(), payload.size(), out, sizeof out);
  EXPECT_LT(len, sizeof out);
  EXPECT_EQ(len % 2, 0u);
  EXPECT_EQ(out[len], '\0');
  EXPECT_EQ(mm::redactLoginPayloadHex(nullptr, 0, out, sizeof out), 0u);
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
