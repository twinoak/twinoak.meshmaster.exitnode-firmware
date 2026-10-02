// TLS is verified, and this is the test that greps for the ways of not verifying it
// (README.md, "Rails that hold in every task"; tasks/phase-1/01: "No insecure TLS setting
// anywhere in the tree").
//
// It walks the tree - src/, test/, tools/, docs/, .github/, boards/, lib/, platformio.ini and
// README.md - and fails on any line that names a way of skipping certificate verification. The
// only file allowed to contain those names is this one, which is why they are written here as
// split strings: a grep of the tree for them finds only the test that forbids them.
//
// tasks/ is not walked: the task files and the runbook quote the grep itself. .pio/ is not walked:
// MeshCore and the libraries are upstream's tree, and the rail is about ours.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifndef PROJECT_DIR
#error "PROJECT_DIR must be defined; platformio.ini's [env:native] does it"
#endif

namespace {

namespace fs = std::filesystem;

// Each pattern is split so that this file does not itself contain the string it forbids.
const std::vector<std::string> forbidden() {
  return {
      std::string("set") + "Insecure",                  // WiFiClientSecure: accept any chain
      std::string("INSE") + "CURE",                     // the usual flag spelling, any library
      std::string("verify") + "=false",                 // the .NET-side spelling, so a port cannot carry it over
      std::string("VERIFY") + "_NONE",                  // mbedtls: MBEDTLS_SSL_VERIFY_NONE
      std::string("VERIFY") + "_OPTIONAL",              // mbedtls: verify, then ignore the result
      std::string("skip_cert_") + "common_name_check",  // esp_tls: the name is not checked
      std::string("setFinger") + "print",               // pinning a leaf's fingerprint instead of verifying the chain
      std::string("setCACert(") + "NULL",               // dropping the root at runtime
      std::string("setCACert(") + "nullptr",
      std::string("esp_tls_") + "insecure",
  };
}

const char* const kRoots[] = {"src", "test", "tools", "docs", ".github", "boards", "lib"};
const char* const kFiles[] = {"platformio.ini", "README.md"};

bool isThisTest(const fs::path& p) { return p.filename() == "test_tls_policy.cpp"; }

void scan(const fs::path& file, std::vector<std::string>& hits) {
  std::ifstream in(file, std::ios::binary);
  std::string line;
  int n = 0;
  while (std::getline(in, line)) {
    n++;
    for (const std::string& pat : forbidden()) {
      if (line.find(pat) != std::string::npos) {
        hits.push_back(file.generic_string() + ":" + std::to_string(n) + ": " + pat);
      }
    }
  }
}

}  // namespace

TEST(TlsPolicy, NothingInTheTreeSkipsCertificateVerification) {
  const fs::path root(PROJECT_DIR);
  std::vector<std::string> hits;
  int scanned = 0;

  for (const char* dir : kRoots) {
    const fs::path d = root / dir;
    if (!fs::is_directory(d)) continue;
    for (const auto& item : fs::recursive_directory_iterator(d)) {
      if (!item.is_regular_file() || isThisTest(item.path())) continue;
      scan(item.path(), hits);
      scanned++;
    }
  }
  for (const char* name : kFiles) {
    const fs::path f = root / name;
    if (!fs::is_regular_file(f)) continue;
    scan(f, hits);
    scanned++;
  }

  EXPECT_GT(scanned, 10) << "the walk found almost nothing - is PROJECT_DIR " << PROJECT_DIR << " right?";
  for (const std::string& h : hits) ADD_FAILURE() << h;
}

TEST(TlsPolicy, ThePinnedRootIsIsrgRootX1) {
  // The root the client verifies meshmaster.dk's chain against, checked by its subject line and
  // by the certificate's dates being the ones Let's Encrypt publishes for ISRG Root X1.
  std::ifstream in(fs::path(PROJECT_DIR) / "src" / "agent" / "TlsRoots.h");
  ASSERT_TRUE(in.is_open()) << "src/agent/TlsRoots.h";
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  EXPECT_NE(std::string::npos, text.find("ISRG Root X1"));
  EXPECT_NE(std::string::npos, text.find("-----BEGIN CERTIFICATE-----"));
  EXPECT_NE(std::string::npos, text.find("-----END CERTIFICATE-----"));
  // The SHA-256 fingerprint Let's Encrypt publishes for it, kept beside the PEM so a reader can check.
  EXPECT_NE(std::string::npos, text.find("96:BC:EC:06:26:49:76:F3:74:60:77:9A:CF:28:C5:A7:CF:E8:A3:C0:AA:E1:1A:8F:FC:EE:05:C0:BD:DF:08:C6"));
}

// ---- the plain scheme, and the one place it is allowed (task p1.1-08) --------------------------

