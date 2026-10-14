# 18 — One wake

**Depends on:** 17, and meshmaster.dk's pile 4.14 **task 120 merged** (the two heartbeat fields; this task syncs the fixtures that carry them).
**Spec:** proposal 01 parts B and C (the hardware repository, `software/spec/proposals/01-the-unit-in-meshmaster.md`) and `software/spec/SYSTEM.md` §5 (*duty cycle*); the Walter firmware's `docs/ARCHITECTURE.md` §10 (*One wake*) and `tasks/phase-3/05-the-wake-cycle.md` (the `Backend::exchange(what_we_know, deadline)` seam the loop will sit behind); meshmaster.dk at today's `master`: `spec/EXIT-NODES.md` §6 and §10 *as built* for the sleeping agent (pile 4.14's 120 and 121), `tests/fixtures/contracts/AgentHeartbeat.json` and the claim fixtures after 120; this repository's `lib/agent-core/include/agent/AgentLoop.h`, `Envelopes.cpp` (`writeHeartbeat`), `JobEnvelopes.cpp` (`kClaimMax`), `tools/sync-fixtures.ps1`.
**Size:** one session.
**Model:** claude-opus-5

## Goal

The Heltec runs `AgentLoop::loop()` forever with WiFi up. The Walter wakes a
few times a day, has one bounded slice of time and energy, and sleeps. This
task gives the loop a second way to be driven — *one wake*: do everything a
check-in is, in order, within a budget, and return — without changing the
first. The Heltec keeps calling `loop()`; the Walter calls `wake()`.

## Deliverables

- **Fixtures synced** from meshmaster.dk after its task 120 (`tools/sync-fixtures.ps1`;
  the manifest's commit moves; `-Verify` green). `writeHeartbeat` gains the
  two optional fields, written only when set: `nextCheckInS` and
  `maxTransactionsThisWake`; `HeartbeatIdentity` (15) or a new
  `WakeIntent` struct carries them. The Heltec sets neither and its heartbeat
  bytes are unchanged (`test_envelopes` against the fixture of an agent that
  proposes nothing — if 120 changed the fixture's example to a proposing
  agent, add a second fixture there and say so in the report).
- **`AgentLoop::wake(const WakeIntent&, const Budget&) -> WakeReport`**:
  one pass, each step bounded by the fake-clock-tested budget — config if
  stale → heartbeat (with the intent; its reply's `pollIntervalS` is **the
  accepted interval**, returned in the report for the caller to sleep by) →
  pending results flushed → if `jobsWaiting`: claim with `max` sized from the
  cap (the server also caps; the loop asks for no more than it can run) →
  run the claimed jobs through `JobRunner` while the wake's time budget
  holds (a job that would not fit is **not claimed**; a job claimed is
  always finished or its result is `failed` with *wake budget* — never left
  leased and silent) → observations posted → return. The report says what
  was done, what was skipped and why, the accepted interval, the server
  time, the error counters.
- **Budget**: seconds for the whole wake, seconds for the uplink, a
  transaction cap (the intent's), and a byte cap for results posted (the
  LTE plan). All held by the fake clock in tests. Pacing (`AirtimeGate`) is
  untouched: a wake with a cap of six transactions still spaces them.
- **Backoff across wakes**: `ServerPolicy`'s actions are the same, but a
  *BackOff* in one-wake mode means *return now and tell the caller to sleep
  at least this long* rather than spinning; the state that `noteFailure`
  keeps (consecutive failures, the hold) survives in the loop object, which
  the Walter keeps across deep sleep in retained storage — so the loop
  exposes `state()` / `restore(state)` as a small POD, tested round-trip.
- **The reboot guard** does not fire in one-wake mode (the Walter has its own
  supervisor and its own idea of what a failure costs); the report says *the
  platform would have been asked to reboot* instead.
- **Observations in one wake**: the ring is drained into as many batches as
  the byte cap allows, oldest first; what does not fit stays for the next
  wake with its original timestamps (the ring's own rule; assert it).
- **A seam for the runner's own clock**: the Walter sets the radio's clock
  before anything else and has the server's time from the heartbeat; the
  loop already calls `takeServerTime` — make sure the one-wake path does it
  before the first job, and test that a result's timestamps are the server's
  when the platform had none.
- **Tests** in `test/test_loop/` with 17's fakes: a wake with nothing waiting
  (one heartbeat, done); with three jobs and a cap of two transactions' worth
  (one claimed, the rest left on the server, never claimed); a job that
  overruns the time budget (`failed`, *wake budget*, result posted); a server
  away (backoff returned, state round-tripped, the next wake honours it);
  a byte cap that fits half the observations; the Heltec's `loop()` suite
  from 17 still green and byte-identical heartbeats.
- **`docs/host.md`** gains *one wake*; `docs/library.md` is 19's.

## Acceptance criteria

- [ ] Fixtures synced after 120; `-Verify` green; `test_contract_fixtures` green.
- [ ] `wake()` exists with the budget semantics above, every bound asserted by the fake clock; `loop()`'s behaviour and the Heltec's heartbeat bytes unchanged (tests).
- [ ] No job is ever left leased by a wake (a property test over random budgets and job lists: every claimed job has a posted result).
- [ ] `state()`/`restore()` round-trip; a backoff survives a "sleep".
- [ ] `pio test -e native` and both board builds green.

## Not verified here

A real wake on a Walter — its phase 4. The Heltec on the air — the bench walk
on the next release.

## Out of scope

The Walter's `Backend` implementation and its interval policy (its phase 4);
the server's side of the two fields (meshmaster.dk 120, 121).
