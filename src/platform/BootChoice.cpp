// THE ONE PLACE THAT CHANGES WHICH SLOT THIS BOARD BOOTS. See BootChoice.h, and
// test/test_flash_writes.

#include "BootChoice.h"

#include <esp_image_format.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

#include "OtaSlot.h"

namespace mm {

namespace {

update::SlotState stateOf(esp_ota_img_states_t state) {
  switch (state) {
    case ESP_OTA_IMG_NEW:
      return update::SlotState::New;
    case ESP_OTA_IMG_PENDING_VERIFY:
      return update::SlotState::Pending;
    case ESP_OTA_IMG_VALID:
      return update::SlotState::Valid;
    case ESP_OTA_IMG_INVALID:
      return update::SlotState::Invalid;
    case ESP_OTA_IMG_ABORTED:
      return update::SlotState::Aborted;
    case ESP_OTA_IMG_UNDEFINED:
      return update::SlotState::Undefined;
  }
  return update::SlotState::Unknown;
}

class Esp32BootSlots final : public update::BootSlots {
 public:
  // The same answer, and the same sentence, as the slot that is written: a board with no second
  // application slot has no update of any kind, and one line says so in both places
  // (src/platform/OtaSlot.h).
  bool available() const override { return otherApplicationSlot().available() && runningSlot() >= 0; }

  const char* why() const override {
    if (!otherApplicationSlot().available()) return otherApplicationSlot().why();
    if (runningSlot() < 0) {
      return "this board is not running from an application slot the framework can name, so nothing is switched";
    }
    return otherApplicationSlot().why();
  }

  int runningSlot() const override {
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running == nullptr || running->type != ESP_PARTITION_TYPE_APP) return -1;
    const int slot = static_cast<int>(running->subtype) - static_cast<int>(ESP_PARTITION_SUBTYPE_APP_OTA_MIN);
    if (slot < 0 || slot > 1) return -1;  // a factory image, or a slot beyond the two here
    return slot;
  }

  update::SlotState runningState() const override {
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running == nullptr) return update::SlotState::Unknown;
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (esp_ota_get_state_partition(running, &state) != ESP_OK) return update::SlotState::Unknown;
    return stateOf(state);
  }

  bool describeOther(update::SlotImage& out) const override {
    out = update::SlotImage();
    const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
    if (next == nullptr) return false;
    // The framework's own reading of the image's header and segments: whether it would load it,
    // and how long it is. The silent form, because a slot that holds nothing would otherwise log
    // a page of errors at every boot (components/bootloader_support/src/esp_image_format.c).
    const esp_partition_pos_t pos = {next->address, next->size};
    esp_image_metadata_t data = {};
    if (esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &pos, &data) != ESP_OK) return true;
    out.verifies = true;
    out.length = data.image_len;
    return true;
  }

  bool bootOther() override {
    const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
    if (next == nullptr) return false;
    // The framework verifies the image before it writes the otadata and answers
    // ESP_ERR_OTA_VALIDATE_FAILED for one it would not load, so a half-written slot cannot
    // become the next to boot (components/app_update/esp_ota_ops.c).
    return esp_ota_set_boot_partition(next) == ESP_OK;
  }

  bool markValid() override { return esp_ota_mark_app_valid_cancel_rollback() == ESP_OK; }

  bool markInvalidAndRestart() override {
    // This does not return when it worked: it writes the running image's otadata as invalid,
    // which makes its entry the one the bootloader will not choose, and restarts. It answers
    // ESP_ERR_OTA_ROLLBACK_FAILED when there is nothing in the other slot to go back to.
    return esp_ota_mark_app_invalid_rollback_and_reboot() == ESP_OK;
  }
};

Esp32BootSlots the_slots;

}  // namespace

update::BootSlots& applicationSlots() { return the_slots; }

}  // namespace mm

// The Arduino core marks a pending image valid before `setup` runs unless this says otherwise
// (cores/esp32/esp32-hal-misc.c, `initArduino`, where it is a weak symbol). It says otherwise:
// what an image has to show before it is marked valid is the trial's three conditions, in
// src/update/Trial.h, and the core is in no position to know any of them this early. Nothing
// else of the core's rollback handling runs then - `verifyOta()` is only reached through this.
extern "C" bool verifyRollbackLater() { return true; }
