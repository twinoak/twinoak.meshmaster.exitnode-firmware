# 57 — A second slot

**Depends on:** —
**Spec:** `boards/README.md` (the whole page: why `nvs` moved, what does not move, why `app1` went), `boards/partitions/meshmaster_8MB.csv` and `meshmaster_16MB.csv`, `platformio.ini` (~L174–181 and ~L279–285: the table each board is built with); `tools/check-partitions.py` (~L55 and ~L63: the framework's defaults; `check_table` ~L142; the merged image's end ~L195) and `tests/fixtures/partitions/`; `tools/pio/merge_bin.py`; `sdkconfig.defaults` and `sdkconfig.heltec_v3` (~L68: `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`; ~L145–152: the table's offset and MD5; ~L1704–1706: `CONFIG_SPI_FLASH_DANGEROUS_WRITE_ABORTS`), `tools/check-sdkconfig.py` (the allowed differences, each with its reason); `.github/workflows/ci.yml` (~L136–159); `README.md` (*Flashing it* ~L163–245, *Rails that hold in every task* ~L643), `docs/field.md` §9 (~L318), `docs/console.md`; `src/console/`, `src/platform/`, `src/settings/Store.cpp`; `test/test_tls_policy` (a native test that greps the tree). In the pinned ESP-IDF and Arduino core, on disk after a build: `components/spi_flash` (what a write to the partition table's sector needs), `components/app_update` (`esp_ota_get_next_update_partition`), `components/partition_table` (`gen_esp32part.py`, and the table's own format with its MD5 entry).
**Size:** one session, a long one.
**Model:** claude-opus-5

## Goal

This firmware cannot be updated from the server, because a board has one place for an
application. The second was taken out of the table on purpose in October: *app1 is gone:
this firmware does no OTA*. Its flash was left unallocated: `0x345000` to `0x66FFFF` on the
V3 and `0x655000` to `0xC8FFFF` on the V4. The application is 1.43 MB.

Thomas, 18 November: owners have asked for updates started from meshmaster.dk. That needs
the second slot back.

A board gets its table from a merged image. The MeshCore web flasher erases the whole board
before it writes one, which costs the key, the token and the WiFi. So the boards in the field are not asked to flash a merged image: they
take this firmware as the plain `.bin`, which keeps everything, and **the firmware adds the
slot itself**, when a person asks for it. That is one sector of 4 kB at `0x8000`, erased and
written. A power cut in that moment leaves a board that needs a merged image over USB.

This task is the table, knowing which table a board holds, and adding the slot. Nothing here
fetches or switches firmware.

## Deliverables

1. **The two tables gain `app1`**, an `ota_1` application slot in the flash that was left
   unallocated, on a 64 kB boundary and ending where `spiffs` begins:

   | | offset | size |
   |---|---|---|
   | V3, `meshmaster_8MB.csv` | `0x350000` | `0x320000` |
   | V4, `meshmaster_16MB.csv` | `0x660000` | `0x630000` |

   No other line of either table changes by a byte: `otadata`, `app0`, `nvs`, `spiffs` and
   `coredump` keep their offsets and sizes. The comments in the files and `boards/README.md`
   say what is now true.
2. **The tables boards hold today are kept as files of their own**, one for each board,
   beside the new ones: the firmware has to know them by their bytes.
3. **`tools/check-partitions.py` holds the new tables**: every rule it has; `app1` where
   deliverable 1 puts it, inside what was unallocated, overlapping nothing; and the
   application image fits the smaller of the two slots. Its self-test gains a table for each
   new rule, in `tests/fixtures/partitions/`.
4. **The firmware knows which table it runs on**, one of three words:
   - `two`: the table has a second application slot. That is the new table, and the
     framework's default, which a board provisioned under `v0.1.1` or earlier and never
     flashed with a merged image since still holds;
   - `one`: the sector at `0x8000` holds, byte for byte, the table of deliverable 2 for this
     board;
   - `other`: anything else.
5. **Adding the slot.** One function. Only when the word is `one`. It writes the sector at
   `0x8000` with this build's own new table, the bytes the same build's merged image has
   there, reads the sector back and compares. It reports *added* or what it read. It
   restarts nothing: the new table is read at the next boot.
   The pinned ESP-IDF refuses such a write as configured (`CONFIG_SPI_FLASH_DANGEROUS_WRITE_ABORTS`).
   The report says what it needs, and the one key changed is in `tools/check-sdkconfig.py`'s
   allowed list with its reason.
6. **One place may write there.** A native test reads the tree, as `test/test_tls_policy`
   does: the calls that write or erase raw flash or a partition are in that one file and in
   no other of `src/` and `lib/`. The file's own address and length are constants.
7. **On the console**, for the bench and the field:
   - `update` prints the word, and the two slots' offsets when there are two;
   - `update slot` adds the slot and says to `reboot`. With `two` it says the slot is there;
     with `other` it refuses in a sentence.
8. **CI holds the bytes.** For each board: the table the firmware would write is the merged
   image's bytes at `0x8000`, and the table it knows as `one` is what
   `gen_esp32part.py` makes of the file of deliverable 2.
9. **The words.** `boards/README.md`. `README.md`, *Flashing it*: the merged image now
   carries two slots; a board upgraded with the plain `.bin` keeps its table until a person
   asks for the slot. `docs/field.md` §9. `docs/console.md`. The rail of deliverable 6 in
   `README.md`'s list. One line in `release-notes/unreleased/`.
10. **For the bench**, as ⚠ items in `docs/field.md`, because no test here can write a
    board's table. On a V3 and on a V4, each provisioned and running the release before
    this one, upgraded with this task's plain `.bin`:
    - `update` says `one`; `status` shows the identity's 64 characters;
    - `update slot`, then `reboot`: `update` says `two` with `app1`'s offset;
    - `status` shows the same 64 characters, the WiFi comes up, and the exit node's page on
      meshmaster.dk shows a heartbeat with the token it had;
    - a board flashed with this task's merged image through `esptool` says `two` at once.

## Acceptance criteria

- [ ] `tools/check-partitions.py` passes both new tables against a merged image of each board, and its self-test refuses: an `app1` that overlaps `nvs`, one that overlaps `spiffs`, one off the 64 kB boundary, and an application longer than the smaller slot (tests).
- [ ] The old table's file for each board differs from the new one by the `app1` line and by nothing else (test).
- [ ] Against a flash scripted to hold the old table: the word is `one`. The new table: `two`. The framework's default: `two`. The old table with one byte changed: `other` (tests).
- [ ] Adding the slot on `one`: the sector then holds the new table byte for byte, and one erase and one write of that sector happened and no other (test).
- [ ] Adding it on `two` and on `other`: nothing is erased or written (tests).
- [ ] A flash scripted to read back one wrong byte: the function reports what it read and does not say *added* (test).
- [ ] The native test of deliverable 6 fails when a second file calls one of those functions (shown by its own self-test).
- [ ] CI's check of deliverable 8 is green for both boards, and red for a table file edited by one byte (shown in the report).
- [ ] `tools/check-sdkconfig.py` green, with the one key allowed.
- [ ] `pio test -e native` green, both boards build, CI green.

## Out of scope

Fetching, judging or switching a firmware (59, 60), and the server asking (61): after this
task a board can have a second slot and nothing uses it. Writing the bootloader, `otadata`
or anything else outside the application slots. A board whose table is `other`. The web
flasher. Flash encryption and secure boot, which this firmware leaves to the owner and which
would refuse this write.
