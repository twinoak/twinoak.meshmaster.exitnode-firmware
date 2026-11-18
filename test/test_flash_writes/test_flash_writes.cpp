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
// So this walks src/ and lib/ and fails when a file that is not the one named for a call holds
// it. It is the shape of test/test_tls_policy, for the same reason: a rail with a test rather
// than a promise. test/ is not walked - this file names every call it forbids - and neither is
// .pio/, which is upstream's tree.
//
// **Since task p1.15-59 the rail names a file per call rather than one file for all of them.**
// Fetching a release means writing the other application slot, which is the `app_update`
// component's business and not raw flash: `esp_ota_begin` refuses the running partition and
// anything that is not an application slot, a guard that stays on with
// CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED set. So there are two files, and a call of one of them
// turning up in the other is as much a failure as it turning up anywhere else:
//
//   src/platform/TableFlash.cpp   the raw-flash calls, bounded to the partition table's sector
//   src/platform/OtaSlot.cpp      the OTA writer, bounded to the partition that is not running
//
// `esp_ota_set_boot_partition` is in the list and in **no** file: switching to a fetched image is
// task 60's, and until it is written nothing here may change what the board boots.

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

// The two files that may hold one of these calls, as the walk reports them.
const char* const kTheFlashFile = "src/platform/TableFlash.cpp";
const char* const kTheSlotFile = "src/platform/OtaSlot.cpp";

struct Forbidden {
  const char* call;
  const char* theOneFile;  // "" for a call no file of this firmware may hold at all
};

// Every ESP-IDF way of erasing or writing flash, or a partition, or an application image, and the
// one file of src/ and lib/ that may hold it. A name that is a prefix of another is listed once:
// the walk reports files, not matches.
const std::vector<Forbidden>& forbidden() {
  static const std::vector<Forbidden> names = {
      // the esp_flash API, which src/platform/TableFlash.cpp uses
      {"esp_flash_erase_region", kTheFlashFile},
      {"esp_flash_erase_chip", kTheFlashFile},
      {"esp_flash_write", kTheFlashFile},  // and _encrypted
      // the legacy spi_flash API of the same component
      {"spi_flash_erase_sector", kTheFlashFile},
      {"spi_flash_erase_range", kTheFlashFile},
      {"spi_flash_write", kTheFlashFile},  // and _encrypted
      // the rom routines the ESP-IDF's own guard does not even cover
      {"esp_rom_spiflash_erase", kTheFlashFile},
      {"esp_rom_spiflash_write", kTheFlashFile},
      // by partition rather than by address: src/platform/OtaSlot.cpp erases the first sector of
      // the slot that is not running, which is how a refused image is made unbootable
      {"esp_partition_erase_range", kTheSlotFile},
      {"esp_partition_write", kTheSlotFile},  // and _raw
      // the OTA writer, which writes a whole application slot
      {"esp_ota_begin", kTheSlotFile},
      {"esp_ota_write", kTheSlotFile},
      // and the one that switches to it, which is task 60's and is in no file yet
      {"esp_ota_set_boot_partition", ""},
      // and NVS, which erases its partition
      {"nvs_flash_erase", ""},
  };
  return names;
}

// The calls `file` holds that it may not: for each, the call and the file that should have had it.
std::vector<std::string> callsItMayNotHold(const fs::path& file, const std::string& asReported) {
  std::vector<std::string> bad;
  std::ifstream in(file, std::ios::binary);
  std::string line;
  while (std::getline(in, line)) {
    for (const Forbidden& f : forbidden()) {
      if (line.find(f.call) == std::string::npos) continue;
      if (asReported == f.theOneFile) continue;
      const std::string note = asReported + " calls " + f.call + " (" +
                               (f.theOneFile[0] == '\0' ? std::string("no file of this firmware may")
                                                        : std::string("only ") + f.theOneFile + " may") +
                               ")";
      if (std::find(bad.begin(), bad.end(), note) == bad.end()) bad.push_back(note);
    }
  }
  return bad;
}

// Every call under `roots` that is in a file that may not hold it, sorted. `scanned` counts what
// was looked at, so that a walk that found nothing because it looked nowhere is seen.
std::vector<std::string> callsOutOfPlace(const fs::path& root, const std::vector<std::string>& roots, int& scanned) {
  std::vector<std::string> hits;
  scanned = 0;
  for (const std::string& dir : roots) {
    const fs::path d = root / dir;
    if (!fs::is_directory(d)) continue;
    for (const auto& item : fs::recursive_directory_iterator(d)) {
      if (!item.is_regular_file()) continue;
      scanned++;
      const std::string as = fs::relative(item.path(), root).generic_string();
      for (const std::string& note : callsItMayNotHold(item.path(), as)) hits.push_back(note);
    }
  }
  std::sort(hits.begin(), hits.end());
  return hits;
}

}  // namespace

TEST(FlashWrites, EveryCallThatErasesOrWritesFlashIsInTheOneFileNamedForIt) {
  int scanned = 0;
  const std::vector<std::string> hits = callsOutOfPlace(fs::path(PROJECT_DIR), {"src", "lib"}, scanned);

  EXPECT_GT(scanned, 20) << "the walk found almost nothing - is PROJECT_DIR " << PROJECT_DIR << " right?";
  EXPECT_EQ(std::vector<std::string>{}, hits)
      << "a file of src/ or lib/ holds a call that erases or writes flash and is not the file named for it. With "
         "CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED set (tools/check-sdkconfig.py) nothing stops a raw call landing "
         "in the running application or the bootloader, so each of these belongs in one file and nowhere else.";
}

