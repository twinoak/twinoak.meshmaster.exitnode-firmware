// The other application slot, as task 59's fetch writes it.
//
// `src/update/Fetch.h`'s SlotSink over the ESP-IDF's `app_update` component: the partition that is
// not running, opened for a write, written in order, read back for checks 4 and 5, and - when any
// of them refuses - made unbootable where it lies by erasing its first sector.
//
// **This is the second file of src/ and lib/ that erases or writes flash**, and the only one that
// may call the framework's OTA writer: `test/test_flash_writes` names a file per call and fails
// when one of them turns up anywhere else - which is why this header, like TableFlash.h, names
// none of them. The reason the rail needs two files rather than one is that this component's
// calls are bounded by a *partition* and the other file's are bounded by an *address*: opening a
// write refuses the running partition and anything that is not an application slot
// (components/app_update/esp_ota_ops.c, ESP-IDF 4.4.7 as pinned), a guard that stays on with
// CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED set and that this file adds nothing to.
//
// What is **not** here: switching to the image, and the trial (task p1.15-60). The call that
// changes which slot a board boots is in src/platform/BootChoice.cpp and in no other file of this
// firmware, and the rail's own list says so.

#pragma once

#include "update/Fetch.h"

namespace mm {

// This board's other application slot. One object, because there is one slot and one fetch.
update::SlotSink& otherApplicationSlot();

}  // namespace mm
