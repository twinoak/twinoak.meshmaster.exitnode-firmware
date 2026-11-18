// Which application slot this board boots, and the verdict on the one that is running
// (task p1.15-60).
//
// `src/update/Trial.h`'s BootSlots over the ESP-IDF's `app_update` component. **This is the one
// file of this firmware that changes which slot a board boots** - the call that sets the boot
// partition and the two that write the running image's verdict into `otadata` are here and
// nowhere else, and `test/test_flash_writes` fails the build when one of them turns up in another
// file, as it does for the two files that write flash (src/platform/TableFlash.cpp and
// src/platform/OtaSlot.cpp). That is why this header, like theirs, names none of them.
//
// It is also where the Arduino core is told to keep its hands off the verdict. `initArduino`
// marks a pending image valid **before `setup` runs** unless `verifyRollbackLater()` says
// otherwise (cores/esp32/esp32-hal-misc.c, under CONFIG_APP_ROLLBACK_ENABLE, which is the core's
// own default and so this firmware's). The definition in this file is the one that says
// otherwise, so the three conditions of the trial - and nothing else - decide whether a new
// image stays.

#pragma once

#include "update/Trial.h"

namespace mm {

// This board's two application slots. One object, because there is one pair.
update::BootSlots& applicationSlots();

}  // namespace mm