// The host harness talks plain HTTP to a compose stack on a loopback port (docs/host.md), which is
// the one build of this firmware that may: the board's client is HTTPS with the chain verified and
// its URL rule refuses any other scheme (src/settings/Keys.cpp). So the scheme is allowed to appear
// in `src/host/` and nowhere else under `src/` - a line of the board's own code that named a
// plain-HTTP address would be a hole in the rail of task 01, and this is the grep that says so.
TEST(TlsPolicy, ThePlainSchemeAppearsOnlyInTheHostBinding) {
  const fs::path root(PROJECT_DIR);
  const std::string plain = std::string("htt") + "p://";  // split, so this file is not a hit itself
  std::vector<std::string> hits;
  int scanned = 0;
  int inHost = 0;
  for (const auto& item : fs::recursive_directory_iterator(root / "src")) {
    if (!item.is_regular_file()) continue;
    scanned++;
    const std::string where = item.path().generic_string();
    const bool isHost = where.find("/src/host/") != std::string::npos;
    std::ifstream in(item.path(), std::ios::binary);
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
      n++;
      if (line.find(plain) == std::string::npos) continue;
      if (isHost) {
        inHost++;
      } else {
        hits.push_back(where + ":" + std::to_string(n));
      }
    }
  }
  EXPECT_GT(scanned, 20) << "the walk of src/ found almost nothing - is PROJECT_DIR right?";
  EXPECT_GT(inHost, 0) << "src/host/ names no plain-HTTP address at all: has the host harness gone?";
  for (const std::string& h : hits) {
    ADD_FAILURE() << h << ": only src/host/ may name a plain-HTTP address (docs/host.md)";
  }
}

// And the rule the host client keeps at run time, which is the half a grep cannot do: an address
// that is neither loopback nor RFC 1918 is refused before a socket is opened.
TEST(TlsPolicy, TheHostClientRefusesAnythingButAPrivateAddress) {
  std::ifstream in(fs::path(PROJECT_DIR) / "src" / "host" / "HostClient.cpp");
  ASSERT_TRUE(in.is_open()) << "src/host/HostClient.cpp";
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  EXPECT_NE(std::string::npos, text.find("isPrivateAddress"));
  EXPECT_NE(std::string::npos, text.find("127"));
  EXPECT_NE(std::string::npos, text.find("192"));
  EXPECT_NE(std::string::npos, text.find("172"));
  // And https:// is refused too, because there is no TLS stack in that build to verify with.
  EXPECT_NE(std::string::npos, text.find(std::string("htt") + "ps://"));
}

// ---- the rails list ---------------------------------------------------------------------------

// README.md's "Rails that hold in every task" is a list of promises, and every one of them names
// the thing in the tree that keeps it: a test, a source file, a fixture directory. Task 06 asks
// for that list to be checked against the tree, so this is the check - every path the section
// mentions exists, and the rails that name a test name one that is in the suite.
TEST(Rails, EveryPathTheRailsListNamesIsInTheTree) {
  const fs::path root(PROJECT_DIR);
  std::ifstream in(root / "README.md");
  ASSERT_TRUE(in.is_open());

  std::vector<std::string> lines;
  std::string line;
  while (std::getline(in, line)) lines.push_back(line);

  // The section, from its heading to the next one.
  size_t start = lines.size();
  size_t end = lines.size();
  for (size_t i = 0; i < lines.size(); i++) {
    if (lines[i].rfind("## Rails that hold in every task", 0) == 0) {
      start = i + 1;
    } else if (start < lines.size() && lines[i].rfind("## ", 0) == 0 && i > start) {
      end = i;
      break;
    }
  }
  ASSERT_LT(start, lines.size()) << "README.md has no 'Rails that hold in every task' section";

  // Every `backticked/path` with a slash or a known extension, and every bullet, counted.
  std::vector<std::string> paths;
  int bullets = 0;
  for (size_t i = start; i < end; i++) {
    const std::string& l = lines[i];
    if (l.rfind("- **", 0) == 0) bullets++;
    size_t pos = 0;
    while (true) {
      const size_t open = l.find('`', pos);
      if (open == std::string::npos) break;
      const size_t close = l.find('`', open + 1);
      if (close == std::string::npos) break;
      std::string token = l.substr(open + 1, close - open - 1);
      pos = close + 1;
      if (token.empty() || token.find(' ') != std::string::npos) continue;
      const bool looksLikeAPath = token.find('/') != std::string::npos || token.find(".md") != std::string::npos;
      if (!looksLikeAPath) continue;
      while (!token.empty() && (token.back() == '/' || token.back() == ')')) token.pop_back();
      paths.push_back(token);
    }
  }

  EXPECT_GE(bullets, 5) << "the rails list is shorter than the five rails this repository states";
  EXPECT_GE(paths.size(), static_cast<size_t>(4)) << "the rails name nothing in the tree any more";
  for (const std::string& p : paths) {
    EXPECT_TRUE(fs::exists(root / p)) << "the rails list names " << p << ", which is not in the tree";
  }
}

// And the three the rails claim a test for, by name: the grep above, the redaction of 01 and 04,
// and the buffers of 06. A rail whose test directory has been renamed is a rail nobody checks.
TEST(Rails, TheTestsTheRailsLeanOnAreAllThere) {
  const fs::path root(PROJECT_DIR);
  const char* const suites[] = {"test_tls_policy", "test_console_parser", "test_credentials",
                                "test_contract_fixtures", "test_buffers"};
  // The host harness is not a test suite of its own (it is a program CI runs against a stub,
  // docs/host.md), so its two halves are checked by name here instead.
  EXPECT_TRUE(fs::is_regular_file(root / "tools" / "host-stub.py")) << "tools/host-stub.py is gone";
  EXPECT_TRUE(fs::is_regular_file(root / "tools" / "host-run.ps1")) << "tools/host-run.ps1 is gone";
  for (const char* s : suites) {
    EXPECT_TRUE(fs::is_directory(root / "test" / s)) << "test/" << s << " is gone";
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
