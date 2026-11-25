// A name of its own on the network, and the rail that keeps the MAC the chip's (task p1.22-74).
//
// An owner of two boards reported on 22 November 2026 that one of them had a new address, and so a
// new IP, after an update. The answer given was that nothing in this firmware sets or reads a MAC;
// the first part of this file is the grep that keeps that answer true, in the shape of
// test/test_flash_writes - a rail with a test rather than a promise, planted files and all.
//
// The second part is the name. A board calls itself `meshmaster-` and the last four hex digits of
// its station MAC, lower case, and `status` prints the name beside the address. The two words come
// from src/net/Hostname.h, and the last tests in this file read src/net/WifiLink.cpp to check
// *where* the name is handed over: at the Arduino core this repository pins, the only call that
// gives a name to the station's netif - and so to the DHCP request - is the first
// `WiFi.mode(WIFI_STA)`, so a `setHostname` after it is a name the network never hears.
//
// src/ and lib/ are walked; test/ is not, because this file names every call it forbids, and
// neither is .pio/, which is upstream's tree.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "net/Hostname.h"

#ifndef PROJECT_DIR
#error "PROJECT_DIR must be defined; platformio.ini's [env:native] does it"
#endif

namespace {

namespace fs = std::filesystem;

// ---- the rail: nothing of ours sets a MAC -----------------------------------------------------

// Every way the ESP-IDF 4.4.7 this repository builds against, and the Arduino core on top of it,
// lets a program choose an address instead of using the chip's. Reads are not here and are fine:
// `esp_read_mac`, `esp_efuse_mac_get_default` and `esp_wifi_get_mac` are how src/net/WifiLink.cpp
// learns the address it prints and builds the name from.
//
//   esp_base_mac_addr_set     the base address every interface's is derived from - the one call
//                             that would make a board come up as another board entirely
//   esp_iface_mac_addr_set    the same, per interface (the ESP-IDF 5 name, forbidden here so that
//                             a pin bump cannot bring it in unnoticed)
//   esp_wifi_set_mac          the station's own, set on the driver
//   esp_netif_set_mac         the netif's, which is what lwIP and so ARP would answer with
//   ETH_CMD_S_MAC_ADDR        the Ethernet driver's setter, through esp_eth_ioctl
//   setMacAddress             any Arduino-core wrapper for one of the above, now or at a later
//                             core
//
// A raw eFuse write (`esp_efuse_write_field_blob` over MAC_FACTORY) is not listed: the pattern
// would be a whole eFuse API rather than a MAC call, and this firmware writes no eFuse at all -
// which the absence of `esp_efuse_write` anywhere in src/ or lib/ says on its own.
const std::vector<std::string>& macSetters() {
  static const std::vector<std::string> names = {
      "esp_base_mac_addr_set", "esp_iface_mac_addr_set", "esp_wifi_set_mac",
      "esp_netif_set_mac",     "ETH_CMD_S_MAC_ADDR",     "setMacAddress",
  };
  return names;
}

// Every file under `roots` that names one of those calls, as `path: call`, sorted. `scanned`
// counts what was looked at, so that a walk that found nothing because it looked nowhere is seen.
std::vector<std::string> macSettersUnder(const fs::path& root, const std::vector<std::string>& roots, int& scanned) {
  std::vector<std::string> hits;
  scanned = 0;
  for (const std::string& dir : roots) {
    const fs::path d = root / dir;
    if (!fs::is_directory(d)) continue;
    for (const auto& item : fs::recursive_directory_iterator(d)) {
      if (!item.is_regular_file()) continue;
      scanned++;
      const std::string as = fs::relative(item.path(), root).generic_string();
      std::ifstream in(item.path(), std::ios::binary);
      std::string line;
      int n = 0;
      while (std::getline(in, line)) {
        n++;
        for (const std::string& call : macSetters()) {
          if (line.find(call) == std::string::npos) continue;
          hits.push_back(as + ":" + std::to_string(n) + ": " + call);
        }
      }
    }
  }
  std::sort(hits.begin(), hits.end());
  return hits;
}

// ---- the name ---------------------------------------------------------------------------------

std::string nameOf(const uint8_t (&mac)[6]) {
  char out[mm::kHostnameCap] = {0};
  mm::hostnameFromMac(mac, out, sizeof out);
  return std::string(out);
}

std::string textOf(const uint8_t (&mac)[6]) {
  char out[mm::kMacTextCap] = {0};
  mm::macToText(mac, out, sizeof out);
  return std::string(out);
}

std::string fileText(const std::string& relative) {
  std::ifstream in(fs::path(PROJECT_DIR) / relative, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

size_t occurrences(const std::string& haystack, const std::string& needle) {
  size_t n = 0;
  for (size_t at = haystack.find(needle); at != std::string::npos; at = haystack.find(needle, at + 1)) n++;
  return n;
}

// One function of a source file, from its signature to the closing brace in the first column, as
// test/test_loop_stack reads one of src/main.cpp. Empty when there is no such function, which
// every caller asserts on: a renamed function must not quietly pass these tests.
std::string functionBody(const std::string& text, const std::string& signature) {
  const size_t at = text.find(signature);
  if (at == std::string::npos) return std::string();
  const size_t end = text.find("\n}\n", at);
  return end == std::string::npos ? text.substr(at) : text.substr(at, end - at);
}

}  // namespace

TEST(MacPolicy, NothingUnderSrcOrLibSetsAMac) {
  int scanned = 0;
  const std::vector<std::string> hits = macSettersUnder(fs::path(PROJECT_DIR), {"src", "lib"}, scanned);

  EXPECT_GT(scanned, 20) << "the walk found almost nothing - is PROJECT_DIR " << PROJECT_DIR << " right?";
  EXPECT_EQ(std::vector<std::string>{}, hits)
      << "a file of src/ or lib/ sets a MAC address. A board's address on the network is the chip's own, burnt into "
         "its eFuse, and docs/field.md tells an owner to reserve an address by it - so an address this firmware "
         "chose would be an address that changes with a firmware, which is exactly what was reported on "
         "22 November 2026 and answered as impossible.";
}

// The rail's own test: four planted files, and the walk must report exactly the three calls that
// set an address and leave the reads alone.
TEST(MacPolicy, ThePlantedSettersAreAllSeenAndTheReadsAreNot) {
  const fs::path planted = fs::temp_directory_path() / "mm-mac-policy-selftest";
  fs::remove_all(planted);
  fs::create_directories(planted / "src" / "net");
  fs::create_directories(planted / "lib" / "agent-core" / "src");
  {
    std::ofstream a(planted / "src" / "net" / "WifiLink.cpp");
    a << "// the reads this firmware really does, and none of them is a setter:\n"
      << "void fine() { esp_read_mac(mac, ESP_MAC_WIFI_STA); esp_wifi_get_mac(WIFI_IF_STA, mac); }\n";
    std::ofstream b(planted / "src" / "net" / "Rogue.cpp");
    b << "void oops() { esp_base_mac_addr_set(mac); }\n"
      << "void alsoOops() { esp_wifi_set_mac(WIFI_IF_STA, mac); }\n";
    std::ofstream c(planted / "lib" / "agent-core" / "src" / "Else.cpp");
    c << "void libraryOops() { WiFi.setMacAddress(mac); }\n";
    std::ofstream d(planted / "src" / "Innocent.cpp");
    d << "void fine() { esp_efuse_mac_get_default(mac); }\n";
  }

  int scanned = 0;
  const std::vector<std::string> hits = macSettersUnder(planted, {"src", "lib"}, scanned);
  EXPECT_EQ(4, scanned);
  const std::vector<std::string> want = {
      "lib/agent-core/src/Else.cpp:1: setMacAddress",
      "src/net/Rogue.cpp:1: esp_base_mac_addr_set",
      "src/net/Rogue.cpp:2: esp_wifi_set_mac",
  };
  EXPECT_EQ(want, hits) << "the walk must see every planted setter and leave the three reads alone";

  fs::remove_all(planted);
}

TEST(TheName, IsMeshmasterAndTheLastFourHexDigitsOfTheStationMac) {
  // The three addresses of the task: one ordinary Espressif station MAC, one whose last two bytes
  // have leading zeros (the case a `%x` would print as `meshmaster-f` and a router would show
  // beside a different board), and one at the top of the range, to show the digits are lower case.
  const uint8_t ordinary[6] = {0x34, 0x85, 0x18, 0x1a, 0x2b, 0x3c};
  const uint8_t zeros[6] = {0x7c, 0xdf, 0xa1, 0x42, 0x00, 0x0f};
  const uint8_t top[6] = {0xf4, 0x12, 0xfa, 0x99, 0xff, 0xee};

  EXPECT_EQ("meshmaster-2b3c", nameOf(ordinary));
  EXPECT_EQ("meshmaster-000f", nameOf(zeros));
  EXPECT_EQ("meshmaster-ffee", nameOf(top));
}

TEST(TheName, IsFifteenCharactersAndFitsItsOwnBuffer) {
  const uint8_t top[6] = {0xf4, 0x12, 0xfa, 0x99, 0xff, 0xee};
  EXPECT_EQ(15u, nameOf(top).size());
  // A DHCP hostname may be 63 characters and the ESP-IDF's own limit is 32, so there is room to
  // spare; what matters is that the buffer the board carries holds the longest name exactly.
  EXPECT_EQ(nameOf(top).size() + 1, mm::kHostnameCap);
}

TEST(TheName, IsTheSameNameEveryTimeForOneChip) {
  const uint8_t mac[6] = {0x34, 0x85, 0x18, 0x1a, 0x2b, 0x3c};
  EXPECT_EQ(nameOf(mac), nameOf(mac)) << "nothing is stored and nothing is random: the name follows the chip";
}

TEST(TheName, TwoBoardsThatShareTheFirstFourBytesAreStillTwoNames) {
  const uint8_t one[6] = {0x34, 0x85, 0x18, 0x1a, 0x2b, 0x3c};
  const uint8_t two[6] = {0x34, 0x85, 0x18, 0x1a, 0x2b, 0x3d};
  EXPECT_NE(nameOf(one), nameOf(two));
}

TEST(TheMacText, IsSixLowerCaseOctetsSeparatedByColons) {
  const uint8_t ordinary[6] = {0x34, 0x85, 0x18, 0x1a, 0x2b, 0x3c};
  const uint8_t zeros[6] = {0x7c, 0xdf, 0xa1, 0x42, 0x00, 0x0f};
  const uint8_t top[6] = {0xf4, 0x12, 0xfa, 0x99, 0xff, 0xee};

  EXPECT_EQ("34:85:18:1a:2b:3c", textOf(ordinary));
  EXPECT_EQ("7c:df:a1:42:00:0f", textOf(zeros));
  EXPECT_EQ("f4:12:fa:99:ff:ee", textOf(top));
  EXPECT_EQ(textOf(top).size() + 1, mm::kMacTextCap);
}

TEST(TheName, ASmallBufferIsTruncatedAndNeverUnterminated) {
  const uint8_t mac[6] = {0x34, 0x85, 0x18, 0x1a, 0x2b, 0x3c};
  char small[8] = {'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x'};
  mm::hostnameFromMac(mac, small, sizeof small);
  EXPECT_EQ("meshmas", std::string(small));
  char one[1] = {'x'};
  mm::hostnameFromMac(mac, one, sizeof one);
  EXPECT_EQ("", std::string(one));
  char text[4] = {'x', 'x', 'x', 'x'};
  mm::macToText(mac, text, sizeof text);
  EXPECT_EQ("34:", std::string(text));
}

TEST(TheName, NothingAtAllIsAnEmptyStringAndNotACrash) {
  char out[mm::kHostnameCap] = {'x'};
  mm::hostnameFromMac(nullptr, out, sizeof out);
  EXPECT_EQ("", std::string(out));
  mm::macToText(nullptr, out, sizeof out);
  EXPECT_EQ("", std::string(out));
  const uint8_t mac[6] = {0x34, 0x85, 0x18, 0x1a, 0x2b, 0x3c};
  mm::hostnameFromMac(mac, nullptr, 16);  // nothing to write to, and nothing written
  mm::macToText(mac, nullptr, 16);
}

// ---- where the name is handed over ------------------------------------------------------------

// The acceptance criterion of the task: the name is given to the network *before the address is
// asked for*. At the pinned Arduino core (2.0.17) `WiFi.setHostname()` only fills a static buffer;
// `WiFiGenericClass::mode()` is the one call that pushes it onto the station's netif, and it
// returns early when the mode asked for is the mode in force - so the push happens once, at the
// first `WiFi.mode(WIFI_STA)`. These two tests are what stops the two lines being reordered by
// somebody who does not know that, which would leave the board under the framework's own name with
// nothing failing anywhere.
TEST(TheName, IsGivenToTheFrameworkBeforeTheModeIsSet) {
  const std::string text = fileText("src/net/WifiLink.cpp");
  ASSERT_FALSE(text.empty()) << "src/net/WifiLink.cpp could not be read";
  const std::string begin = functionBody(text, "\nvoid WifiLink::begin(SettingsStore& settings) {");
  ASSERT_FALSE(begin.empty()) << "src/net/WifiLink.cpp has no WifiLink::begin(SettingsStore&)";

  const size_t named = begin.find("nameThisBoard()");
  const size_t mode = begin.find("WiFi.mode(");
  ASSERT_NE(std::string::npos, named) << "begin() does not name this board at all";
  ASSERT_NE(std::string::npos, mode) << "begin() does not set the station mode at all";
  EXPECT_LT(named, mode) << "the name must be handed over before the first WiFi.mode(WIFI_STA): that call is the only "
                            "one that puts it on the station's netif, so a name set after it never reaches DHCP";
}

TEST(TheName, IsBuiltFromTheEfuseAndHandedToSetHostname) {
  const std::string text = fileText("src/net/WifiLink.cpp");
  ASSERT_FALSE(text.empty());
  const std::string body = functionBody(text, "\nvoid WifiLink::nameThisBoard() {");
  ASSERT_FALSE(body.empty()) << "src/net/WifiLink.cpp has no WifiLink::nameThisBoard()";

  // The eFuse, not the driver: esp_wifi_get_mac has nothing to answer with before mode() has run.
  EXPECT_NE(std::string::npos, body.find("esp_read_mac(mac, ESP_MAC_WIFI_STA)"));
  EXPECT_NE(std::string::npos, body.find("hostnameFromMac("));
  EXPECT_NE(std::string::npos, body.find("macToText("));
  EXPECT_NE(std::string::npos, body.find("WiFi.setHostname(hostname_)"));
}

// And the other half of *nothing is settable*: the name is in no key of the settings table, so
// there is no `set hostname …` to go wrong and no copy of it on flash to disagree with the chip.
TEST(TheName, IsNoSettingAndIsStoredNowhere) {
  const std::string keys = fileText("src/settings/Keys.cpp");
  ASSERT_FALSE(keys.empty()) << "src/settings/Keys.cpp could not be read";
  EXPECT_EQ(std::string::npos, keys.find("hostname")) << "the name follows the chip; it is not a key";
  const std::string hostname = fileText("src/net/Hostname.cpp");
  ASSERT_FALSE(hostname.empty());
  EXPECT_EQ(std::string::npos, hostname.find("nvs_")) << "nothing about the name touches NVS";
}

// `status`'s WiFi line says both, in every state: the line a person reads before reserving an
// address in a router is read exactly when the link is down.
TEST(TheName, IsOnTheWifiLineOfStatusBesideTheMac) {
  const std::string text = fileText("src/net/WifiLink.cpp");
  ASSERT_FALSE(text.empty());
  const std::string body = functionBody(text, "\nvoid WifiLink::describe(char* out, size_t cap) const {");
  ASSERT_FALSE(body.empty()) << "src/net/WifiLink.cpp has no WifiLink::describe()";
  EXPECT_NE(std::string::npos, body.find("macText_"));
  EXPECT_NE(std::string::npos, body.find("hostname_"));
  // One suffix, written once after the switch, so that no state's line is the one that forgets it
  // - and all four states are still in there to carry it.
  EXPECT_EQ(1u, occurrences(body, ", mac %s, name %s"));
  EXPECT_EQ(4u, occurrences(body, "case State::"));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
