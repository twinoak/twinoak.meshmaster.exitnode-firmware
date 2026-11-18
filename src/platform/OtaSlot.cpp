// THE ONE PLACE THAT WRITES AN APPLICATION SLOT. See OtaSlot.h, and test/test_flash_writes.

#include "OtaSlot.h"

#include <esp_ota_ops.h>
#include <esp_partition.h>

#include <cstdio>
#include <cstring>

#include "PartitionTable.h"
#include "TableBytes.h"
#include "TableFlash.h"

namespace mm {

namespace {

// One flash sector, which is what `spoil()` erases. 4 kB on every ESP32 this firmware runs on;
// written here rather than reached for through a framework header, because it is also the figure
// src/platform/PartitionTable.h holds for the table's own sector.
constexpr uint32_t kSectorBytes = 0x1000;

class Esp32OtaSlot final : public update::SlotSink {
 public:
  bool available() const override {
    look();
    return part_ != nullptr;
  }

  const char* why() const override {
    look();
    return why_;
  }

  uint32_t capacity() const override {
    look();
    return part_ == nullptr ? 0 : part_->size;
  }

  bool begin(uint32_t expected) override {
    look();
    if (part_ == nullptr || expected == 0 || expected > part_->size) return false;
    closeHandle();
    // OTA_WITH_SEQUENTIAL_WRITES, and not the file's length: with the length, esp_ota_begin()
    // erases every sector the image will fill before it returns - some nine seconds of flash
    // erase for 1.4 MB, in one call, with the mesh and the console waiting on it. With sequential
    // writes each esp_ota_write() erases only the sectors it is about to fill, so the erase is
    // spread over the ranges and no single piece of this fetch holds the loop for long
    // (components/app_update/esp_ota_ops.c: `need_erase`).
    if (esp_ota_begin(part_, OTA_WITH_SEQUENTIAL_WRITES, &handle_) != ESP_OK) return false;
    open_ = true;
    held_ = 0;
    expected_ = expected;
    return true;
  }

  uint32_t held() const override { return held_; }

  bool write(const uint8_t* data, size_t len) override {
    if (!open_ || data == nullptr || len == 0) return false;
    if (held_ + len > expected_) return false;  // never more than the manifest promised
    if (esp_ota_write(handle_, data, len) != ESP_OK) return false;
    held_ += static_cast<uint32_t>(len);
    return true;
  }

  bool restart() override {
    if (!open_) return false;
    const uint32_t expected = expected_;
    closeHandle();
    return begin(expected);
  }

  bool finish() override {
    if (!open_) return false;
    // esp_ota_end() is the framework's own verdict: it runs esp_image_verify() over the partition
    // and answers ESP_ERR_OTA_VALIDATE_FAILED for an image it would not load. It does **not**
    // change the boot choice - the call that does is in src/platform/BootChoice.cpp and in no
    // other file of this firmware (test/test_flash_writes keeps it that way).
    const esp_err_t err = esp_ota_end(handle_);
    open_ = false;
    held_ = 0;
    return err == ESP_OK;
  }

  bool spoil() override {
    look();
    closeHandle();
    if (part_ == nullptr) return false;
    // The first sector holds the image header the bootloader reads. Erased, there is nothing
    // there that anything will boot, whatever lies above it.
    return esp_partition_erase_range(part_, 0, kSectorBytes) == ESP_OK;
  }

  bool read(uint32_t offset, uint8_t* out, uint32_t len) const override {
    look();
    if (part_ == nullptr || out == nullptr || len == 0) return false;
    if (offset > part_->size || len > part_->size - offset) return false;
    return esp_partition_read(part_, offset, out, len) == ESP_OK;
  }

 private:
  void closeHandle() {
    if (!open_) return;
    esp_ota_abort(handle_);
    open_ = false;
    held_ = 0;
  }

  // Which partition, and why there is none. Looked up once: a board's table does not change while
  // it runs, and the one thing that can write it (`update slot`) says to reboot.
  void look() const {
    if (looked_) return;
    looked_ = true;
    why_ = "this board has no second application slot";

    const TablePair tables = tablesForThisBoard();
    const TableReading r = readTable(partitionTableFlash(), tables.oneSlot);
    if (!r.read) {
      why_ = "this board's partition table could not be read, so nothing is claimed about its slots";
      return;
    }
    if (r.kind == TableKind::One) {
      why_ = "this board's table has one application slot; type `update slot` to add the second, then `reboot`";
      return;
    }
    if (r.kind == TableKind::Other) {
      why_ =
          "this board's table is not one this firmware recognises; flash the release's merged image over USB with "
          "esptool";
      return;
    }

    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
    if (next == nullptr || next == running) {
      // The sector says `two` and this boot does not have it: the table was written by
      // `update slot` and the board has not been restarted since. The table is read at boot.
      why_ = "the table has a second slot and this boot has not read it yet; type `reboot` first";
      return;
    }
    part_ = next;
    static char line[96];
    std::snprintf(line, sizeof(line), "app1 at 0x%06lX, 0x%06lX bytes",
                  static_cast<unsigned long>(next->address), static_cast<unsigned long>(next->size));
    why_ = line;
  }

  mutable bool looked_ = false;
  mutable const esp_partition_t* part_ = nullptr;
  mutable const char* why_ = "";
  esp_ota_handle_t handle_ = 0;
  bool open_ = false;
  uint32_t held_ = 0;
  uint32_t expected_ = 0;
};

Esp32OtaSlot the_slot;

}  // namespace

update::SlotSink& otherApplicationSlot() { return the_slot; }

}  // namespace mm
