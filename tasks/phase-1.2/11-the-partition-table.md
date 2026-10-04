# 11 — The partition table

**Depends on:** 10 (it wrote the sentence this task changes).
**Spec:** README.md *Flashing it* and the rails (*the identity is generated on the board and never leaves it*); `platformio.ini`, `boards/heltec_v4.json` (`default_16MB.csv`), the framework's own `tools/partitions/default_8MB.csv` and `default_16MB.csv` as installed under `~/.platformio/packages/framework-arduinoespressif32/`; `.github/workflows/release.yml` (`-t mergebin`, the notes); `src/settings/SettingsStore` (NVS), `IdentityStore(SPIFFS, "/identity")`; `docs/field.md`; finding 3 in the README and the box in `../phase-1/FOLLOWUPS.md`.
**Size:** one session.
**Model:** claude-fable-5-1

## Goal

The merged image erases a provisioned board's settings because
`default_8MB.csv` keeps NVS at `0x9000`, in the gap between the partition
table and `boot_app0` that `esptool`'s merge fills with `0xFF`. Move NVS
above the application on both boards so that a merged image, which ends
where the application ends, can never reach it - and change nothing else
about where anything lives, because the identity in SPIFFS is the one thing
on a board that cannot be re-provisioned.

## Rails, before anything is written

- **Read the two default tables from the installed framework**, not from
  memory and not from this file: the offsets and sizes of `nvs`, `otadata`,
  `app0`, `app1`, `spiffs` and `coredump` for 8 MB and for 16 MB. Copy them
  into the report before changing a byte.
- **`spiffs` keeps its offset and its size on both boards.** The identity
  lives there. A table that moves it by one sector is a table that makes
  every repeater's ACL entry for this board a stranger's key.
- **`app0` keeps its offset (`0x10000`) and its size.** The application
  image and `pio run -t upload` write there; the bootloader's `otadata` at
  `0xE000` keeps pointing at it.
- **The partition labelled `nvs` keeps its label.** `Preferences` finds NVS
  by that label, so the move needs no change in `SettingsStore`.
- **Nothing in this task can be proved on a board**, and a wrong table is
  found only there. Everything below is therefore checked by reading the
  files the build produces, in CI, every build.

## Deliverables

- **Two tables.** `boards/partitions/meshmaster_8MB.csv` and
  `meshmaster_16MB.csv`, each the framework's default with `nvs` moved to
  the start of the region `app1` had (this firmware does no OTA, so `app1`
  goes; say so in the csv's comment) - `nvs` at `app1`'s old offset with
  its default size, and the rest of that region **left unallocated**: not
  given to `spiffs`, whose offset would then move. The gap at
  `0x9000`–`0xDFFF` stays empty. `board_build.partitions` names the file per environment in
  `platformio.ini`; `boards/README.md` says why these exist.
- **A check in CI, every build.** `tools/check-partitions.py`, run by
  `ci.yml`'s build job after `pio run` and `-t mergebin` on each board:
  parses the csv the environment names, asserts `spiffs`, `coredump`,
  `otadata` and `app0` have the framework default's offset and size for that
  flash size, asserts `nvs` begins above `app0`'s end, and asserts the merged
  image's length is below `nvs`'s offset - so the day an application grows
  past the region, CI says so before a release does. The script's own
  tests are three fixture tables, one good and two wrong.
- **What happens on the day.** A board upgraded with the **application
  image** keeps its old table - the ESP32 reads the table from `0x8000` at
  boot and the image does not touch it - so NVS is still at `0x9000` and the
  settings stay; nothing changes for that board until it is next flashed
  merged. A board flashed **merged** gets the new table, finds `nvs` empty
  (or holding whatever the old `app1` region held: `Preferences.begin`
  erases and re-inits on `ESP_ERR_NVS_NO_FREE_PAGES` and
  `ESP_ERR_NVS_NEW_VERSION_FOUND` - verify this in the framework source and
  say so, or make `SettingsStore::begin` do it) and needs WiFi and a token
  again: the token is a new one, *Generate a new token* on the exit node's
  page (meshmaster.dk task 96), then `set meshmaster.token`. The identity
  survives both paths. After this, the merged image never reaches the
  settings again, and there is no reason left to avoid it.
- **The words.** README's *Flashing it* (10's version) is rewritten once
  more: either image keeps the settings and the identity from now on; the
  one exception is the **first merged flash of a release with this table**
  over a board provisioned under the old one, which forgets the settings
  and is the re-provisioning above. `docs/field.md` the same. The release
  notes in `release.yml` carry that paragraph, worded for the release that
  ships the table and generic after it - make it one text that is true for
  both, rather than a version check in a shell script.
- **The build.** `pio run -e heltec_v3`, `-e heltec_v4`, each `-t mergebin`;
  the check passes on both; the RAM and flash lines in the report.

## Acceptance criteria

- [ ] The report quotes both framework default tables as read from the installed package, and the two new tables beside them.
- [ ] `spiffs`, `coredump`, `otadata`, `app0` are byte-identical in offset and size to the defaults on both boards; `nvs` is above `app0`'s end; the gap at `0x9000` is unallocated.
- [ ] `tools/check-partitions.py` runs in CI on both boards against the real merged image and passes; its fixture tests fail it for a moved `spiffs` and for a merged image that reaches `nvs`.
- [ ] The framework's `Preferences.begin` recovery on a blank or foreign NVS region is confirmed from its source and cited, or `SettingsStore::begin` does the erase-and-retry itself with a test of the decision.
- [ ] README, `docs/field.md` and the release notes say which flash forgets the settings and when, and that the identity never goes.
- [ ] `pio test -e native` green; CI green on both boards.

## Not verified here

A board. The first merged flash of the release that carries this table is
the test: the identity's first eight hex characters on the display before
and after must match, and the repeater's ACL must still hold the board.

## Out of scope

OTA (this firmware has none, and `app1` goes with that said); the display
(09); the backoff (10).
