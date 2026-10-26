# 25 — The strings are UTF-8

**Depends on:** nothing.
**Spec:** the 1.6 review ([`../phase-1.6/FOLLOWUPS.md`](../phase-1.6/FOLLOWUPS.md), its three `[1.7]` items); meshmaster.dk at `master` (read-only): `tests/fixtures/contracts/README.md` (the escaping rule, clause 8), `JsonEscaping.json`; this repository's `lib/agent-core/include/agent/JsonWriter.h` (~L95–135), `src/mesh/AgentMesh.cpp` (`validUtf8PrefixLength` ~L183), `lib/agent-core/src/AgentLoop.cpp` (`keepAcrossSleep`, `restoreKept`, `buildBatch`, `wakeJobs`), `test/test_envelopes/`, `test/test_loop/` (~L1572), `CHANGELOG.md`.
**Size:** one session.
**Model:** claude-opus-5

## Goal

Three defects the 1.6 review found, each in the library and each with a test
that fails first.

## Deliverables

1. **`JsonWriter::string()` never writes invalid UTF-8.** Since 24 a character
   is written as its own bytes, and the decoder accepts overlong forms, code
   points above U+10FFFF and lead bytes F5–FF — so `E0 83 85` reaches the wire
   as itself and System.Text.Json refuses the result, which then blocks the
   ordered result queue until it is evicted. Decode as `validUtf8PrefixLength`
   does (overlong, surrogates, above U+10FFFF, F5–FF all invalid) and write
   U+FFFD for each **maximal subpart** of an ill-formed sequence (Unicode §3.9,
   what .NET does), so a cut `E2 82 41` is one U+FFFD and an `A`. One
   `Clause8` case each: `C0 80`, `E0 83 85`, `F0 80 83 85`, `F4 90 80 80`,
   `F5 80 80 80`, `ED A0 80`, `E2 82 41`, and a sequence cut at the end of the
   buffer; the output parses with ArduinoJson and is valid UTF-8 (a checker in
   the test, not the writer's own decoder).
2. **A kept advert without a clock is not given the restore's time.**
   `keepAcrossSleep` writes epoch 0 when the clock had no time, and
   `restoreKept` then stamps it with the moment of the restore. Drop such
   adverts at the write and count them in `observationsDropped` (or carry
   their age and the sleep's length, if the image has room — say which); the
   test writes the image **with no clock**.
3. **A declined type is declined before it is priced.** `wakeJobs` asks
   `mesh.supports(type)` before `transactionsOf`, so a bound `trace` the mesh
   cannot serve is answered `unsupported`, not `failed` with *wake budget*.
4. **`agent-core` 0.3.1**, a CHANGELOG section (no interface moved; the wire
   moved only for strings that were invalid before) and the version in all three
   places.

## Acceptance criteria

- [ ] Every new `Clause8` case fails before and passes after; `JsonEscaping.json` still matches byte for byte; no output of the writer is invalid UTF-8 over a fuzzed input (a few thousand random byte strings in a native test, seeded).
- [ ] The kept-advert test with no clock and the declined-type test pass.
- [ ] The Heltec's heartbeat bytes unchanged; `pio test -e native`, the host matrix and both board builds green; sizes in the report.

## Out of scope

The Walter's store (its repository).
