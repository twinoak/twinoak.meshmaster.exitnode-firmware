// The contract fixtures are a copy, and this is the half of the rail that `pio test -e native`
// can see: every file MANIFEST.txt lists is here, is the size that was recorded, and is a JSON
// document; and nothing is in the directory that the manifest does not know about. The other half
// is tools/sync-fixtures.ps1 -Verify, which checks the SHA-256 of every file and which CI runs on
// every commit - hashing does not belong in a firmware test, but "the file somebody edited by
// hand is a different size" is caught here too.
//
// From F-01 on, the tests that round-trip the agent's envelopes read the same files.

#include <gtest/gtest.h>

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef FIXTURES_DIR
#error "FIXTURES_DIR must be defined; platformio.ini's [env:native] does it"
#endif
#ifndef AHEAD_FIXTURES_DIR
#error "AHEAD_FIXTURES_DIR must be defined; platformio.ini's [env:native] does it"
#endif

namespace {

namespace fs = std::filesystem;

struct Entry {
  std::string sha256;
  std::uintmax_t size = 0;
  std::string name;
};

struct Manifest {
  std::string commit;
  std::vector<Entry> entries;
};

bool is_hex(const std::string& s, std::size_t len) {
  if (s.size() != len) return false;
  for (char c : s) {
    const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    if (!ok) return false;
  }
  return true;
}

fs::path fixtures_dir() { return fs::path(FIXTURES_DIR); }
fs::path ahead_dir() { return fs::path(AHEAD_FIXTURES_DIR); }

// "# source: <repo>/<path> at commit <sha>" and, for every file, "<sha256>  <bytes>  <name>".
Manifest read_manifest() {
  Manifest m;
  std::ifstream in(fixtures_dir() / "MANIFEST.txt");
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const std::string marker = "at commit ";
    if (line.rfind("# source:", 0) == 0) {
      const auto at = line.find(marker);
      if (at != std::string::npos) m.commit = line.substr(at + marker.size());
      continue;
    }
    if (line.empty() || line[0] == '#') continue;
    std::istringstream fields(line);
    Entry e;
    fields >> e.sha256 >> e.size;
    std::getline(fields >> std::ws, e.name);
    if (!e.name.empty()) m.entries.push_back(e);
  }
  return m;
}

std::string read_file(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream all;
  all << in.rdbuf();
  return all.str();
}

// Not a parser: enough to say "this is a JSON document and not a fetch that went wrong" - the
// brackets outside strings balance and the whole of it is one object or array.
bool looks_like_json(const std::string& text) {
  std::size_t i = 0;
  while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) i++;
  if (i >= text.size() || (text[i] != '{' && text[i] != '[')) return false;

  int depth = 0;
  bool in_string = false;
  bool escaped = false;
  for (; i < text.size(); i++) {
    const char c = text[i];
    if (in_string) {
      if (escaped) { escaped = false; continue; }
      if (c == '\\') { escaped = true; continue; }
      if (c == '"') in_string = false;
      continue;
    }
    switch (c) {
      case '"': in_string = true; break;
      case '{':
      case '[': depth++; break;
      case '}':
      case ']':
        depth--;
        if (depth < 0) return false;
        break;
      default: break;
    }
  }
  return depth == 0 && !in_string;
}

}  // namespace

TEST(ContractFixtures, ManifestNamesTheMeshmasterCommitItCameFrom) {
  ASSERT_TRUE(fs::is_directory(fixtures_dir())) << fixtures_dir();
  ASSERT_TRUE(fs::exists(fixtures_dir() / "MANIFEST.txt"))
      << "run tools/sync-fixtures.ps1 - the fixtures are a copy, never written by hand";

  const Manifest m = read_manifest();
  EXPECT_TRUE(is_hex(m.commit, 40))
      << "MANIFEST.txt must record the full meshmaster.dk commit, not '" << m.commit << "'";
}

TEST(ContractFixtures, EveryListedFileIsHereAndIsTheRecordedSize) {
  const Manifest m = read_manifest();
  for (const Entry& e : m.entries) {
    const fs::path file = fixtures_dir() / e.name;
    ASSERT_TRUE(fs::exists(file)) << e.name << " is in MANIFEST.txt but not on disk";
    EXPECT_EQ(e.size, fs::file_size(file))
        << e.name << " is not the size MANIFEST.txt recorded; change it in meshmaster.dk and sync";
    EXPECT_TRUE(is_hex(e.sha256, 64)) << e.name << ": MANIFEST.txt holds no SHA-256";
    EXPECT_TRUE(looks_like_json(read_file(file))) << e.name << " is not a JSON document";
  }
}

