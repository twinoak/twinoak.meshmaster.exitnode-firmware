# 60 — Switched, proved, or back

**Depends on:** 59
**Spec:** task 59's record of what the other slot holds; `sdkconfig.heltec_v3` and `sdkconfig.heltec_v4` (~L68: `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`), `tools/check-sdkconfig.py` (the committed configuration is the Arduino core's own but for the keys it lists, so the core's own bootloader was built with the same); `src/main.cpp` (`setup` ~L650, `loop` ~L752, `radio_ok`), `src/Health.h` and `src/Health.cpp` (the reasons a board restarts, kept in RTC memory; the deliberate restart), `src/platform/Esp32Platform.cpp` (~L20), `src/net/WifiLink.cpp`, `src/net/Clock.cpp`, `src/agent/AgentClient.cpp` (`postHeartbeat` ~L402); `lib/agent-core/include/agent/AgentLoop.h` (what a heartbeat's answer changes); `README.md` (*Flashing it* ~L163–245; the rails ~L643, *It stays up, and says why when it did not*), `docs/field.md` §9, `docs/console.md`. In the pinned ESP-IDF and Arduino core: `components/app_update` (`esp_ota_set_boot_partition`, `esp_ota_get_state_partition`, `esp_ota_mark_app_valid_cancel_rollback`, `esp_ota_mark_app_invalid_rollback_and_reboot`, `esp_ota_get_partition_description`), `components/bootloader_support` (what the bootloader does with an image that is pending), and the core's `esp32-hal-misc.c` (`verifyRollbackLater`: what the core does to a pending image before `setup` runs). In the Walter firmware's repository, read only, as the model: `components/core/include/core/update/self_update.h` (~L24–110: a trial and its three conditions).
**Size:** one session, a long one.
**Model:** claude-opus-5

## Goal

The other slot can hold a judged image (59). This task boots it, and decides whether it
stays.

A firmware that boots is not yet a firmware that works. What an exit node is for is reaching
meshmaster.dk and the mesh; a new image that cannot do that would leave a board that can
never be told to update again. So a switched image runs **on trial**: it has ten minutes to
show three things, and if it does not, the old image runs again without anybody's help.

## Deliverables

1. **The switch.** Only to an image task 59's record calls judged, and only after its hash
   has been read from the slot again and is the record's. The record of the update is
   written first, then the boot choice, then a restart by the firmware's own deliberate
   path, with its reason kept as every restart's is. What waits in RAM for the server,
   results and observations, is posted first, one try.
2. **The trial.** An image booted for the first time after a switch is on trial for ten
   minutes from its boot. It is kept when all three have been seen in that boot:
   - the mesh started;
   - the WiFi joined and the clock was set from the network;
   - a heartbeat was answered `2xx`.

   Then, and at no moment before, the image is marked valid.
3. **Back.** Ten minutes without all three: the image is marked invalid and the board
   restarts into the old one. A reset during the trial, by a crash, the watchdog or a power
   cut, has the same end, through the bootloader. The Arduino core marks a pending image
   valid before `setup` runs unless it is told otherwise; it is told otherwise, and the
   report says where.
4. **Where the bootloader does not undo.** A board whose bootloader came from other firmware
   may not mark an image as pending. The new image sees that at its boot, says so in the
   journal, and goes by the same three conditions and the same ten minutes; when they fail
   it switches back itself and restarts.
5. **The record outlives the restart**, in the settings store: what ran before, what was
   switched to, when, and the steps so far as a journal of `at`, `step` and `note` within
   the bounds of meshmaster.dk's `FirmwareJournalEntry.json` (40 entries, 2 000 characters
   in all). The boot after the trial, whichever image it is, finishes the record:
   *committed*, or *rolled back* with what was missing. It is what task 61 posts.
6. **A flash over USB still wins.** `README.md` tells an owner to upgrade with the plain
   `.bin`, which the web flasher writes to the first slot whatever the board boots from. So
   at boot, when the firmware runs from the second slot and the first holds an image that
   the record does not know, that verifies, and that carries this product's descriptor for
   this board, the firmware switches to it, once, and that image is on trial like any
   other. An image the record knows as tried and failed is not switched to again.
7. **Nothing else is written.** The boot choice through the ESP-IDF's own calls, the other
   slot by task 59, this task's keys in the settings store. The identity and every other
   setting are as they were, and task 57's test of what may write flash still passes.
8. **From the console**, for the bench: `update apply` switches to the judged image and
   restarts. `update` gains the trial's state while there is one (each condition, and the
   minutes left) and the last update's ending. `status` says *on trial* in its first line
   while it is.
9. **The words.** `README.md`: *Flashing it* (which slot a board runs from, and
   deliverable 6), and a rail in its list: a new firmware is kept only when it has reached
   the server. `docs/field.md` §9: what an update looks like on the OLED and the console,
   and what to do with a board that went back. `docs/console.md`. One line in
   `release-notes/unreleased/`.
10. **For the bench**, as ⚠ items in `docs/field.md`, on a board with a second slot, release
    A running and release B served to it:
    - `update fetch`, `update apply`: B runs, `update` shows the three conditions met and
      *committed* inside two minutes, the identity is the same 64 characters, and the exit
      node's page shows B;
    - again from B to a third release with the WiFi's access point switched off before
      `update apply`: ten minutes later the board restarts and B runs; with the WiFi back,
      `update` says *rolled back: no wifi*;
    - again, and the power pulled one minute after the new image starts: at power-up the
      old image runs and says *rolled back*;
    - with the board running from the second slot, the plain `.bin` of any release flashed
      through the web flasher: that release runs after the flash.

## Acceptance criteria

The slots, the boot choice, the clock and the restart are fakes a test scripts.

- [ ] A switch with nothing judged in the record, and one whose slot's hash is no longer the record's: refused, the boot choice unchanged, no restart (tests).
- [ ] A switch: the record is written before the boot choice, and the boot choice before the restart (test reads the fakes' order).
- [ ] On trial with the three conditions met at 30 seconds: marked valid at that moment, and the record ends *committed* (test).
- [ ] Each condition missing alone at ten minutes: marked invalid, a restart, and after it the old image's boot ends the record *rolled back* naming that condition (tests).
- [ ] The image is never marked valid before the third condition, at any order of the three (test over the six orders).
- [ ] A restart at four minutes of a trial, scripted as the bootloader choosing the old image: the record ends *rolled back* with the restart's reason (test).
- [ ] A bootloader scripted as not marking the image pending: the journal says so; the conditions met: *committed*; a condition missing at ten minutes: the boot choice is set back and the board restarts (tests).
- [ ] A journal of 45 steps is kept as 40, within 2 000 characters (test).
- [ ] Running from the second slot with an unknown, valid image of this product and board in the first: switched to, once. With an image of the other board, one that does not verify, and one the record knows as failed: not switched to (tests).
- [ ] Running from the first slot, whatever the second holds: no switch at boot (test).
- [ ] `update apply` with nothing judged refuses in a sentence (test).
- [ ] `pio test -e native` green, both boards build, CI green.

## Out of scope

The server asking and the result's post (61). Updating the bootloader, the table beyond task
57, or anything outside the application slots. Going back to an older image by choice after
a commit. An image of another product. A trial longer or shorter than ten minutes, which is
one constant.
