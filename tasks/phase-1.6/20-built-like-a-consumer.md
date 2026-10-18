# 20 — Built like a consumer builds it

**Depends on:** nothing.
**Spec:** the 1.5 review ([`../phase-1.5/FOLLOWUPS.md`](../phase-1.5/FOLLOWUPS.md), *Review of 15–19*: the parameter named `unix` and ArduinoJson under GCC 13 at `-O2`); the Walter's p4-01 report (`twinoak.meshcore.walter-firmware`, the report of its phase-4 task 01, *Reported upstream* §1 and §2, with the diff - read it if the repository is beside this one, and do not copy from it blindly; the review above has what it found); `lib/agent-core/src/ConfigReply.cpp:41,48`, `lib/agent-core/library.json` (`build.flags`, the `-Werror` every PlatformIO consumer inherits), `tools/check-library-host.py` (`FLAGS`, ~L41), `tools/check-idf-component.cmake`, `.github/workflows/ci.yml`; `docs/library.md` §2 and §8.
**Size:** one session, short.
**Model:** claude-opus-5

## Goal

The library's first consumer built it as an ESP-IDF component on a Linux
host and found two things this repository's checks cannot see: a `gnu++`
dialect predefines `unix`, and GCC 13 at `-O2` warns inside ArduinoJson —
with `-Werror` carried to every consumer by `library.json`. Make the host
check build the library the ways a consumer will, and make it pass.

## Deliverables

1. **`unix` → `epoch`** in `ConfigReply.cpp`, and a grep of the whole library
   for any other identifier a GNU dialect or a target predefines (`unix`,
   `linux`, `i386`, `__unix__`'s friends); none may be a parameter, local or
   member name. The grep is a check in `check-library-host.py`, not a
   one-off, so the next one fails here.
2. **The host check builds a matrix**, not one configuration: `-std=c++17`
   and `-std=gnu++17`, each at `-O0` and `-O2`, with the board's
   `-fno-exceptions -fno-rtti`, `-Wall -Wextra -Werror`. Print the compiler
   and its version first. Keep the run under a minute on CI (compile only, in
   parallel if it needs to be).
3. **ArduinoJson under GCC 13 at `-O2`.** Reproduce the
   `-Wmaybe-uninitialized` at `ObjectImpl.hpp:36` (via `ConfigReply.cpp` and
   `JobEnvelopes.cpp`) on CI's GCC, then remove it the narrowest way that
   holds: a diagnostic pragma scoped to the ArduinoJson include in one
   header the library's files use (and prove the pragma reaches a warning the
   middle end raises after inlining — it may not), or a newer ArduinoJson if
   one has fixed it (its changelog cited, the pin moved in `library.json`,
   `idf_component.yml` and `platformio.ini` together, and both boards'
   sizes in the report). Not `-Wno-error` for the whole library: a consumer
   loses a real warning that way. If none of these holds, say so and propose
   the smallest change of `library.json`'s flags in the report.
4. **CI runs the matrix** on its Linux runner, whose GCC is the consumer's
   (the job's log names the version). `docs/library.md` §8 says which
   dialects and levels are proven.

## Acceptance criteria

- [ ] No identifier in `lib/agent-core/` is a predefined macro in any of the four configurations; the check fails on a planted `int unix`.
- [ ] `check-library-host.py` builds all four configurations clean with `-Werror` on CI's GCC 13.
- [ ] Both board images build; their sizes are in the report against `v0.2.0` (expect no change unless ArduinoJson moved).
- [ ] `pio test -e native` green.

## Not verified here

The Walter's own build: its next sync of the library drops its local patch,
and its report says so.

## Out of scope

Any change of behaviour. Clang (no consumer uses it).