TEST(ContractFixtures, NothingInTheDirectoryIsMissingFromTheManifest) {
  const Manifest m = read_manifest();
  for (const auto& item : fs::directory_iterator(fixtures_dir())) {
    if (!item.is_regular_file() || item.path().extension() != ".json") continue;
    const std::string name = item.path().filename().string();
    bool listed = false;
    for (const Entry& e : m.entries) {
      if (e.name == name) { listed = true; break; }
    }
    EXPECT_TRUE(listed) << name << " is in the directory but not in MANIFEST.txt - a fixture that "
                                   "was not copied by tools/sync-fixtures.ps1";
  }
}

// ---------------------------------------------------------------------------------------------
// The fixtures that wait (task p1.15-56)
//
// `tests/fixtures/contracts-ahead/` is **not** a copy: each file is an envelope written here from
// the line of the task that needs it, because the two repositories' piles run at the same time
// and meshmaster.dk had not put it on its master. `tools/sync-fixtures.ps1` does not touch the
// directory and MANIFEST.txt does not list it. What this suite can hold it to is the bytes: they
// are the task's line exactly, so that when the sync brings the real file the two can be
// compared byte for byte and a wire the two ends do not agree on is found before any release.

// The one line of task p1.15-56, which is meshmaster.dk's task 239 deliverable 1, and the one
// trailing newline every fixture of the copied tree has.
const char* const kNeighboursResultTotal =
    "{\"neighbours\":[{\"prefix\":\"a1\",\"lastHeardS\":42,\"snr\":7.25},"
    "{\"prefix\":\"7f\",\"lastHeardS\":3600,\"snr\":-2.5}],\"total\":17}\n";

// The five lines of task p1.15-61, which are meshmaster.dk's task 244's: the heartbeat's `update`
// member, a heartbeat that carries it, the ask in a reply's `update`, a reply that carries it, and
// the one result.
const char* const kHeartbeatUpdate = "{\"table\":\"one\",\"id\":17,\"step\":\"proving\"}\n";

const char* const kAgentHeartbeatUpdate =
    "{\"agentVersion\":\"0.3.1\",\"transport\":\"onboard\",\"at\":\"2026-09-23T14:00:00Z\","
    "\"companionPublicKey\":\"a1b2\",\"companionFirmware\":\"v1.17.1\","
    "\"radio\":{\"freqMHz\":869.618,\"bwKHz\":62.5,\"sf\":8,\"cr\":8},"
    "\"jobTypes\":[\"status\",\"telemetry\",\"neighbours\",\"config.read\",\"config.apply\",\"cli\","
    "\"trace\",\"discover\",\"advert.request\",\"clock.sync\",\"password.set\",\"acl.set\",\"reboot\"],"
    "\"budget\":{\"maxTransactionsPerMinute\":2,\"minGapSeconds\":20},\"companionConnected\":true,"
    "\"queueDepth\":0,\"errors\":{\"uplink\":0},\"board\":\"heltec_v3\",\"nextCheckInS\":3600,"
    "\"maxTransactionsThisWake\":12,\"update\":{\"table\":\"one\",\"id\":17,\"step\":\"proving\"}}\n";

const char* const kUpdateAsk =
    "{\"id\":17,\"releaseId\":9,\"product\":\"twinoak-exitnode-firmware\",\"version\":\"0.8.0\","
    "\"base\":\"/agent/v1/firmware/9\","
    "\"expectedSha256\":\"3b1f0c4e5d6a79880817a6b5c4d3e2f1a0b9c8d7e6f5041322314051607f8e9d\","
    "\"size\":1432000}\n";

const char* const kHeartbeatReplyUpdate =
    "{\"serverTime\":\"2026-09-23T14:00:01Z\","
    "\"budget\":{\"maxTransactionsPerMinute\":2,\"minGapSeconds\":20},\"pollIntervalS\":30,"
    "\"jobsWaiting\":false,\"update\":{\"id\":17,\"releaseId\":9,"
    "\"product\":\"twinoak-exitnode-firmware\",\"version\":\"0.8.0\","
    "\"base\":\"/agent/v1/firmware/9\","
    "\"expectedSha256\":\"3b1f0c4e5d6a79880817a6b5c4d3e2f1a0b9c8d7e6f5041322314051607f8e9d\","
    "\"size\":1432000}}\n";

