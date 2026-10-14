# 16 — The library

**Depends on:** 15.
**Spec:** proposal 03 (*The shape*) in the hardware repository; PlatformIO's documentation of `library.json` and of libraries under `lib/` of a project (the version the toolchain pins); ESP-IDF's documentation of a component's `CMakeLists.txt` and of the component manager's `git` source with a `path` (the version the Walter firmware pins — `twinoak.meshcore.walter-firmware`, `tasks/phase-0/01-the-toolchain.md` and its report say which); this repository's `platformio.ini` (`[env:native]`'s `build_src_filter`), `test/test_tls_policy/test_tls_policy.cpp` (the paths it reads: `src/agent/TlsRoots.h`, the scan roots including `lib`), `README.md` (*Rails that hold in every task* — its paths are checked by `Rails.EveryPathTheRailsListNamesIsInTheTree`), `tools/sync-fixtures.ps1`, `.github/workflows/ci.yml`.
**Size:** one session.
**Model:** claude-opus-5

## Goal

The portable half of the agent becomes a library with a name, a manifest and
a boundary the compiler enforces: `lib/agent-core/`, compiled whole by
`[env:native]` — `JobRunner` and `AgentLoop` included, for the first time —
and by the board builds exactly as before. A second firmware can then take
it at a commit the way this one takes MeshCore. Nothing moves that is not
portable, and nothing portable is left behind.

## Deliverables

- **`lib/agent-core/`** with `library.json` (name `agent-core`, the
  repository's version, `frameworks: *`, `platforms: *`, no dependency but
  ArduinoJson at the exact version `platformio.ini` pins, and a `build`
  section that puts `include/` on the path), `src/` and `include/agent/`:
  - from `src/agent/`: everything but `AgentClient.*` and `TlsRoots.h`;
  - `src/mesh/ReplyCodec.*`, `src/Backoff.*`, `src/LogRing.*`, `src/Log.h`
    (the portable macros and `mm::logf`; `Log.cpp`'s `Serial` sink stays
    board side), `src/version.h`'s declarations if the library reports a
    version (decide: the library has its own `AGENT_CORE_VERSION` from
    `library.json`, and the firmware's version is the board's);
  - the three interfaces of 15.
  Includes become `#include <agent/JobRunner.h>` throughout; no relative
  include crosses the boundary in either direction.
- **The boundary, enforced.** A native test walks `lib/agent-core/` and
  fails on any `#include` of `Arduino.h`, `WiFi*.h`, `Preferences.h`,
  `esp_*.h`, `freertos/*`, `HTTPClient.h`, anything under MeshCore's tree,
  and on `millis(`, `delay(`, `Serial`, `ESP.` outside a comment. The
  library compiles with `-std=c++17 -Wall -Wextra -Werror` and with
  `-fno-exceptions -fno-rtti` (the board's flags) on the host.
- **`[env:native]`** drops its hand-kept `build_src_filter` list for the
  library: the library is compiled whole (`lib_deps` or the project's own
  `lib/` discovery — say which and why), and the filter keeps only the
  board-side files the tests still need (`settings/Keys.cpp`,
  `console/Parser.cpp`, `ui/DisplayLines.cpp`, `version.cpp`). The 17 test
  suites pass unchanged.
- **The ESP-IDF face.** `lib/agent-core/CMakeLists.txt` registering the same
  sources as an IDF component (`idf_component_register(SRCS … INCLUDE_DIRS
  include REQUIRES …)`), with ArduinoJson declared the way the Walter's
  toolchain can satisfy it (a managed component or a vendored copy — read
  the Walter's phase-0 report for its component manager settings and
  say which), and an `idf_component.yml` with the library's version. **It
  is not built here** (no IDF in this repository's toolchain): the report
  says so, and the Walter's phase-4 task 01 is where it first compiles. A
  CMake syntax check (`cmake -P` on a stub, or `cmake --trace-expand` on a
  minimal project that includes the file) is the most this task can do; do
  it.
- **The rails move with the files.** `test_tls_policy`'s path to
  `TlsRoots.h` is unchanged (it stays in `src/agent/`); its scan already
  covers `lib/`. `README.md`'s rails section names the moved files' new
  paths; `Rails.EveryPathTheRailsListNamesIsInTheTree` is green.
  `docs/upstream.md` gains a paragraph: *this repository is itself an
  upstream now* — what a consumer pins, what changes a consumer must be told
  about (the three interfaces, the `MeshReply` constants, the fixtures'
  manifest commit), and that a tag `vX.Y.Z` of this repository is the
  library's version too.
- **CI** builds the host tests and both boards as before; a step prints the
  library's file list so a reviewer sees what moved.

## Acceptance criteria

- [ ] `lib/agent-core/` holds every portable file and no other; `src/agent/` holds `AgentClient.*` and `TlsRoots.h` and nothing else (list both in the report).
- [ ] The boundary test is green; the library compiles on the host with the board's strictness flags.
- [ ] `pio test -e native` passes all 17 suites unchanged; `pio run -e heltec_v3 -e heltec_v4` sizes within 1 kB of 15's.
- [ ] `library.json`, `CMakeLists.txt` and `idf_component.yml` are in the tree; the CMake check ran; the report says plainly that the IDF build is unverified here.
- [ ] README and `docs/upstream.md` updated; the rails tests green.

## Not verified here

The library compiled by ESP-IDF — the Walter's phase 4, task 01. The Heltec
on the air with a rearranged tree — the bench walk on the next release.

## Out of scope

Fakes (17). The one-wake mode (18). Any behaviour change.
