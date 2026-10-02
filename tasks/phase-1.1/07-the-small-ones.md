# 07 — The small ones

**Depends on:** nothing.
**Spec:** `docs/upstream.md`, `tests/fixtures/contracts/README.md`, meshmaster.dk `spec/API.md` §3 and task p4.3-68 (`StatusPayload.version`), the review in `tasks/phase-1/FOLLOWUPS.md`.
**Size:** one session, short.
**Model:** claude-opus-5

## Goal

Four things the phase-1 review left as one-line boxes.

## Deliverables

- **The contract fixtures at `b04799f`.** `tools/sync-fixtures.ps1 -Commit
  b04799f -Set contracts`: meshmaster.dk's task p4.3-71 added seventeen
  envelope files (`ObservationBatch`, `Observation`, `ObservationFields`,
  `ClaimResponse`, `RepeaterRef`, `JobCredential`, `JobTimeouts`,
  `RadioParameters`, `AgentBudget`, …) and changed none; the manifest
  moves and `-Verify` passes.
- **`test_envelopes` reads the files.** The observation batch (task 02)
  and the claim response (task 03) are held to `ObservationBatch.json` and
  `ClaimResponse.json` byte for byte, as the heartbeat already is to
  `AgentHeartbeat.json`; the inline expectations go. `JobCredential.json`
  carries all five fields at once (its README says so): the parser test
  reads it as a shape and does not treat it as a plausible claim.
- **`StatusPayload.version`.** meshmaster.dk's task 68 made the acl
  status read fetch the owner-info request once a day: the claim's
  `StatusPayload` carries `version: false` when the server does not want
  it this time, and nothing when it does. The status handler honours it —
  one request instead of two on forty-seven reads of forty-eight — and
  the native test for the status result covers both.
- **The merged branches.** `task/p1-02` … `task/p1-06` are still on
  origin; delete them (`gh api -X DELETE` or `git push origin --delete`),
  and give `tasks/run-all.ps1` the branch delete after a merge that
  meshmaster.dk's copy has, so the next phase does not leave them.

## Acceptance criteria

- [ ] `tools/sync-fixtures.ps1 -Verify` green at `b04799f`; `pio test -e native` reads the two files.
- [ ] A claim with `version: false` makes one status request against the fake; without the field, two.
- [ ] `git ls-remote --heads origin` lists `master` and nothing of phase 1.
- [ ] `pio run -e heltec_v3`, `pio run -e heltec_v4` and `pio test -e native` green; CI green.

## Out of scope

Anything a board can settle.
