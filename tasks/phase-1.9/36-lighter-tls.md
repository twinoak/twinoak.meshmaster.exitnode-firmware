# 36 — A lighter TLS

**Depends on:** nothing (pile 1.8 merged).
**Spec:** `platformio.ini` (`[esp32_base]` ~L100: `framework = arduino`, `platform = platformio/espressif32@6.11.0`; `[env:heltec_v3]`, `[env:heltec_v4]`, their partitions and `extra_scripts`), `tools/pio/merge_bin.py`, `tools/check-partitions.py` (the pattern for a check against the framework's own file), `boards/heltec_v4.json` (PSRAM), `src/platform/TlsHeap.{h,cpp}` (mbedtls's allocator wrapped through `mbedtls_platform_set_calloc_free`: the routing to PSRAM on a V4 and the per-call accounting depend on `MBEDTLS_PLATFORM_MEMORY`), `src/agent/AgentClient.cpp` (`WiFiClientSecure` + `HTTPClient`, the kept connection of 1.8/30), `src/net/MqttClient.cpp` (esp-mqtt over esp-tls), `src/observer/Board.h` (the figures, which this task does not move), `.github/workflows/ci.yml` and `release.yml` (the build steps and their caches), `README.md` (*Building it*), `docs/observer.md` (*What it costs*); 1.8/31's report (*What one slot costs*: mbedtls's two content buffers `CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN` 16 384 each, ~33 400; the CA store and the peer's chain kept, ~8 000) and its note that the next step is a custom ESP-IDF build; the framework on disk, arduino-esp32 `3.20017.241212` (2.0.17, on ESP-IDF 4.4) and its `tools/sdk/esp32s3/` (the config it was built with, per memory type); PlatformIO's *Arduino as an ESP-IDF component* (`framework = arduino, espidf`), ESP-IDF 4.4's mbedtls Kconfig.
**Size:** one session, long.
**Model:** claude-fable-5-1

## Goal

On `v0.5.0` a Heltec V3 cannot open a broker slot. The kept connection to
meshmaster.dk holds ~63 KB and a slot needs as much again, on a board with ~136 KB
(this pile's README). The bulk of a connection is mbedtls's two 16 KB content
buffers, held for the whole life of the connection, plus the certificates kept
after the handshake. Those sizes are compile-time options of ESP-IDF, and the
Arduino core ships ESP-IDF precompiled, so nothing in our code can change them.
Build ESP-IDF from source under **the same Arduino core**, with its own config
but for the TLS lines, and change nothing else.

## Deliverables

1. **The build.** `framework = arduino, espidf` (Arduino as an ESP-IDF component)
   for both boards, on the **same** Arduino core (2.0.17) and the ESP-IDF it was
   built on (4.4.x). If `espressif32@6.11.0` cannot pair the two, find the
   platform release that does and pin it, saying why. The `native` environment
   and `lib/agent-core` are untouched. **Stop on purpose** (`logs/STOPPED-36.md`,
   RUNBOOK §5) rather than move the Arduino core to 3.x or the ESP-IDF to 5.x.
   That is a port, not this task. Write down what was found, and what plan B
   would be: Espressif's `esp32-arduino-lib-builder`, rebuilding the core's own
   libraries with the changed config and pinning them through
   `platform_packages`.
2. **The config is the core's own, but for the TLS lines.** Start from the
   config the core was built with for each board's memory type: the V3 without
   PSRAM, the V4 with its 2 MB QSPI PSRAM. Commit it as the `sdkconfig.defaults`
   (per board where they differ) and change only these:
   - `CONFIG_MBEDTLS_DYNAMIC_BUFFER=y`: the content buffers are allocated while
     a record is in flight, not for the life of the connection;
   - `CONFIG_MBEDTLS_DYNAMIC_FREE_PEER_CERT=y` and
     `CONFIG_MBEDTLS_DYNAMIC_FREE_CONFIG_DATA=y`;
     `CONFIG_MBEDTLS_DYNAMIC_FREE_CA_CERT` only if every connection sets its CA
     again on every handshake (check `WiFiClientSecure` and esp-tls; say what
     you found);
   - `CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN=y` with
     `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=16384` (a server may send a full
     record) and `CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=4096`;
   - `CONFIG_MBEDTLS_SSL_KEEP_PEER_CERTIFICATE` off, if the dynamic options
     need it so.

   `MBEDTLS_PLATFORM_MEMORY` stays on: `TlsHeap`'s wrapper is how the V4 routes
   to PSRAM and how a call's cost is counted. Check it still sees every
   allocation; dynamic buffers allocate and free more often.
3. **The check.** `tools/check-sdkconfig.py`, in CI beside `check-partitions.py`,
   holds each committed config against the core's own file and fails on any
   difference that is not in its list of allowed keys (the lines above, each
   with its reason). `--self-test` plants one difference that is not allowed
   and one that is.
4. **The same firmware otherwise:**
   - the partition tables (`check-partitions.py` green), flash mode and
     frequency, and PSRAM on the V4;
   - the merged image's layout: `merge_bin.py`'s offsets, with the bootloader
     now built from source and set to the same flash mode and size as the one
     it replaces;
   - what the board says at boot and in `status`;
   - the native tests green, unchanged.

   Report the RAM and Flash of both images before and after, and the config
   diff, line by line.
5. **CI and the release workflow** build the new way, with their caches keyed
   on the committed configs too. Give the first, uncached build time per board,
   and the cached one.
6. **Words:**
   - README *Building it*: how it is built now, and why;
   - `docs/observer.md` *What it costs*: what is expected and why, marked as
     **expected** until 37 writes the measured figures;
   - a line in `CHANGELOG.md`'s unreleased section: no interface moved, the
     build did.

## Acceptance criteria

- [ ] Both boards build in CI the new way; the native tests and `check-partitions.py` green; `check-sdkconfig.py` green, and failing on a planted unlisted key (the self-test).
- [ ] The committed configs differ from the core's own only in the listed TLS keys (the check's output quoted in the report).
- [ ] The merged images keep their layout (offsets, partition table, flash mode and size), shown in the report.
- [ ] RAM/Flash of both images before and after, and both build times, in the report.
- [ ] The report says plainly what only the bench can tell (the heap at rest, the handshake beside a slot, the bootloader on a first install), so the walk in the README can be done from it.

## Out of scope

The gate's figures in `Board.h` (37, from the bench). The Arduino core 3.x and
ESP-IDF 5.x. Any change to what the firmware does. A release (37).
