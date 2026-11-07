# 39 — A trace of two bytes

**Depends on:** nothing (after 38 in the order).
**Spec:** `lib/agent-core/src/JobRunner.cpp` (`runTrace` ~L620–660: for a direct neighbour, *a known path of zero hops*, `hashSize = 1`, with the comment *this board's own size, one byte (PATH_HASH_SIZE)*), `lib/agent-core/include/agent/IMesh.h` (`sendTrace(…, hashSize, …)` ~L196; nothing tells the library the width the board floods at), `src/mesh/AgentMesh.h` (`kFloodPathHashSize = 2` ~L193–201 and its comment), `test/` (the runner's trace tests, `RunnerTest.ATraceAlongThreeByteHashesIsUnsupportedAtNoTransaction`), the README's rail *Path hashes are two bytes or more, never one* (~L670) and `docs/library.md` (~L890, the same rail); meshmaster.dk's `src/MeshMaster.Agent/Handlers.cs` (~L962: the .NET agent traces a direct neighbour at the companion's own width, two bytes since D-33), read only.
**Size:** one session.
**Model:** claude-opus-5

## Goal

Thomas, 5 November, on meshmaster.dk job 20005, a trace from Ov3t|Master
(`e4f3…`) to its direct neighbour DK_8620_Kjellerup_V_R03 (`8623…`):

```json
{ "hops": [ { "snr": 8.5, "prefix": "86" }, { "snr": 7.25, "prefix": "E4" } ] }
```

One byte a hop. Three repeaters within earshot of that exit node share the first
byte `86`: `8620` (R01), `8621` (R02) and `8623` (V_R03). Each of them repeats a
trace addressed to `86`, so the 8.5 dB may not be V_R03's at all. That is
exactly what D-33 forbids. The README says *path hashes are two bytes or more,
never one, wherever this firmware chooses a width*. The trace to a direct
neighbour is the one place it still chooses one, because `runTrace` fills it in
as `1`. A trace along a path the server sent already keeps that path's width.
Job 20003 went `8623 → 8801 → 8623 → E4F3`.

## Deliverables

1. **The library is told the board's width.** `IMesh` gets
   `floodPathHashSize()`, which `AgentMesh` answers with `kFloodPathHashSize`
   and the fakes with 2. `runTrace` uses it for a direct neighbour, both for
   the repeater's hash and for this board's own hash on the way back.
2. **Nothing else in the library picks a width.** A trace along a path the
   claim carried keeps that path's width, as now: the library never widens a
   path, because the extra byte of a narrow hop belongs to no node. Grep for
   any other `= 1` width and say in the report that there is none, or fix it.
3. **Words:** the comment at `runTrace`, the README's rail and `docs/library.md`
   (*a trace to a direct neighbour is at the board's width*), `CHANGELOG.md`
   (*Unreleased*).

## Acceptance criteria

- [ ] Host test: a trace to a direct neighbour sends a round trip of two-byte hashes: the repeater's first two key bytes, then this board's. The parsed hops carry four-hex-digit prefixes.
- [ ] Host test: a trace along a claimed one-byte path still goes at one byte, and one along a two-byte path at two (unchanged).
- [ ] `pio test -e native` green, both board builds green, CI green.

## Out of scope

The width of a path the server sends (meshmaster.dk's side). The flood width,
already 2. Releasing: this ships with the next release of the firmware.
