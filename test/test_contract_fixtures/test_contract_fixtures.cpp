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

TEST(WaitingFixtures, TheyAreTheirTasksLineByteForByte) {
  ASSERT_TRUE(fs::is_directory(ahead_dir())) << ahead_dir();
  const fs::path file = ahead_dir() / "NeighboursResultTotal.json";
  ASSERT_TRUE(fs::exists(file)) << file;
  EXPECT_EQ(read_file(file), std::string(kNeighboursResultTotal))
      << "a waiting fixture is the line its task carries, and nothing else: it is compared with "
         "the copied file byte for byte when the sync brings it";
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
