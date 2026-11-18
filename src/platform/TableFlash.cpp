// THE ONE PLACE THAT ERASES OR WRITES RAW FLASH.
//
// Nothing else in src/ or lib/ may call esp_flash_erase_region, esp_flash_write or any of their
// cousins; test/test_flash_writes walks the tree on every native run and fails when a second file
// does. The reason is the sdkconfig key this file needs: the pinned ESP-IDF refuses a write that
// lands in the bootloader, the partition table or the running application
// (components/spi_flash/partition.c, esp_partition_main_flash_region_safe) and, as the Arduino
// core configures it, *aborts* the board rather than returning an error
// (CONFIG_SPI_FLASH_DANGEROUS_WRITE_ABORTS). Writing the partition table means turning that guard
// off for the whole image (CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED, tools/check-sdkconfig.py's
// allowed list), so what the guard used to do is done here instead, in one file a reviewer can
// read in a minute:
//
//   * the address and the length are constants of this file, not parameters of anything;
//   * a caller names an offset inside the sector and a length, never an address;
//   * every call is refused unless it lies wholly inside that one sector;
//   * nothing here erases or writes anything else, and there is no second function that could.
//
// The sector is the partition table's own, 4 kB at 0x8000: CONFIG_PARTITION_TABLE_OFFSET in
// sdkconfig.defaults, and PARTITION_TABLE_SIZE in the ESP-IDF's gen_esp32part.py. Task p1.15-57.

#include "TableFlash.h"

#include <esp_flash.h>

#include <cstddef>
#include <cstdint>

namespace mm {

namespace {

// The one address and the one length this file may touch. CONFIG_PARTITION_TABLE_OFFSET is 0x8000
// on both boards (sdkconfig.heltec_v3 ~L151, sdkconfig.heltec_v4), and the ESP-IDF reads one
// sector there.
constexpr uint32_t kTableAddress = 0x8000;
constexpr uint32_t kTableLength = static_cast<uint32_t>(kTableSectorLen);

bool insideTheSector(size_t at, size_t len) {
  return len > 0 && at < kTableLength && len <= kTableLength - at;
}

class Esp32TableFlash final : public TableFlash {
 public:
  bool read(size_t at, unsigned char* into, size_t len) override {
    if (into == nullptr || !insideTheSector(at, len)) return false;
    return esp_flash_read(nullptr, into, kTableAddress + static_cast<uint32_t>(at), static_cast<uint32_t>(len)) == ESP_OK;
  }

  bool eraseSector() override {
    return esp_flash_erase_region(nullptr, kTableAddress, kTableLength) == ESP_OK;
  }

  bool write(size_t at, const unsigned char* from, size_t len) override {
    if (from == nullptr || !insideTheSector(at, len)) return false;
    return esp_flash_write(nullptr, from, kTableAddress + static_cast<uint32_t>(at), static_cast<uint32_t>(len)) == ESP_OK;
  }
};

Esp32TableFlash the_table_flash;

}  // namespace

TableFlash& partitionTableFlash() { return the_table_flash; }

}  // namespace mm
