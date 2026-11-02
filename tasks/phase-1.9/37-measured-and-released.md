# 37 — Measured, and released

**Depends on:** 36, and the bench rows in this pile's README (*after 36, …*). **First:** if those rows are empty, write `logs/STOPPED-37.md` saying so, change nothing else, and end. The figures this task writes come from a board, and a desk cannot measure them.
**Spec:** this pile's README (*Measured*), 36's report (*Not verified here*), `src/observer/Board.{h,cpp}` (`kHeltecV3`, `kHeltecV4`: slots, `slotInternalBytes`, the PSRAM share, `uplinkDipBytes`, their source phrases), `src/observer/Connection.{h,cpp}` (the gate: `gateNeedBytes()`, the floor `kHeapFloorBytes`, the margin, the yield), `docs/observer.md` (*The lines to type*, *What it costs*), `.github/workflows/release.yml` (the notes: the V3/V4 rule ~L240 and ~L312), `CHANGELOG.md`, the three `agent-core` manifests; 1.8/35 (how v0.5.0 was tagged and checked).
**Size:** one session.
**Model:** claude-opus-5

## Goal

36 made a TLS connection lighter, and Thomas walked it on the bench. Put what
the walk measured into the firmware's figures and words, decide the V3/V4 rule
from those figures rather than from 28 October's, and publish **v0.6.0** so that
FD9B and the other V3s get their observer back.

## Deliverables

1. **The figures from the walk.** `kHeltecV3` and `kHeltecV4` take the measured
   slot cost and uplink dip (a kept call, and a handshake), each with its source
   phrase dated (*measured 2 Nov 2026, after 36, …*). Say whether `kHeapFloorBytes`
   should move, from the hour's `min ever`, and move it only if the figures say
   so.
2. **The rule, as measured.** How many slots a V3 opens, and a V4, from the
   rows. If a V3 fits two, say it and allow it; if it fits one, keep one. The
   refusal's sentence and `docs/observer.md`'s *The lines to type* follow.
3. **Words:** `docs/observer.md` *What it costs* with the measured table in place
   of 36's expected one; the release notes in `release.yml` with the V3/V4 rule
   as measured, and one line: *a lighter TLS: a V3 runs the observer beside its
   MeshMaster connection again*. Keep v0.5.0's shape: the flasher at
   **https://meshcore.io** with *Custom firmware*, the table of merged vs plain
   `.bin`, the console set-up.
4. **v0.6.0.** `CHANGELOG.md` v0.6.0, answering the four questions (nothing of
   the interfaces, caps or fixtures moved; the build did), and `agent-core` 0.6.0
   in the three manifests, as 35 did for 0.5.0. Tag `v0.6.0` on `master` after
   CI is green, and publish it as **Latest**, not a pre-release (Thomas,
   1 November). Check the four `.bin` files and `SHA256SUMS`, as 35 did.

## Acceptance criteria

- [ ] `Board.h`'s figures are the walk's, each with its dated source; the gate's tests pass with them; a V3 profile opens the measured number of slots (a test).
- [ ] The notes and `docs/observer.md` say the measured rule, and none says `flasher.meshcore.co.uk` or `esptool`.
- [ ] `v0.6.0` is the *Latest* release with four `.bin` files and `SHA256SUMS` from the tagged commit; the report quotes the run and the checksums.

## Out of scope

Anything 36 did not change. The v0.5.0 notes (Thomas, 2 November: leave them).
