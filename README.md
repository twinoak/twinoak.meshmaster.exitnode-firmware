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

The application is `src/`: the serial console and its settings, WiFi and the
clock, the TLS client, the OLED, and the mesh in companion role — MeshCore's
core, the Heltec V3 variant and its display driver are compiled from the pinned
package beside it, and no example of upstream's is built. Four files of `src/`
are host-portable (the key table, the console parser, the ISO-8601 helper and
the config-reply parser) and are what the native tests exercise; the rest needs
a board.

## Flashing it

From a build on this machine, with the board on USB:

```bash
pio run -e heltec_v3 -t upload            # add --upload-port COM5 if there is more than one port
pio device monitor -b 115200              # the console; Ctrl+C leaves it
```

From CI's artifact (`firmware-heltec_v3-<sha>`, the application image alone),
onto a board that already has a bootloader and partition table — that is, any
board that has been flashed once by either line above:

```bash
esptool.py --chip esp32s3 --port COM5 --baud 921600 write_flash 0x10000 firmware.bin
```

A release publishes, per board, a **merged** image for a board out of the box
(bootloader + partition table + application, flashed at `0x0`) and the
application image alone beside it, with `SHA256SUMS` and the one `esptool`
line to use. A tag `vX.Y.Z` on a commit CI has passed is what makes one; *Run
workflow* on `release.yml` rehearses it and publishes nothing.

Flashing does not touch the settings (NVS) or the identity (SPIFFS), so a board
keeps its WiFi, its token and its key across updates. `erase confirm` on the
console forgets the settings; the identity stays even then.

## Provisioning it

Over the USB serial console — 115 200 baud, one command per line, a `>` prompt,
[`docs/console.md`](docs/console.md) for every command and key — not over a
captive portal. The token comes from the exit node's registration page on
meshmaster.dk (`/exit-nodes/new`) and is kept in NVS. A board from nothing:

```console
> status
firmware  meshmaster-agent 0.0.0-dev on MeshCore companion-v1.17.1 (d9296435)
board     heltec_v3, Power-on reset, up 0d 00:00:41
identity  7f3a9c02e1b44d9a0f6c2b7d8e9a1c3f5d7b9e0a2c4e6f8a0b1c2d3e4f5a6b7c
radio     869.618 MHz, bw 62.5 kHz, sf 8, cr 8, tx 22 dBm; companion role, no repeat
frames    rx 3 (advert 2, grp-txt 1, txt 0, ack 0, path 0, trace 0, other 0), tx 0, noise floor -112 dBm, frame log off
wifi      off, wifi.ssid is unset
time      no time (sntp not started, no wifi)
server    not configured (meshmaster.url unset, meshmaster.token unset)
heap      free 231212, min ever 228004, largest block 110580
log       level info, frames off, oled on
> set wifi.ssid Twinoak
ok wifi.ssid
> set wifi.pass ********************
ok wifi.pass
> set meshmaster.url https://meshmaster.dk
ok meshmaster.url
> set meshmaster.token mma_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
ok meshmaster.token
> get meshmaster.token
meshmaster.token set
> get wifi.pass
wifi.pass set
> get
wifi.ssid Twinoak
wifi.pass set
meshmaster.url https://meshmaster.dk
meshmaster.token set
radio.freq unset (869.618)
radio.bw unset (62.5)
radio.sf unset (8)
radio.cr unset (8)
radio.tx unset (22)
log.level unset (info)
log.frames unset (off)
> set radio.tx 23
error: radio.tx: out of range, -9..22
> set meshmaster.url http://meshmaster.dk
error: meshmaster.url: the scheme must be https://
> status
firmware  meshmaster-agent 0.0.0-dev on MeshCore companion-v1.17.1 (d9296435)
board     heltec_v3, Power-on reset, up 0d 00:03:10
identity  7f3a9c02e1b44d9a0f6c2b7d8e9a1c3f5d7b9e0a2c4e6f8a0b1c2d3e4f5a6b7c
radio     869.618 MHz, bw 62.5 kHz, sf 8, cr 8, tx 22 dBm; companion role, no repeat
frames    rx 9 (advert 6, grp-txt 3, txt 0, ack 0, path 0, trace 0, other 0), tx 0, noise floor -112 dBm, frame log off
wifi      connected to Twinoak, 192.168.1.20, rssi -61 dBm (1 connects, 0 drops)
time      2026-09-28T12:03:14Z (sntp)
server    https://meshmaster.dk: config ok at 12:03:14, agent 17 "Window at Grenaa", transport onboard, budget 2/min gap 20 s, poll 30 s, root ISRG Root X1
heap      free 187432, min ever 180020, largest block 98304; after the tls call 187432
log       level info, frames off, oled on
```

Between the `set` lines and the second `status`, the log on the same port says
`wifi: connected`, `clock: ... from sntp` and `server: config ok ...` as they
happen. The value of a secret is typed once, echoes as asterisks, and is
printed by nothing afterwards; the two `error:` lines are what a wrong value
gets. Every `set` takes effect at once, so no reboot is part of provisioning.
The session above is replayed by the native tests against the parser, reply for
reply.

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
  ESP32 core's certificate bundle beside it); the client is never told to
  accept an unverified chain, and a native test greps the tree for the calls
  and flags that would tell it so (`test/test_tls_policy`).
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
