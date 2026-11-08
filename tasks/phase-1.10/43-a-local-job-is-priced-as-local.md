# 43 — A local job is priced as local

**Depends on:** 40 (merged).
**Spec:** `lib/agent-core/src/AgentLoop.cpp` (`transactionsOf` ~L54, `estimateJobMs` ~L596, `wakeJobs` ~L690: the price a claimed job is checked against before it runs, `n x (minGapSeconds + loginMs)`); `lib/agent-core/include/agent/IMesh.h` (40's `answersLocally()`: true from `beginJob` to `endJob`, and so not known yet when the wake prices a job); `lib/agent-core/src/JobRunner.cpp` (`spend`, which 40 taught to skip the gate). Read only: the Walter firmware's `tasks/phase-9/FOLLOWUPS.md`, *The wake still prices a local job as air* (found while doing its 05), with the arithmetic.
**Size:** one session.
**Model:** claude-opus-5

## Goal

40 made a job the board answers locally skip the airtime gate while it runs. But the wake prices a
claimed job **before** it runs, with the gaps of the air: a 34-line `config.read` of a unit's own
repeater is priced at 34 × (20 s + 8 s), 952 s, though it takes seconds. The Walter's exchange is
given 1 200 s, so it fits today with 248 s to spare. A shorter bound, a longer key list or a larger
gap from the server would refuse, in seconds and at no transaction, the very job 40 made fast.

## Deliverables

1. **The wake can know before `beginJob` that a job is local.**
   - A defaulted `IMesh` virtual that answers for a job's repeater key without beginning the job (for
     example `answersLocallyFor(publicKeyHex)`, default `false`). Choose the shape and say why. Its
     contract must agree with 40's `answersLocally()`: true for a key exactly when a job begun for it
     would be local.
   - `AgentMesh` keeps the default.
2. **A local job is priced at its own cost:** its reply waits, no gaps and no transactions, in the
   check `wakeJobs` makes and in what it reports. A job that is not local: exactly as today.
3. **Tests:** the 34-line local `config.read` passes the check with a small budget that refuses the
   same job over the air; a non-local job is refused exactly as before; the Heltec's figures do not
   move.
4. **`docs/library.md`:** the local door's paragraph says a local job is priced as local.

## Acceptance criteria

- [ ] A local job is priced at its reply waits, decided before it runs, from one predicate that agrees with 40's (tests).
- [ ] Non-local pricing and the Heltec unchanged; docs updated; the host suite green.

## Out of scope

The Walter's `LinkMesh` implementing the new virtual (its pile 10, task 01).
