# MeshMaster agent firmware — an exit node that is one board

An exit node that is one board. A Heltec V3 (ESP32-S3, SX1262, WiFi, an OLED)
on USB power at a window joins WiFi, registers with
[meshmaster.dk](https://github.com/twinoak/meshmaster.dk), heartbeats, reports
the adverts it hears, and runs the jobs the server hands it — status,
telemetry, neighbours, trace, then logins and the writes — with no Raspberry Pi
and no companion cable. It speaks the agent protocol
([EXIT-NODES §6](https://github.com/twinoak/meshmaster.dk/blob/fbf4625a0352663e1c26bd6877e45e6c555d4f7c/spec/EXIT-NODES.md#6-agent--server-protocol),
[API §3](https://github.com/twinoak/meshmaster.dk/blob/fbf4625a0352663e1c26bd6877e45e6c555d4f7c/spec/API.md#3-agent-api-agentv1))
exactly as the .NET agent does, from the same `MeshMaster.Contracts` shapes, and
the server sees an ordinary `mesh`-kind exit node whose transport is `onboard`.

Decided by Thomas on 28 September 2026
([D-28](https://github.com/twinoak/meshmaster.dk/blob/fbf4625a0352663e1c26bd6877e45e6c555d4f7c/spec/DECISIONS.md#3-new-decisions)).
**The spec lives in twinoak/meshmaster.dk and stays there** — the protocol has
two ends and the other one is the server's. This repository is the board's end:
the firmware, its toolchain, its tests and its releases.
[`tasks/README.md`](tasks/README.md) is the table of how the two relate.

## Building it

[PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/)
and nothing else — MeshCore comes as a pinned dependency and the toolchain
downloads itself on the first build.

```bash
pio run -e heltec_v3     # the board
pio test -e native       # the tests that need no board
```

On Windows `pio test -e native` needs a C++ compiler the machine usually has
only inside a container;
[`tasks/RUNBOOK.md §2`](tasks/RUNBOOK.md) has the one-line `docker run` for it.

**Today the build is MeshCore's stock `companion_radio` example** for the
board — a green build to start from, not our firmware. Task 01 replaces it with
our application in `src/`.

## Flashing it

A release publishes, per board, a **merged** image for a board out of the box
(bootloader + partition table + application, flashed at `0x0`) and the
application image alone beside it, with `SHA256SUMS` and the one `esptool` line
to use. A tag `vX.Y.Z` on a commit CI has passed is what makes one; *Run
workflow* on `release.yml` rehearses it and publishes nothing.

The board is provisioned over its USB serial console — `set`, then `save` — not
over a captive portal. Its token comes from the exit node's registration page
on meshmaster.dk and is kept in NVS.

## Keeping up with MeshCore

MeshCore is a dependency pinned at one commit (`[meshcore]` in
`platformio.ini`): `companion-v1.17.1`, `d9296435`, the commit the field
repeater runs. It moves, and this firmware is meant to move with it, in three
tiers — a weekly workflow that opens the bump as a pull request, one command
that turns a red bump into a task, and the rare wire-protocol change that is a
meshmaster.dk change first. [`docs/upstream.md`](docs/upstream.md) is the page.

## The tasks

[`tasks/phase-1/README.md`](tasks/phase-1/README.md): six tasks from a board
that boots to an exit node that stays up, run unattended by
`tasks/run-all.ps1`, one headless session each. The repository itself was pile
4.2's task 65 in
[twinoak/meshmaster.dk](https://github.com/twinoak/meshmaster.dk/blob/fbf4625a0352663e1c26bd6877e45e6c555d4f7c/tasks/phase-4.2/65-bootstrap-the-firmware-repo.md).

| # | Task | Milestone |
|---|---|---|
| 01 | [the-board-speaks](tasks/phase-1/01-the-board-speaks.md) | boots, serial CLI, WiFi, time, TLS to meshmaster.dk with the token; nothing on the air |
| 02 | [an-exit-node-with-no-jobs](tasks/phase-1/02-an-exit-node-with-no-jobs.md) | **milestone 1**: registered, heartbeating, reporting adverts |
| 03 | [the-read-jobs](tasks/phase-1/03-the-read-jobs.md) | status, telemetry, neighbours, trace |
| 04 | [logins-and-the-config-read](tasks/phase-1/04-logins-and-the-config-read.md) | discover, acl.set, config.read; passwords in RAM only |
| 05 | [the-writes](tasks/phase-1/05-the-writes.md) | config.apply, cli, advert.request, clock.sync, password.set, reboot |
| 06 | [a-node-that-stays-up](tasks/phase-1/06-a-node-that-stays-up.md) | watchdog, reconnect, buffering, the field runbook, the Heltec V4 |

## What a session can and cannot check

A session can **build**, run the **native** tests, and read the MeshCore source
it builds against (on disk under `.pio/libdeps/<env>/MeshCore` after a build).
It cannot flash a board or hear a radio. Every task therefore ends with a *Not
verified here* list that is Thomas's to walk with the board on the bench, and
the firmware prints enough on its serial console that the walk is a matter of
reading.

## Rails that hold in every task

- **TLS is verified.** The server's certificate chain is checked against a
  bundled root (ISRG Root X1 for meshmaster.dk's Let's Encrypt chain, and the
  ESP32 certificate bundle beside it); `setInsecure()` and its cousins never
  appear in the tree, and a test greps for them.
- **Secrets stay where they are.** The agent token lives in NVS and is printed
  by no command (`get meshmaster.token` says *set*); a repeater's password lives
  in RAM for the job and is overwritten after; the serial log redacts as the
  .NET agent does. Flash encryption is documented as the owner's choice, not
  assumed.
- **Nothing the mesh says is logged as text.** Overheard channel and contact
  messages are counted and dropped, never printed. `MESH_PACKET_LOGGING` and
  `MESH_DEBUG` stay off, whatever upstream's examples do.
- **One send per command.** A `set` sent twice is a change made twice: single
  send, strictly increasing sender timestamps, the region loader's terminator.
- **The contract is the .NET agent's.** Every envelope is checked against
  `MeshMaster.Contracts`' fixtures, copied byte for byte into
  [`tests/fixtures/contracts/`](tests/fixtures/contracts/README.md), and a
  difference is a bug in the firmware, not a second dialect.

## Licence

MIT — [`LICENSE`](LICENSE), the one GitHub wrote when the repository was created
(holder: *twinoak*; twinoak/meshmaster.dk's says *Thomas Eg*, and both are the
same person's). MeshCore is MIT too and is a dependency, not a fork: nothing of
its tree is vendored here.
