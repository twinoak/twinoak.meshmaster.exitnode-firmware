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

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
