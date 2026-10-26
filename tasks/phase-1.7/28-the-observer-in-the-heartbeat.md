# 28 — The observer in the heartbeat

**Depends on:** 27, and meshmaster.dk's **pile 4.17 merged** (its task 136: `JobResultReport.failedAt`; its task 140: `AgentHeartbeat.observer` and `AgentHeartbeatObserver.json`). Before anything else, look: if `tests/fixtures/contracts/AgentHeartbeatObserver.json` is not on meshmaster.dk's `master` (`gh api repos/twinoak/meshmaster.dk/contents/tests/fixtures/contracts`), stop on purpose: write `tasks/phase-1.7/logs/STOPPED-28.md` naming what is missing (RUNBOOK.md §5), change nothing else, and end.
**Spec:** meshmaster.dk at `master` (read-only): `spec/DECISIONS.md` D-46, `src/MeshMaster.Contracts/Envelopes.cs` (`AgentHeartbeat`, `JobResultReport`), the fixtures and their README; this repository's `tools/sync-fixtures.ps1`, `lib/agent-core/` (`AgentLoop::buildHeartbeat`, `HeartbeatIdentity`, `JobRunner`'s login and request steps, `Envelopes.cpp`, `JobResults.cpp`), `src/observer/` (27), `CHANGELOG.md`, `docs/library.md`, `docs/observer.md`.
**Size:** one session.
**Model:** claude-opus-5

## Goal

MeshMaster shows an exit node's observer from what the heartbeat says, and
tells a silent login from a lost request by the result's `failedAt`. Send
both, and close the pile.

## Deliverables

- **Fixtures synced** from meshmaster.dk's `master` (`tools/sync-fixtures.ps1
  -Commit master`; the manifest's commit moves; `-Verify` green).
- **`observer` in the heartbeat**, through the library without the library
  knowing what an observer is: a provider the consumer may give the loop (a
  callback or an interface of one method) that writes the object; the Heltec
  gives one when a slot is configured and none otherwise, so a node with the
  observer off sends exactly `AgentHeartbeatNoWake.json`'s bytes. Held to
  `AgentHeartbeatObserver.json` byte for byte.
- **`failedAt` in every failed result**: `login` when no login was answered,
  `request` when one was and the request was not; absent on success and on
  results with no login (`trace`). Tests in `test_runner` per job class.
- **`agent-core` 0.4.0** (the provider is an interface change): CHANGELOG —
  what a consumer adds (nothing, if it has no observer), the fixtures' commit
  (*the wire moved*: two optional fields); version in all three places;
  `docs/library.md`.
- **`docs/observer.md`** finished: the Danish guide's lines as this firmware
  takes them, `mqtt.status` read line by line, and what MeshMaster's Observer
  card shows.
- **README**: the observer in the feature list, off by default.

## Acceptance criteria

- [ ] `-Verify` green at the new commit; `test_contract_fixtures` green.
- [ ] A heartbeat with the observer off equals `AgentHeartbeatNoWake.json`; with two slots, `AgentHeartbeatObserver.json`'s shape.
- [ ] `failedAt` per class, tested; `pio test -e native`, the host matrix and both board builds green; sizes against `v0.3.0` in the report.

## Out of scope

Configuring the observer from MeshMaster.
