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
