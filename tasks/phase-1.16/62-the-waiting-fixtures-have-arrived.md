# 62 — The waiting fixtures have arrived

**Depends on:** —
**Spec:** `tests/fixtures/contracts-ahead/README.md` (what waits, and what is done when the sync brings it), `tests/fixtures/contracts/README.md` and `MANIFEST.txt`, `tools/sync-fixtures.ps1`, `docs/upstream.md` (~L185) and `docs/library.md` (~L1439); `test/test_contract_fixtures/test_contract_fixtures.cpp` (~L23–52: the three directories; ~L154–291: the six lines, `kWaiting` and the four `WaitingFixtures` tests), `test/support/Fixtures.h` (~L31–33; `contract` ~L55; `contractAhead` ~L66), `test/test_config_reply/test_config_reply.cpp` (~L20–36, ~L90, ~L110), `test/test_envelopes/test_envelopes.cpp` (~L42–63, ~L644–646, ~L761), `test/test_job_results/test_job_results.cpp` (~L33–55), `test/test_update_ask/test_update_ask.cpp` (~L37, ~L402), `test/support/FakeUplink.h` (~L101), `platformio.ini` (~L376, ~L445: `AHEAD_FIXTURES_DIR`), `.gitattributes` (~L15–17); `tasks/phase-1.15/FOLLOWUPS.md` (its first item).
**Size:** one session, a short one.
**Model:** claude-opus-5

## Goal

`master` is red. On 19 November Thomas ran `tools\sync-fixtures.ps1 -Commit master` (`0edc7b0`,
*Sync fixtures*), which is the first half of what pile 1.15 left to do: it brought sixteen
files into `tests/fixtures/contracts/`, the six that tasks 56 and 61 had written ahead among
them. They are the same bytes as the six in `tests/fixtures/contracts-ahead/`: the two
repositories built the wire to the same lines.

The test that fails is the one that was put there to fail now:
`WaitingFixtures.NoneOfThemIsInTheCopiedTreeOrItsManifest`, *the sync has brought it, so point
the tests at the copied tree and delete the waiting one*. One of 840, and nothing else.

This task is the second half. It changes tests and words, and no line of the firmware.

## Deliverables

1. **The six are read from the copied tree** by every test that read them from the waiting
   one: `NeighboursResultTotal`, `HeartbeatUpdate`, `AgentHeartbeatUpdate`, `UpdateAsk`,
   `HeartbeatReplyUpdate`, `UpdateResult`. No test reads a contract from anywhere else.
2. **The six waiting files go.**
3. **The place for the next one stays**, empty: the directory with its README, its two
   tripwires (a waiting file no test holds to its task's line; a waiting file that is also in
   the copied tree) and the define. Two piles will run side by side again. The README's table
   says nothing waits, and how a line is added to it.
4. **The two relations between the files are held in the copied tree:** `AgentHeartbeatUpdate`
   is `AgentHeartbeat` with `HeartbeatUpdate` as its last member, and `HeartbeatReplyUpdate`
   holds `UpdateAsk` verbatim. The six lines typed into the test go: the copied tree is the
   contract, and its `MANIFEST.txt` holds it.
5. **The words** wherever they say that a fixture waits: `docs/upstream.md`, `docs/library.md`,
   the comments of the tests and of `FakeUplink.h`. This task's release note is one line, or
   none if a note is only for what an owner sees: say which in the report.
6. **Nothing is ticked in pile 1.15's `FOLLOWUPS.md`.** The report says its first item is done,
   and the reviewer ticks it.

## Acceptance criteria

- [ ] `pio test -e native` is green, with as many test cases as before or the report says which went and why (the four `WaitingFixtures` tests are counted by name).
- [ ] `tests/fixtures/contracts-ahead/` holds its README and no `.json` (test: the directory's own tripwires pass on an empty directory).
- [ ] A grep rail in the report: no file under `test/` opens one of the six names anywhere but the copied tree.
- [ ] Put back by hand, `UpdateAsk.json` in the waiting directory fails the build twice over: no test holds it to a line, and it is in the copied tree (shown in the report, not committed).
- [ ] The two relations hold against the copied files (tests).
- [ ] `tools\sync-fixtures.ps1 -Verify` passes for the three sets.
- [ ] No file under `src/` or `lib/` changes (the diff's own list, in the report).
- [ ] Both boards build, CI green on the pull request and on `master` after the merge.

## Out of scope

The ten other files the sync brought (`NoiseScope*`, `UnitReport*`, `FirmwareApplyProgress`,
`FirmwareOfferPayloadScanner`): they are a unit's, and nothing here reads them. Any change to a
fixture. The Walter's own waiting directory.
