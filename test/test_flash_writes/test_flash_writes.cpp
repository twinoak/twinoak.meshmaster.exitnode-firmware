// One place may erase or write raw flash, and this is the test that says which (task p1.15-57,
// README.md's "Rails that hold in every task").
//
// Adding a board's second application slot means writing the partition table's own sector, which
// the pinned ESP-IDF refuses and, as configured, aborts the board over
// (CONFIG_SPI_FLASH_DANGEROUS_WRITE_ABORTS). Turning that guard off is a per-image choice -
// CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED, in tools/check-sdkconfig.py's allowed list - so from
// that commit on, *any* call in this firmware that erases or writes flash lands wherever it is
// told, the running application and the bootloader included. What the guard used to do is done by
// one file instead: src/platform/TableFlash.cpp, which holds the address and the length as
// constants of its own and refuses anything outside that one sector.
//
// So this walks src/ and lib/ and fails when a second file of either holds one of those calls.
// It is the shape of test/test_tls_policy, for the same reason: a rail with a test rather than a
// promise. test/ is not walked - this file names every call it forbids - and neither is .pio/,
// which is upstream's tree.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifndef PROJECT_DIR
#error "PROJECT_DIR must be defined; platformio.ini's [env:native] does it"
#endif

namespace {

namespace fs = std::filesystem;

// The one file that may hold them, as the walk reports it.
const char* const kTheOneFile = "src/platform/TableFlash.cpp";

// Every ESP-IDF way of erasing or writing flash, or a partition, or an application image. A name
// that is a prefix of another is listed once: the walk reports files, not matches.
const std::vector<std::string>& forbidden() {
  static const std::vector<std::string> names = {
      // the esp_flash API, which src/platform/TableFlash.cpp uses
      "esp_flash_erase_region",
      "esp_flash_erase_chip",
      "esp_flash_write",  // and _encrypted
      // the legacy spi_flash API of the same component
      "spi_flash_erase_sector",
      "spi_flash_erase_range",
      "spi_flash_write",  // and _encrypted
      // the rom routines the ESP-IDF's own guard does not even cover
      "esp_rom_spiflash_erase",
      "esp_rom_spiflash_write",
      // by partition rather than by address
      "esp_partition_erase_range",
      "esp_partition_write",  // and _raw
      // the OTA writer, which writes a whole application slot and switches to it
      "esp_ota_begin",
      "esp_ota_write",
      "esp_ota_set_boot_partition",
      // and NVS, which erases its partition
      "nvs_flash_erase",
  };
  return names;
}

bool holdsOne(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  std::string line;
  while (std::getline(in, line)) {
    for (const std::string& name : forbidden()) {
      if (line.find(name) != std::string::npos) return true;
    }
  }
  return false;
}

// Every file under `roots` that holds one, as a path relative to `root`, sorted. `scanned` counts
// what was looked at, so that a walk that found nothing because it looked nowhere is seen.
std::vector<std::string> filesThatCallOne(const fs::path& root, const std::vector<std::string>& roots, int& scanned) {
  std::vector<std::string> hits;
  scanned = 0;
  for (const std::string& dir : roots) {
    const fs::path d = root / dir;
    if (!fs::is_directory(d)) continue;
    for (const auto& item : fs::recursive_directory_iterator(d)) {
      if (!item.is_regular_file()) continue;
      scanned++;
      if (holdsOne(item.path())) hits.push_back(fs::relative(item.path(), root).generic_string());
    }
  }
  std::sort(hits.begin(), hits.end());
  return hits;
}

}  // namespace

TEST(FlashWrites, OnlyOneFileOfSrcAndLibErasesOrWritesRawFlash) {
  int scanned = 0;
  const std::vector<std::string> hits = filesThatCallOne(fs::path(PROJECT_DIR), {"src", "lib"}, scanned);

  EXPECT_GT(scanned, 20) << "the walk found almost nothing - is PROJECT_DIR " << PROJECT_DIR << " right?";
  const std::vector<std::string> want = {kTheOneFile};
  EXPECT_EQ(want, hits) << "a second file of src/ or lib/ erases or writes flash. With "
                           "CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED set (tools/check-sdkconfig.py) nothing stops such "
                           "a call landing in the running application or the bootloader, so it belongs in "
                        << kTheOneFile << " and nowhere else.";
}

TEST(FlashWrites, TheOneFileReallyIsThere) {
  EXPECT_TRUE(fs::is_regular_file(fs::path(PROJECT_DIR) / kTheOneFile))
      << kTheOneFile << " is gone: the test above would then pass by finding nothing at all";
}

// The check's own test: two planted files, and the walk must report both. A walk that reported
// one, or none, would let a second caller into the tree unnoticed.
TEST(FlashWrites, TheWalkFindsASecondCallerWhenThereIsOne) {
  const fs::path planted = fs::temp_directory_path() / "mm-flash-writes-selftest";
  fs::remove_all(planted);
  fs::create_directories(planted / "src" / "platform");
  fs::create_directories(planted / "lib" / "agent-core" / "src");

  {
    std::ofstream a(planted / "src" / "platform" / "TableFlash.cpp");
    a << "// the one file\n"
      << "bool write() { return esp_flash_write(nullptr, b, 0x8000, 16) == ESP_OK; }\n";
    std::ofstream b(planted / "src" / "Something.cpp");
    b << "void oops() { esp_partition_erase_range(p, 0, 4096); }\n";
    std::ofstream c(planted / "lib" / "agent-core" / "src" / "Else.cpp");
    c << "void alsoOops() { esp_ota_write(handle, data, len); }\n";
    std::ofstream d(planted / "src" / "Innocent.cpp");
    d << "void fine() { esp_flash_read(nullptr, b, 0x8000, 16); }\n";
  }

  int scanned = 0;
  const std::vector<std::string> hits = filesThatCallOne(planted, {"src", "lib"}, scanned);
  EXPECT_EQ(4, scanned);
  const std::vector<std::string> want = {"lib/agent-core/src/Else.cpp", "src/Something.cpp", "src/platform/TableFlash.cpp"};
  EXPECT_EQ(want, hits) << "the walk must see both planted callers, and leave the reader alone";
  EXPECT_NE(want, std::vector<std::string>{kTheOneFile}) << "which is what the test above would then refuse";

  fs::remove_all(planted);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
