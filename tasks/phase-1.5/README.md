# Pile 1.5 — the agent core as a library

Written on 14 October from proposal 03 of the repeater-v2 work
(`twinoak.meshcore.standard-repeater-design-v2`,
`software/spec/proposals/03-agent-core-as-a-library.md`), accepted by Thomas
the same day. Five tasks.

**Prerequisite:** pile 1.4 merged (it is: PR #16, v0.1.4). Task 18 also waits
for meshmaster.dk's pile 4.14, task 120 (the two heartbeat fields) — start this
pile after that has merged, or run 15–17 and let 18 stop until it has.

The Walter of a repeater-v2 unit is to be a MeshMaster agent, and everything
hard about being one is in `src/agent/` of this repository, proven on the
air. What stops the Walter using it is three assumptions around the logic —
the mesh in the same process, WiFi for the uplink, Arduino for the platform.
This pile puts an interface in front of each, moves the portable files into
`lib/agent-core/`, compiles the whole agent on the host against fakes for the
first time, and gives the loop a bounded *one wake* mode for a device that
sleeps. **The Heltec's behaviour does not change**: every existing native
test passes untouched and the board image is the same size to within three
vtables. That is the proof, and every task asserts it.

## Order and dependencies

| # | Task | Depends on | What it leaves behind |
|---|---|---|---|
| 15 | [the-seams](15-the-seams.md) | — | `IMesh`, `IUplink` (with extend and progress), `IPlatform`, `IJobExtension`; `JobRunner` and `AgentLoop` on the interfaces, settings read as values, the scratch as state; `sendCli` returns its timestamp; 08 superseded |
| 16 | [the-library](16-the-library.md) | 15 | `lib/agent-core/` with `library.json`, an IDF `CMakeLists.txt` and `idf_component.yml`; a boundary test; `[env:native]` compiles it whole; the rails' paths moved |
| 17 | [the-runner-on-the-host](17-the-runner-on-the-host.md) | 16 | `FakeMesh`, `FakeUplink`, `FakePlatform`; the runner and the loop end to end on the host, every rule of EXIT-NODES §5–§7 with a test; `docs/host.md` |
| 18 | [one-wake](18-one-wake.md) | 17, meshmaster.dk 120 | fixtures synced; `AgentLoop::wake()` with a budget, no job left leased, backoff state across sleeps; the Heltec's heartbeat bytes unchanged |
| 19 | [the-handover](19-the-handover.md) | 18 | `docs/library.md` for a consumer; README; the version to tag (`v0.2.0`) |

15 → 16 → 17 → 18 → 19.

```powershell
.\tasks\run-all.ps1 -Phase '1.5'
```

(The quotes: PowerShell reads a bare `1.5` as a number.)

## Models

- **Fable** — 15. The interfaces are the contract two firmwares will share;
  a wrong cut here is paid for twice.
- **Opus** — 16, 17, 18, 19.

## The exit criterion

`pio test -e native` runs the whole agent — loop, runner, every job type —
against fakes in under a minute; `pio run` for both boards is within 1 kB of
v0.1.4; `docs/library.md` lets the Walter's phase 4 write its three
implementations without reading this repository's source; Thomas tags
`v0.2.0`.

## Watch

- **08.** The host harness branch `origin/task/p1.1-08-the-host-harness` is
  superseded by 15–17 and should be deleted (Thomas). Nothing from it is
  merged.
- **The order with meshmaster.dk.** 18 syncs the contract fixtures after
  pile 4.14's 120 has landed; until then `tools/sync-fixtures.ps1 -Verify`
  on `master` there would fetch fixtures without the two fields and 18 would
  have nothing to write.
- **The fakes are citations.** `FakeMesh` answers as the pinned MeshCore
  does, by fixture or by a cited function; a MeshCore bump re-reads them
  (`tasks/sync/TEMPLATE.md`).

## Follow-ups

Review findings go into [FOLLOWUPS.md](FOLLOWUPS.md), tagged with the task that
must pick them up.
