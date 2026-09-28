# Phase 1 — the onboard exit node, from a dead board to one that stays up

The firmware's first phase: six tasks that take a Heltec V3 from "it boots and
prints a prompt" to an exit node on meshmaster.dk that answers every job the
.NET agent answers and survives a week on a windowsill. The repository itself,
the toolchain, CI and the release were pile 4.2's task 65 in
[twinoak/meshmaster.dk](https://github.com/twinoak/meshmaster.dk/blob/fbf4625a0352663e1c26bd6877e45e6c555d4f7c/tasks/phase-4.2/65-bootstrap-the-firmware-repo.md);
these six were written there too, as `tasks/firmware/F-01…F-06`, and this is
where they live now.

## Order and dependencies

| # | Task | Depends on | Milestone |
|---|---|---|---|
| 01 | [the-board-speaks](01-the-board-speaks.md) | the repository | boots, serial CLI, WiFi, time, TLS to meshmaster.dk with the token; nothing on the air |
| 02 | [an-exit-node-with-no-jobs](02-an-exit-node-with-no-jobs.md) | 01 | **milestone 1**: registered, heartbeating, reporting adverts — an exit node on meshmaster.dk feeding unknown nodes |
| 03 | [the-read-jobs](03-the-read-jobs.md) | 02 | status, telemetry, neighbours, trace claimed and answered |
| 04 | [logins-and-the-config-read](04-logins-and-the-config-read.md) | 03 | discover, acl.set, config.read; the ACL restore; passwords in RAM only |
| 05 | [the-writes](05-the-writes.md) | 04 | config.apply, cli, advert.request, clock.sync, password.set, reboot |
| 06 | [a-node-that-stays-up](06-a-node-that-stays-up.md) | 05 | watchdog, reconnect, buffering, the field runbook, the Heltec V4 target |

01 → 02 → 03 → 04 → 05 → 06, strictly: each builds on the loop the one before
it left. The V4 target arrives in 06 — it needs MeshCore's own
`boards/heltec_v4.json`, which a consumer of the package cannot put on
`boards_dir`, so that task vendors the board definition into `boards/` here.

```powershell
.\tasks\run-all.ps1 -Phase 1
```

## Models

- **Fable** — 01, 04, 05. The three whose mistakes would be security mistakes:
  where the token lives and what the console prints (01), a repeater's password
  in RAM and the ACL restore (04), and one send per command (05).
- **Opus** — 02, 03, 06.

## The exit criterion

A Heltec V3 on USB power at a window, registered with meshmaster.dk, that has
answered every job type of
[API §3](https://github.com/twinoak/meshmaster.dk/blob/fbf4625a0352663e1c26bd6877e45e6c555d4f7c/spec/API.md)
at least once against a real repeater, and has been up for a week without a
hand on it. Only Thomas can say the last two: every task ends with a *Not
verified* list that is his to walk with the board on the bench.

## Follow-ups

Review findings go into [FOLLOWUPS.md](FOLLOWUPS.md), tagged with the task that
must pick them up.
