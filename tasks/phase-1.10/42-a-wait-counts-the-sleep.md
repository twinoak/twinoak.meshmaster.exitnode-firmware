# 42 — A wait counts the sleep

**Depends on:** nothing (after 41 in the order).
**Spec:** `lib/agent-core/include/agent/AgentLoop.h` (`WakeState`: *every duration in it is a duration and not a deadline*; `state()` and `restore()`); `lib/agent-core/src/AgentLoop.cpp` (`state()` ~L830: `holdForMs`, `failingForMs`; `restore()` ~L850: `holdUntil_ = now + saved.holdForMs`; the BackOff branch ~L280 and its 30 s, 60 s, 120 s steps; `wake()`'s *a wait the server asked for is still running* ~L779; `retrySeconds()`; the reboot guard, `kRebootAfterFailures` and `kRebootAfterFailingMs`); `test/test_loop` (the `WakeState` cases); `docs/library.md` §6 (*a device that sleeps*) and the `millis()` row. Read only: the Walter firmware's `components/agent/agent_backend.cpp` (where it saves and restores the state, and its `agent state` lines).
**Size:** one session.
**Model:** claude-opus-5

## Goal

8 November, the first repeater-v2 unit. It woke every 15 minutes all night (56 wakes since 20:37),
and meshmaster.dk heard from it on about every sixth: the server showed it *quiet*, and jobs waited
for hours. The Walter's console had it:

```
agent state: restored across the last sleep (1 restored, 0 saved, 0 refused)
  version 1; backoff 30000 ms, 1 consecutive failure(s) for 1436464 ms, hold 0 ms left
  counters: 31 heartbeat(s), 31 http failure(s), ...
```

A call that fails sets a hold of 30 s or more. `state()` saves what is left of it, `restore()` puts
it back against the new boot's clock, and **the sleep in between counts for nothing**. So a 30 s hold
is spent five or six seconds at a time, one short wake after another, and each of those wakes sends
nothing (*a wait the server asked for is still running*). At 15-minute wakes that is about 90 minutes
of silence after one failed call. At the Walter's usual 60 minutes it is about six hours, and with
the backoff at its 120 s ceiling a whole day. The wait was meant to be seconds.

The Heltec never sleeps and never calls `restore()`, so nothing changes for it.

## Deliverables

1. **`restore()` is told how long the board was away.**
   - A second argument: the milliseconds that passed between `state()` and `restore()` on a clock
     that runs through the sleep. The caller measures it; the library has no such clock. `restore(s)`
     with one argument keeps meaning *no time passed*, so a caller that does not know passes nothing
     and gets today's behaviour exactly.
   - The hold is what is left after that time: `holdForMs` minus the time away, and nothing below
     zero. A Retry-After the server named is treated the same way: it was a number of seconds of
     real time.
   - `failingForMs` gains the time away too: the run of failures lasted through the sleep. Check
     what that does to the reboot guard on a board that sleeps (`report.rebootWanted`, and what the
     Walter does with it), and say in the report that it does nothing worse than today. A
     deep-sleeping board boots fresh at every wake anyway.
2. **The words.** `WakeState`'s comment and `restore()`'s say the rule: a duration is re-based
   against the new clock *after* the time away is taken off it. `docs/library.md` says it where it
   explains `WakeState`. The log line *N s of a wait ... is still owed from before the sleep* is
   written only when something is still owed after the time away.
3. **Tests** (`test/test_loop`):
   - a failed call, `state()`, a new boot, `restore(s, 900000)`: the next wake heartbeats;
   - `restore(s, 10000)` after a 30 s hold: 20 s still owed, the wake sends nothing, and the next
     one after another 20 s does;
   - a Retry-After of 600 s and 900 s away: nothing owed;
   - `restore(s)` with one argument: exactly today's figures;
   - the time away is added to `failingForMs`.

## Acceptance criteria

- [ ] A hold or a Retry-After loses the time a board was away; one argument behaves as before (tests).
- [ ] The reboot guard on a sleeping board is read and reported; the Heltec's behaviour is unchanged.
- [ ] The comments and `docs/library.md` say the rule; the build and the host suite green.

## Out of scope

The Walter (its pile 10 syncs this and measures the time away). Why so many calls fail on the unit:
the Walter's pile 9 report 02 and its trace build.
