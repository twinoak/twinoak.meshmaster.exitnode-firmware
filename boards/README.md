# `boards/` — board definitions PlatformIO cannot find anywhere else

PlatformIO reads `boards_dir` (this directory, by default) **before** it clones
a dependency, so a board definition that lives inside MeshCore's package is not
one a consumer of that package can point `board =` at. There is exactly one
such board here, copied from the pin and unedited — line for line, with this
repository's LF endings, which `.gitattributes` gives every text file:

| File | Copied from | At |
|---|---|---|
| `heltec_v4.json` | `boards/heltec_v4.json` in [MeshCore](https://github.com/meshcore-dev/MeshCore) | the commit `[meshcore].pin` names in `platformio.ini` (`companion-v1.17.1`, `d9296435`) |

Nothing here is edited: a difference between this file and the pinned package's
is a bug, not a feature, and a pin bump re-copies it
([`docs/upstream.md`](../docs/upstream.md), which lists this directory among
the places a bump re-reads). The Heltec V3 needs no such file — its board is
PlatformIO's own `esp32-s3-devkitc-1`, which is what MeshCore's own V3 variant
uses too.

The Arduino *variant* the V4's definition names (`"variant": "heltec_v4"`, the
directory `pins_arduino.h` lives in) is **not** copied: it is reached with
`-I ${meshcore.dir}/variants/heltec_v4` in the environment's `build_flags`,
which is how MeshCore reaches its own, and the variant's sources are compiled
into the MeshCore library by `MC_VARIANT=heltec_v4` (upstream's
`build_as_lib.py`). So the one thing vendored is the one thing that cannot be
referenced.

## `partitions/` — the two tables, and why they exist

`meshmaster_8MB.csv` (the V3) and `meshmaster_16MB.csv` (the V4) are the
framework's `default_8MB.csv` and `default_16MB.csv` with **one change: `nvs`
is above the application**, at the offset `app1` had. `platformio.ini` names
them per environment (`board_build.partitions`); the board definitions above
are not edited for it.

The reason is the merged image. A board out of the box is flashed with
bootloader + partition table + application as one file from `0x0`, and
`esptool merge_bin` fills the gap between the partition table (`0x8000`) and
`boot_app0` (`0xE000`) with `0xFF`. The framework's default puts NVS — the
WiFi credentials, the server URL, the token — at `0x9000`, inside that gap, so
every merged flash of `v0.1.1` erased a provisioned board's settings (bench,
4 October 2026). A merged image ends where the application ends; NVS above the
application is NVS a merged image cannot reach.

**The fill stops at `otadata`, and `otadata` stays where the framework put
it.** The `0xFF` runs from `0x9000` to `0xDFFF` and no further, because
`FLASH_EXTRA_IMAGES` names the framework's own `boot_app0.bin` at `0xE000`:
the same merge writes its 8 192 bytes over both `otadata` sectors, and what
lands there is the record that says *boot `app0`* - which is exactly what the
bootloader wants to find on a board that has never done an OTA. (Checked on
this repository's own merged V3 image, 5 October 2026: `0x9000`-`0xDFFF` is
all `0xFF`, and `0xE000`-`0xFFFF` is `boot_app0.bin` byte for byte.) So
`otadata` is not a casualty of the merge and needs no protecting; moving it
above the application to "protect" it would leave that write landing in a hole
and the bootloader reading an erased `otadata`.

What **does not move**, and why it is held in CI rather than promised:

- **`spiffs`** keeps the default's offset and size on both boards. The identity
  lives there (`IdentityStore(SPIFFS, "/identity")`), it is the one thing on a
  board that cannot be re-provisioned, and a `spiffs` that moves by one sector
  is an identity that is gone and a repeater ACL that names a stranger.
- **`app0`** keeps `0x10000` and its size: the application image and
  `pio run -t upload` write there, and `otadata` at `0xE000` keeps pointing at
  it. **`coredump`** stays where it was.
- **`nvs` keeps its label** and the default's size: `Preferences` opens the
  partition by that label (`src/settings/Store.cpp` changes nothing).
- **`app1` is gone** — this firmware does no OTA, so the second slot was never
  written — and the rest of its region is **left unallocated** rather than
  given to `spiffs`, which would have moved `spiffs`. The gap at
  `0x9000`–`0xDFFF` stays empty for the same reason.

[`tools/check-partitions.py`](../tools/check-partitions.py) reads the csv each
environment names and holds all of that against the framework default for the
board's flash size, then checks that the merged image really does end before
`nvs` begins — in `ci.yml`, after `pio run` and `-t mergebin`, on every build of
every board, so the day the application grows past its region CI says so before
a release does. Its own tests are the two wrong tables in
[`tests/fixtures/partitions/`](../tests/fixtures/partitions/README.md). A
framework bump that moves a default (`platformio/espressif32` in
`platformio.ini`) is a red build there too, which is right: these tables are
derived from the defaults and would need re-deriving.

What a board goes through on the day this table lands is in README.md's
*Flashing it*: the application image keeps the old table and the old settings;
the first merged flash gets the new table, keeps the identity, and needs WiFi
and a new token once.
