# 53 — The repeater's clock

**Depends on:** nothing in this pile.
**First:** in `meshmaster.dk` (`twinoak/meshmaster.dk`: the sibling folder, or through `gh api`; read only), check that `master` has `tasks/phase-4.37/reports/210.md`. If it does not, write `logs/STOPPED-53.md` saying so, change nothing else, and end. 210 sets the contract this task follows.
**Spec:** meshmaster.dk, read only, as merged with 210: `spec/API.md` §3 (a report's clock), `spec/EXIT-NODES.md` §7; this repository: `src/mesh/AgentMesh.cpp` (`takeResponse`: the first four bytes of a response are kept as `MeshReply.tag`, and for a login they are the repeater's own clock), `lib/agent-core/include/agent/IMesh.h` (`MeshReply`), `lib/agent-core/src/JobRunner.cpp` (`awaitLoginReply` ~L223, `loginOnce` ~L268, the reachability probe's blank login, the discover's logins), `lib/agent-core/src/JobResults.cpp` and `JobEnvelopes.cpp` (the report), the platform's clock as the server corrects it, `test/support/FakeMesh.h`, `test/test_runner`, `tests/fixtures/contracts`, `docs/library.md`, `CHANGELOG.md`.
**Size:** one session, short.
**Model:** claude-opus-5

## Goal

A login reply begins with the repeater's own clock. This firmware reads those four bytes at every
login and keeps them nowhere. meshmaster.dk's 210 gives them a place in a job's report, so that it
can track how far out a repeater's clock is and warn (Thomas, 13 November).

## Deliverables

1. **At every login of its own that is answered**, the report carries the repeater's clock as the
   reply gave it and this board's own time at that moment, as 210's contract spells them. Two
   numbers, no arithmetic here.
2. **Not otherwise.** A reused session made no login and reports none. A job through the local
   door logs in to nothing and reports none. A board whose own clock the server has not yet
   corrected reports none.
3. **Whichever login it was**: the job's own, the one after *no reply on a reused session*, the
   discover's. Where a job made more than one, say which is reported and why.
4. **In the library**, so the Walter has it at its next sync.
5. **The contract fixtures** synced from meshmaster.dk at the commit that has 210.
6. **The words.** `docs/library.md`, `CHANGELOG.md` (the four questions), and this task's line in
   `release-notes/unreleased/` if task 52 has landed.

## Acceptance criteria

- [ ] A job that logs in reports the two numbers, and they are the reply's four bytes and the fake platform's time (test).
- [ ] A reused session, the local door, and a board with no corrected time each report none (tests).
- [ ] A late reply that 47 drops is not taken for the clock (test).
- [ ] The fixtures pass with and without the field.
- [ ] The runbook's verification passes on this machine (`tasks/RUNBOOK.md` §2). CI green.
