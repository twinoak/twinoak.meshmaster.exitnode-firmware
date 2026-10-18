# 21 — A wake keeps its budget

**Depends on:** 20.
**Spec:** the 1.5 review ([`../phase-1.5/FOLLOWUPS.md`](../phase-1.5/FOLLOWUPS.md), *A wake's byte cap and uplink seconds do not bound the job step*); task p1.5-18 and its report (the budget's four bounds); `lib/agent-core/src/JobRunner.cpp` (`post()` ~L1374, the unbounded `flushResults()` ~L1405, the bounded one below it), `AgentLoop.cpp` (`wake()`, `wakeResults`, `runClaimed`, `reportNotRun`, `uplinkSpentMs_`), `include/agent/AgentLoop.h` (`Budget`, `WakeReport`); `test/test_loop/` (`AByteCapPostsAsManyBatchesAsItFits…`, `AWakeFlushesWhatTheLastWakeCouldNotPost…`); `docs/host.md` (*one wake*), `docs/library.md` §6.
**Size:** one session.
**Model:** claude-opus-5

## Goal

18's wake promises four bounds. Two of them leak at the job step: every job
result goes through `JobRunner::post()`, which first flushes the **whole**
offline buffer with no byte cap, and none of those posts is counted in the
report's bytes or the uplink's seconds. On an LTE plan priced by the byte,
one claimed job can post a day's kept results. Make every post in a wake
spend from the same budget, and prove it.

## Deliverables

- **One budget for every post.** `post()` takes the wake's remaining bytes
  (and `loop()` passes *no cap*, so the Heltec is unchanged); the queue
  ahead of a new result flushes as far as the bytes allow, oldest first, and
  a result that does not fit is **kept** behind the queue, not posted out of
  order. `report.bytesPosted` counts every byte the wake posted, from every
  step.
- **The uplink's seconds count the job step.** Every `IUplink` call a wake
  makes is timed into `uplinkSpentMs_` — config, heartbeat, claim, results,
  observations, extend, progress — through one place, so a new call cannot
  forget. When the uplink budget is spent, the wake stops posting and keeps
  what is left, as it already does for observations.
- **A claimed job is still never left silent**: if its own result cannot be
  posted inside the budget it is kept (the buffer's rule) and the report
  says *kept: budget*; 18's property (every claimed job ends with a result
  posted or kept) is extended to the job step's posts.
- **Tests** with 17's fakes: a kept result plus a claimed job under a cap
  that fits one of the two (the old one goes first, the new one is kept,
  bytes counted exactly); an uplink budget spent during the job step; a
  wake whose cap is zero bytes (nothing posted, everything kept, the report
  says why); the property test over random caps, buffers and job lists;
  `loop()`'s suite untouched.
- `docs/host.md` and `docs/library.md` §6 say the rule in one sentence each:
  *a wake's byte and uplink budgets cover every call it makes*.

## Acceptance criteria

- [ ] No `IUplink` call in a wake is outside the byte and uplink-second accounting (a test per call kind, and a grep-style test that `JobRunner` has no unbounded flush reachable from `wake()`).
- [ ] Results are posted in the order they were made, under any cap.
- [ ] The Heltec's `loop()` behaviour and heartbeat bytes unchanged (the existing suites, untouched).
- [ ] `pio test -e native` and both board builds green.

## Not verified here

A wake on a Walter over LTE (its phase 4).

## Out of scope

What survives a deep sleep (22). The transaction cap inside `Budget` (the
intent's one number stays the server's and the loop's, as 18 decided).
