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

namespace {
int countOf(const std::string& text, const std::string& needle) {
  int n = 0;
  for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + needle.size())) n++;
  return n;
}
}  // namespace

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

TEST(TlsPolicy, TheBrokersRootIsIsrgRootX2AndTheCarriedRootsAreExactlyTheTwo) {
  // Task p1.7-27: the two observer brokers' chains (mqtt.meshview.dk, mqtt.meshmapper.net) are
  // Let's Encrypt's ECDSA hierarchy, anchored at ISRG Root X2 and served cross-signed by X1. The
  // header carries X2 beside X1 with its subject, its validity and the fingerprint Let's Encrypt
  // publishes - and no third root: the two hosts are named beside the evidence, so a reader can
  // repeat the `openssl s_client` the report quotes.
  std::ifstream in(fs::path(PROJECT_DIR) / "src" / "agent" / "TlsRoots.h");
  ASSERT_TRUE(in.is_open()) << "src/agent/TlsRoots.h";
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  EXPECT_NE(std::string::npos, text.find("CN=ISRG Root X2"));
  EXPECT_NE(std::string::npos, text.find("valid until 2040-09-17"));
  EXPECT_NE(std::string::npos, text.find("69:72:9B:8E:15:A8:6E:FC:17:7A:57:AF:B7:17:1D:FC:64:AD:D2:8C:2F:CA:8C:F1:50:7E:34:45:3C:CB:14:70"));
  EXPECT_NE(std::string::npos, text.find("mqtt.meshview.dk:443"));
  EXPECT_NE(std::string::npos, text.find("mqtt.meshmapper.net:443"));
  EXPECT_EQ(2, countOf(text, "-----BEGIN CERTIFICATE-----")) << "exactly two roots are carried";
  EXPECT_EQ(2, countOf(text, "-----END CERTIFICATE-----"));
  EXPECT_NE(std::string::npos, text.find("kCarriedRootsPem[] = MM_ISRG_ROOT_X1_PEM MM_ISRG_ROOT_X2_PEM"));
  // The PEM bodies are the two roots and nothing else: X1's is RSA (MIIFaz...), X2's ECDSA (MIICGz...).
  EXPECT_NE(std::string::npos, text.find("MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw"));
  EXPECT_NE(std::string::npos, text.find("MIICGzCCAaGgAwIBAgIQQdKd0XLq7qeAwSxs6S+HUjAKBggqhkjOPQQDAzBPMQsw"));
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

// And the ones the rails claim a test for, by name: the grep above, the redaction of 01 and 04,
// the buffers of 06 and the server's answers of p1.2-10. A rail whose test directory has been
// renamed is a rail nobody checks.
TEST(Rails, TheTestsTheRailsLeanOnAreAllThere) {
  const fs::path root(PROJECT_DIR);
  const char* const suites[] = {"test_tls_policy",        "test_console_parser", "test_credentials",
                                "test_contract_fixtures", "test_buffers",        "test_server_policy",
                                "test_observer",          "test_connection"};
  for (const char* s : suites) {
    EXPECT_TRUE(fs::is_directory(root / "test" / s)) << "test/" << s << " is gone";
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
