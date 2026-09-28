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

// PlatformIO's googletest integration links no gtest_main, so every test file brings its own -
// the pattern of MeshCore's own test/ at the pin.
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