TEST(FlashWrites, BothFilesReallyAreThereAndHoldTheirOwnCalls) {
  for (const char* file : {kTheFlashFile, kTheSlotFile}) {
    EXPECT_TRUE(fs::is_regular_file(fs::path(PROJECT_DIR) / file))
        << file << " is gone: the test above would then pass by finding nothing at all";
  }
  // And each really is the caller, so that the rail is not held by two empty files.
  int scanned = 0;
  const std::vector<std::string> flash = callsOutOfPlace(fs::path(PROJECT_DIR), {"src/platform"}, scanned);
  EXPECT_EQ(std::vector<std::string>{}, flash);
  std::ifstream a(fs::path(PROJECT_DIR) / kTheFlashFile, std::ios::binary);
  const std::string flashText((std::istreambuf_iterator<char>(a)), std::istreambuf_iterator<char>());
  EXPECT_NE(std::string::npos, flashText.find("esp_flash_write")) << kTheFlashFile;
  std::ifstream b(fs::path(PROJECT_DIR) / kTheSlotFile, std::ios::binary);
  const std::string slotText((std::istreambuf_iterator<char>(b)), std::istreambuf_iterator<char>());
  EXPECT_NE(std::string::npos, slotText.find("esp_ota_write")) << kTheSlotFile;
  EXPECT_NE(std::string::npos, slotText.find("esp_partition_erase_range")) << kTheSlotFile;
}

// Task 60's call, in no file of this firmware: nothing yet may change what the board boots.
TEST(FlashWrites, NothingSwitchesTheBootPartitionYet) {
  int scanned = 0;
  const std::vector<std::string> hits = callsOutOfPlace(fs::path(PROJECT_DIR), {"src", "lib"}, scanned);
  for (const std::string& note : hits) {
    EXPECT_EQ(std::string::npos, note.find("esp_ota_set_boot_partition")) << note;
  }
}

// The check's own test: six planted files, and the walk must report exactly the four that are
// wrong - a second caller, a call in the wrong one of the two named files, and task 60's call
// anywhere at all. A walk that reported fewer would let one into the tree unnoticed.
TEST(FlashWrites, TheWalkFindsEveryCallThatIsInTheWrongFile) {
  const fs::path planted = fs::temp_directory_path() / "mm-flash-writes-selftest";
  fs::remove_all(planted);
  fs::create_directories(planted / "src" / "platform");
  fs::create_directories(planted / "lib" / "agent-core" / "src");

  {
    std::ofstream a(planted / "src" / "platform" / "TableFlash.cpp");
    a << "// the one file for raw flash, and it may hold these\n"
      << "bool write() { return esp_flash_write(nullptr, b, 0x8000, 16) == ESP_OK; }\n"
      << "bool erase() { return esp_flash_erase_region(nullptr, 0x8000, 0x1000) == ESP_OK; }\n"
      << "// ... but not the slot's:\n"
      << "bool oops() { return esp_ota_write(handle, data, len) == ESP_OK; }\n";
    std::ofstream b(planted / "src" / "platform" / "OtaSlot.cpp");
    b << "// the one file for the slot, and it may hold these\n"
      << "bool open() { return esp_ota_begin(p, 0, &h) == ESP_OK; }\n"
      << "bool put() { return esp_ota_write(h, d, n) == ESP_OK; }\n"
      << "bool spoil() { return esp_partition_erase_range(p, 0, 4096) == ESP_OK; }\n"
      << "// ... but not raw flash, and not the boot choice:\n"
      << "bool no() { return esp_ota_set_boot_partition(p) == ESP_OK; }\n";
    std::ofstream c(planted / "src" / "Something.cpp");
    c << "void oops() { esp_partition_erase_range(p, 0, 4096); }\n";
    std::ofstream d(planted / "lib" / "agent-core" / "src" / "Else.cpp");
    d << "void alsoOops() { esp_ota_write(handle, data, len); }\n";
    std::ofstream e(planted / "src" / "Innocent.cpp");
    e << "void fine() { esp_flash_read(nullptr, b, 0x8000, 16); }\n";
    std::ofstream f(planted / "src" / "AlsoInnocent.cpp");
    f << "void fine() { esp_partition_read(p, 0, b, 16); }\n";
  }

  int scanned = 0;
  const std::vector<std::string> hits = callsOutOfPlace(planted, {"src", "lib"}, scanned);
  EXPECT_EQ(6, scanned);
  const std::vector<std::string> want = {
      "lib/agent-core/src/Else.cpp calls esp_ota_write (only src/platform/OtaSlot.cpp may)",
      "src/Something.cpp calls esp_partition_erase_range (only src/platform/OtaSlot.cpp may)",
      "src/platform/OtaSlot.cpp calls esp_ota_set_boot_partition (no file of this firmware may)",
      "src/platform/TableFlash.cpp calls esp_ota_write (only src/platform/OtaSlot.cpp may)",
  };
  EXPECT_EQ(want, hits) << "the walk must see every planted call that is in the wrong file, and leave the two "
                           "readers and the two files' own calls alone";

  fs::remove_all(planted);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