const char* const kUpdateResult =
    "{\"outcome\":\"rolledBack\",\"fromVersion\":\"0.7.2\",\"toVersion\":\"0.7.2\","
    "\"refusal\":\"check\",\"check\":4,"
    "\"journal\":[{\"at\":\"2026-11-20T09:00:00Z\",\"step\":\"check\","
    "\"note\":\"table one, 0.7.2 running\"},"
    "{\"at\":\"2026-11-20T09:00:01Z\",\"step\":\"adding-slot\"},"
    "{\"at\":\"2026-11-20T09:00:40Z\",\"step\":\"fetching\",\"note\":\"0.8.0, 1432000 bytes\"},"
    "{\"at\":\"2026-11-20T09:01:10Z\",\"step\":\"switching\"},"
    "{\"at\":\"2026-11-20T09:01:30Z\",\"step\":\"proving\",\"note\":\"mesh up, wifi up\"},"
    "{\"at\":\"2026-11-20T09:11:30Z\",\"step\":\"rolling-back\","
    "\"note\":\"no heartbeat answered in 10 min\"},"
    "{\"at\":\"2026-11-20T09:11:50Z\",\"step\":\"restored\",\"note\":\"0.7.2 running\"}]}\n";

struct Waiting {
  const char* name;
  const char* const line;
};

const Waiting kWaiting[] = {
    {"NeighboursResultTotal.json", kNeighboursResultTotal},
    {"HeartbeatUpdate.json", kHeartbeatUpdate},
    {"AgentHeartbeatUpdate.json", kAgentHeartbeatUpdate},
    {"UpdateAsk.json", kUpdateAsk},
    {"HeartbeatReplyUpdate.json", kHeartbeatReplyUpdate},
    {"UpdateResult.json", kUpdateResult},
};

TEST(WaitingFixtures, TheyAreTheirTasksLineByteForByte) {
  ASSERT_TRUE(fs::is_directory(ahead_dir())) << ahead_dir();
  for (const Waiting& w : kWaiting) {
    const fs::path file = ahead_dir() / w.name;
    ASSERT_TRUE(fs::exists(file)) << file;
    EXPECT_EQ(read_file(file), std::string(w.line))
        << w.name
        << ": a waiting fixture is the line its task carries, and nothing else - it is compared "
           "with the copied file byte for byte when the sync brings it";
  }
}

// Every file in the directory is one of the lines above: a waiting fixture that no test holds to
// its task's line would be a wire nobody checked.
TEST(WaitingFixtures, NoneOfThemIsUnaccountedFor) {
  for (const auto& item : fs::directory_iterator(ahead_dir())) {
    if (!item.is_regular_file() || item.path().extension() != ".json") continue;
    const std::string name = item.path().filename().string();
    bool held = false;
    for (const Waiting& w : kWaiting) {
      if (name == w.name) { held = true; break; }
    }
    EXPECT_TRUE(held) << name << " is waiting and no test holds it to the line of the task that wrote it";
  }
}

// The three nested pairs of this task's lines, which is the rule the fixtures' README states: a
// member that is a file of its own appears inside the envelope that carries it, byte for byte.
TEST(WaitingFixtures, TheNestedOnesAreHeldVerbatimByTheirEnvelope) {
  const std::string member = std::string(kHeartbeatUpdate);
  const std::string heartbeat = std::string(kAgentHeartbeatUpdate);
  const std::string ask = std::string(kUpdateAsk);
  const std::string reply = std::string(kHeartbeatReplyUpdate);
  EXPECT_NE(std::string::npos, heartbeat.find(member.substr(0, member.size() - 1)));
  EXPECT_NE(std::string::npos, reply.find(ask.substr(0, ask.size() - 1)));

  // And the heartbeat is the copied AgentHeartbeat.json with the member appended, which is what
  // makes the two one sample rather than two agents.
  const std::string copied = read_file(fixtures_dir() / "AgentHeartbeat.json");
  ASSERT_FALSE(copied.empty());
  std::string body = copied;
  while (!body.empty() && (body.back() == '\n' || body.back() == '\r')) body.pop_back();
  ASSERT_FALSE(body.empty());
  EXPECT_EQ(body.substr(0, body.size() - 1), heartbeat.substr(0, body.size() - 1))
      << "AgentHeartbeatUpdate.json is no longer AgentHeartbeat.json plus the update member";
}

TEST(WaitingFixtures, NoneOfThemIsInTheCopiedTreeOrItsManifest) {
  const Manifest m = read_manifest();
  for (const auto& item : fs::directory_iterator(ahead_dir())) {
    if (!item.is_regular_file() || item.path().extension() != ".json") continue;
    const std::string name = item.path().filename().string();
    EXPECT_TRUE(looks_like_json(read_file(item.path()))) << name << " is not a JSON document";
    for (const Entry& e : m.entries) {
      EXPECT_NE(e.name, name) << name << " is waiting and is also in MANIFEST.txt: the sync has "
                                         "brought it, so point the tests at the copied tree and "
                                         "delete the waiting one (the pile's FOLLOWUPS.md)";
    }
    EXPECT_FALSE(fs::exists(fixtures_dir() / name))
        << name << " is in the copied tree now; the waiting one has had its day";
  }
}

// PlatformIO's googletest integration links no gtest_main, so every test file brings its own -
// the pattern of MeshCore's own test/ at the pin.
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
