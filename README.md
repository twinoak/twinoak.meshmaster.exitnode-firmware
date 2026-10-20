# MeshMaster exit-node firmware — an exit node that is one board

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
pio run -e heltec_v3     # the first board
pio run -e heltec_v4     # the second: same firmware, MeshCore's V4 variant
pio test -e native       # the tests that need no board
```

The V4's board definition is the one thing of MeshCore's that is copied into
this repository — PlatformIO reads `boards_dir` before it clones a dependency,
so `board = heltec_v4` cannot name a file inside one
([`boards/README.md`](boards/README.md)). A V4 out of the box transmits at
**10 dBm**, not the V3's 22: its external PA is on for every transmission and
upstream's own note says 10 at the chip is 22 dBm at the socket. The range
`radio.tx` accepts is the same on both.

On Windows `pio test -e native` needs a C++ compiler the machine usually has
only inside a container;
[`tasks/RUNBOOK.md §2`](tasks/RUNBOOK.md) has the one-line `docker run` for it.

Those tests are not units alone. Since pile 1.5 they run **the whole agent** —
the loop, the runner, every one of the thirteen job types, the login sequence,
the ACL restore, the airtime gate, the offline buffer, the backoff — against a
scripted repeater, a scripted server and a clock that only moves when a test
says so, with nothing sleeping and no board in the room:
[`docs/host.md`](docs/host.md) says what those fakes are, what they are not, and
how a consumer of the library reuses them.

The tree is in two halves. `src/` is **this board**: the serial console and its
settings, WiFi and the clock, the TLS client and the root it verifies against,
the OLED, the mesh in companion role, the watchdog, and where a log line goes —
MeshCore's core, the Heltec variant and its display driver are compiled from the
pinned package beside it, and no example of upstream's is built.

[`lib/agent-core/`](lib/agent-core/library.json) is **the agent**, a library
with a name and a version of its own since pile 1.5 — the next section is what
it is and who it is for. The files of `src/` the native tests still reach are
the key table, the console parser, the OLED's four lines and the version
string; the rest of `src/` needs a board.

## The library

Since pile 1.5 the portable half of the agent is
[`lib/agent-core/`](lib/agent-core/library.json), a library with a manifest, an
ESP-IDF component file and a version of its own: the job runner and the agent
loop, the envelopes and results of the agent API, the reply codec, the ISO-8601
helper, the credential buffer, the config read, the apply engine and the CLI
reply rules, the airtime budget, the session cache, the observation ring, the
rate limiter, the offline result buffer, the log ring and the log's formatting,
the backoff, and the policy that reads the server's status codes. It stands on
three seams — `lib/agent-core/include/agent/IMesh.h`, `IUplink.h`,
`IPlatform.h` — with this board's mesh, WiFi client and ESP32 behind them; it
includes nothing of Arduino, the ESP-IDF, WiFi, NVS or MeshCore
(`test/test_library` is that boundary, as an allow list over every include);
and `pio test -e native` compiles and runs the whole of it on a host.

**A second firmware takes it at a tag**, the way this one takes MeshCore.
[`docs/library.md`](docs/library.md) is the page for that firmware: what it
gets and what it must provide, the three interfaces method by method, the
lifecycle, the rails it inherits and must keep, the fixtures that are the wire
contract, and what a bump may change.
[`docs/upstream.md`](docs/upstream.md) says what pinning it means and
[`CHANGELOG.md`](CHANGELOG.md) is what a consumer watches. The first consumer
is the Walter of a repeater-v2 unit, whose platform is the ESP-IDF and whose
mesh is a RAK on a serial link — which is why there are three interfaces and
not one.

**Where things stand at `v0.3.0`.** Pile 1.6 is the library as its first
consumer found it: it builds in a `gnu++` dialect and at `-O2` on GCC 13 now
(20), a wake spends no byte and no second outside its budget at the job step
(21), a device that sleeps can keep one image of results and adverts across the
sleep (22, `agent/IRetained.h`), and the interface can say what a firmware *is*
and what its mesh cannot carry (23). **The Heltec does none of those things**:
it does not sleep, it passes no store, its mesh carries everything and its
heartbeat still says `onboard`. What did change for it is one thing, and it is
on the wire: meshmaster.dk moved its envelopes to .NET's relaxed JSON encoder,
so a `+`, a `>` and a Danish letter travel as themselves instead of as
`\uXXXX` (24). A CLI reply that starts `> ` is five bytes shorter, and a repeater
called Grenå costs four fewer in every result that names it. Against `v0.2.0`
the V3 image is **336 bytes more RAM and 1 892 bytes more flash** (the V4, 336
and 1 884), every byte of it named in `tasks/phase-1.6/reports/`.

**Where things stood at `v0.2.0`.** The tree moved and the behaviour did not.
The Heltec does exactly what it did at `v0.1.4`: every native test that existed
before this pile passes with **no assertion changed**, and against that release
the V3 image is **208 bytes more RAM and 8.4 kB less flash** (the V4, 208 and
8.3 kB). Both figures are one thing: 17 kB of the agent's buffers were
*initialised* data that the image carried and the boot copied into RAM, and as
members of one scratch object they are zero-initialised instead — `.dram0.data`
−16 896, `.dram0.bss` +17 104 — against which code and read-only data grew 8.3
kB for the seams, the two new uplink calls and the one-wake mode.
`tasks/phase-1.5/reports/` has it task by task and symbol by symbol.

What is new is where the agent *lives* and how much of it can be tested without
a board — the host suite now runs the loop, the runner and all thirteen job
types end to end against a scripted repeater, a scripted server and a clock
that only moves when a test says so ([`docs/host.md`](docs/host.md)) — and that
the loop has a second way to be driven, `wake(intent, budget)`, for a device
that sleeps. The Heltec does not call it, and the linker leaves it out of the
board image altogether.

## Flashing it

From a build on this machine, with the board on USB (`-e heltec_v4` for a V4):

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

A release publishes, per board, a **merged** image (bootloader + partition table
+ application, flashed at `0x0`) and the **application** image alone beside it,
with `SHA256SUMS` and the one `esptool` line to use. A tag `vX.Y.Z` on a commit
CI has passed is what makes one; *Run workflow* on `release.yml` rehearses it
and publishes nothing.

**Which of the two.** The merged image is for a board **out of the box, or one
coming from other firmware** — the repeater image, a companion build — because
it carries the bootloader and the partition table that the application image
presumes are already there. The application image at `0x10000` is the upgrade
for a board already running this firmware; so is `pio run -t upload`, which
writes the same region. **Either keeps the settings and the identity.** This
firmware's partition table ([`boards/README.md`](boards/README.md)) puts NVS —
the WiFi credentials, the server URL, the token — above the application, where
a merged image, which ends where the application ends, cannot reach it; the
identity in SPIFFS sits above that, at the same offset as in every table this
firmware has ever had, which is why a repeater's ACL entry for this board holds
through any reflash. CI holds both on every build (`tools/check-partitions.py`).

**The one exception, once per board.** A board provisioned under `v0.1.1` or
earlier carries the framework's default table, which keeps NVS at `0x9000` —
inside the span `esptool`'s merge fills with `0xFF`. The **first merged flash**
of a release with this firmware's table onto such a board **forgets the
settings**: it writes the new table, and NVS is now read from a region nothing
ever wrote, so the board comes up with `wifi off, wifi.ssid is unset` and
`server not configured` and is provisioned again from the console —
`set wifi.ssid`, `set wifi.pass`, and a **new** token, because the old one is
gone from the board and the server keeps only its hash: *Generate a new token*
on the exit node's page on meshmaster.dk, then `set meshmaster.token`. The
identity is untouched — `status` shows the same 64 hex characters before and
after, and the repeater's ACL still holds the board — so the exit node is
itself again, associations and all, and not a fresh registration. Upgrading
such a board with the application image instead keeps its old table and its
settings, and postpones the re-provisioning to its next merged flash. After
that one flash, no image reaches the settings again. `erase confirm` on the
console forgets the settings on purpose; the identity stays even then. What any
reflash loses is what was in RAM: results and observations this board had not
posted yet, and the reboot log `reboots` prints
([`docs/field.md`](docs/field.md)).

## Provisioning it

Over the USB serial console — 115 200 baud, one command per line, a `>` prompt,
[`docs/console.md`](docs/console.md) for every command and key — not over a
captive portal. The token comes from the exit node's registration page on
meshmaster.dk (`/exit-nodes/new`) and is kept in NVS; the server is
`https://meshmaster.dk` unless `meshmaster.url` says otherwise (a staging
installation, a test stack behind a certificate the board can verify). A board
from nothing:

```console
> status
firmware  meshmaster-agent 0.0.0-dev on MeshCore companion-v1.17.1 (d9296435)
board     heltec_v3, Power-on reset, up 0d 00:00:41
identity  7f3a9c02e1b44d9a0f6c2b7d8e9a1c3f5d7b9e0a2c4e6f8a0b1c2d3e4f5a6b7c
radio     869.618 MHz, bw 62.5 kHz, sf 8, cr 8, tx 22 dBm; companion role, no repeat
frames    rx 3 (advert 2, grp-txt 1, txt 0, ack 0, path 0, trace 0, other 0), tx 0, noise floor -112 dBm, frame log off
wifi      off, wifi.ssid is unset
time      no time (sntp not started, no wifi)
server    not configured (meshmaster.token unset)
heap      free 203980, min ever 200772, largest block 110580
log       level info, frames off, oled on
> set wifi.ssid Twinoak
ok wifi.ssid
> set wifi.pass ********************
ok wifi.pass
> set meshmaster.token mma_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
ok meshmaster.token
> get meshmaster.token
meshmaster.token set
> get wifi.pass
wifi.pass set
> get
wifi.ssid Twinoak
wifi.pass set
meshmaster.url unset (https://meshmaster.dk)
meshmaster.token set
radio.freq unset (869.618)
radio.bw unset (62.5)
radio.sf unset (8)
radio.cr unset (8)
radio.tx unset (22)
log.level unset (info)
log.frames unset (off)
display.timeout unset (0)
> set radio.tx 23
error: radio.tx: out of range, -9..22
> set meshmaster.url http://meshmaster.dk
error: meshmaster.url: the scheme must be https://
> status
firmware  meshmaster-agent 0.0.0-dev on MeshCore companion-v1.17.1 (d9296435)
board     heltec_v3, Power-on reset, up 0d 00:03:10
identity  7f3a9c02e1b44d9a0f6c2b7d8e9a1c3f5d7b9e0a2c4e6f8a0b1c2d3e4f5a6b7c
radio     869.618 MHz, bw 62.5 kHz, sf 8, cr 8, tx 22 dBm; companion role, no repeat
frames    rx 32 (advert 23, grp-txt 6, txt 0, ack 0, path 0, trace 0, other 3), tx 3, noise floor -112 dBm, frame log off
wifi      connected to Twinoak, 192.168.1.20, rssi -61 dBm (1 connects, 0 drops)
time      2026-09-28T12:03:14Z (sntp)
server    https://meshmaster.dk: config ok at 12:03:14, agent 17 "Window at Grenaa", transport onboard, budget 2/min gap 20 s, poll 30 s, root ISRG Root X1
heartbeat last at 12:03:16, 4 answered, poll 120 s; 20 observation(s) posted, 3 waiting, 0 dropped
jobs      2 run, 3 transaction(s); last job 12346 telemetry ok after 1 tx
heap      free 160104, min ever 152280, largest block 90112; after the tls call 160104
log       level info, frames off, oled on
> mesh
adverts   23 heard, 3 waiting to be posted (ring 64), 0 dropped, 0 undecodable
frames    rx 32, tx 3, overheard 6 (counted, never read)
last five newest first: key, name, role, hops, snr, rssi, heard
  a1b2c3d4e5f6  Grenaa Chimney #1                repeater   0 hop(s)  snr 7.25  rssi -84  41 s ago
> server
state     running
heartbeat last 12:03:16 (65 s ago), 4 answered; jobs waiting no
next      in 55 s (poll 120 s, budget 2/min gap 20 s)
batch     last 12:04:19, 20 item(s); 1 batch(es), 20 observation(s) posted
pending   3 of 64 in the ring, 0 dropped, 23 heard in all
jobs      1 claim(s), 2 job(s) run, 0 claim(s) unanswered; `jobs` prints the last ten
errors    overheard 6, wifiReconnects 0, httpFailures 0, observationsDropped 0, resultsDropped 0, reboots 0; `errors` has the rest
requests  8 in the last minute of the 120 the policy allows, 0 held back; 0 batch(es) refused
> jobs
budget    2/min, gap 20 s; 3 transaction(s) sent, 20 s waited for the budget
claims    1 made, 2 job(s) run, 0 result(s) not taken first time
buffer    0 result(s) waiting of 8, 0 of 12288 bytes; 0 dropped, 2 held in all
types     status, telemetry, neighbours, config.read, config.apply, cli, trace, discover, advert.request, clock.sync, password.set, acl.set, reboot
last ten  newest first: id, type, outcome, transactions, when
  12346    telemetry   ok            1 tx  12:05:11
  12345    status      ok            2 tx  12:04:48
> reboots
this boot Power-on reset, up 0d 00:05:41; watchdog 90 s on the loop task
counted   0 restart(s) since the last power cut, 0 of them this firmware's own
log       empty: the RTC memory did not survive, which is what a power cut looks like
last five none remembered
```

Between the `set` lines and the second `status`, the log on the same port says
`wifi: connected`, `clock: ... from sntp`, `server: config ok ...` and
`observations: N posted` as they happen. The value of a secret is typed once,
echoes as asterisks, and is printed by nothing afterwards; the two `error:`
lines are what a wrong value gets. Every `set` takes effect at once, so no
reboot is part of provisioning. The session above is replayed by the native
tests against the parser, reply for reply.

## What the display says

The OLED **stays on**. Four lines, written for the window a board is mounted in,
redrawn once a second:

```
ID: 46F6     12:03:14
WiFi: yes Online: yes
Heartbeat: 12:03:14
Idle, last 12:34:56
```

Line one is the first four characters of the companion key — the prefix
meshmaster.dk shows an exit node by — and the clock, or `no time` until it is
set; `no identity` alone when the radio made none. Line two is the two
questions worth asking at a glance: *WiFi* is the link, *Online* is whether the
last call to meshmaster.dk was answered. Line three is when the server last
heard from this board, `Heartbeat: none` until the first. Line four is what it
is doing — `Job 1234 acl.set` while a job is on the air, `Idle, last 12:34:56`
otherwise, `Idle, no jobs yet` before any — and when it is doing nothing
because something is wrong, which of the four it is: `token revoked/wrong`
after a 401, `no wifi` when the link is down, `no token: set it` when the link
is up and no token has been set, `no server: retry 30s` while there is a wait
before the next call — a doubling backoff, or the `Retry-After` a deploy's 503
named. Line two's *no* says something is wrong; line four says what, and
*Online* is `no` through a `Retry-After` hold as it is through a backoff.

A **click on the PRG button** (GPIO 0) toggles the panel: off when it is on, on
when it is off. `display.timeout` is the seconds of no button press after which
it goes dark, and its default is **`0`**, which never turns it off;
`set display.timeout 300` is the five minutes this firmware did before and takes
effect without a reboot. Holding PRG through a reset is the chip's bootloader
and has nothing to do with either.

## What you should see on meshmaster.dk

A provisioned board is an ordinary exit node with an extra word. Within one
poll interval of the first heartbeat, `/exit-nodes` shows the row **online**,
and where another exit node's transport column says `serial` or `tcp` this one
says **onboard · Heltec V3** (or **Heltec V4**) — the transport and the `board`
its heartbeat reported ([UI §6](https://github.com/twinoak/meshmaster.dk/blob/master/spec/UI.md#6-exit-nodes)).

Its page, `/exit-nodes/{id}`, is filled in from the heartbeat and from nothing
else:

- **Agent version** the firmware's own (`meshmaster-agent 0.1.0+abc1234`, or
  `0.0.0-dev` from a local build), **Firmware** the MeshCore release this image
  is built from (`companion-v1.17.1`) and **Board** `heltec_v3`. On a board
  the companion firmware *is* the firmware, so the two lines together are the
  string `status` prints.
- **Companion key** the board's own identity, the 64 hex characters `status`
  shows. It is made once from radio noise and kept in SPIFFS, so it survives a
  reflash and an `erase confirm`; if it ever changes the server flags the exit
  node and suspends admin routing through it until the owner confirms the new
  key, which is
  [EXIT-NODES §10](https://github.com/twinoak/meshmaster.dk/blob/master/spec/EXIT-NODES.md#10-exit-node-lifecycle-and-health)'s
  *Replace* and is the reason `erase confirm` keeps the identity.
- **Radio** `869.618 MHz, BW 62.5, SF8, CR8` — the `radio.*` settings, read
  from the board and not from the registration form. A radio that differs from
  the `network.radio` setting raises the page's warning, which is the point of
  reporting it.
- **Job types** all thirteen of API §3 — the four reads, `discover`, `acl.set`
  and `config.read`, and the six that change a repeater: `config.apply`, `cli`,
  `advert.request`, `clock.sync`, `password.set`, `reboot` — and **queue
  depth** 0. Routing requires the type advertised, so the server sends these
  and nothing else; a job of any other type is answered `unsupported` at no
  transaction. Every admin job carries the repeater's admin password in the
  claim, and a `password.set` the new one beside it; each lives in one fixed
  buffer for the length of the job and is zeroed when the job ends, whatever it
  came to (`lib/agent-core/include/agent/Credentials.h`). Every admin session on a repeater this
  exit node holds a grant on ends with `setperm <own key> <granted level>`, so
  the entry the association made is read-only again when the job is over — a
  `password.set` sends it after its password line and then drops the session,
  and a `reboot` sends none, because nothing answers after it. A write is sent
  **once**: an apply's commands in order with strictly increasing sender
  timestamps, stopped at the first refusal or silence, and the region loader's
  terminator sent even when the sequence dies (`lib/agent-core/include/agent/ConfigApply.h`).
- **Error counters** `overheard` (frames of the mesh's own chat this board
  discarded unread — a large number is a busy public channel, not a fault),
  `wifiReconnects`, `httpFailures`, `observationsDropped` (adverts the RAM
  ring had to drop because the server or WiFi was away), `resultsDropped` (the
  same for job results) and `reboots` (restarts this firmware asked for
  itself, because the server had been unreachable against a live WiFi link for
  an hour and a half). `errors` on the console prints all six with the
  counters that never leave the board beside them, and `reboots` says why each
  restart happened.

And **Unknown nodes** fills up. Every advert the radio hears becomes one
observation — the public key, the path and hop count it arrived by, the SNR and
RSSI, and the name, position and role the advert carried — posted in batches of
twenty, or every sixty seconds if fewer have come in. A key the register
already knows teaches that (exit node, repeater) pair a path; a key it does not
appears under `/unknown-nodes` with the hops it was heard at, ready to be
adopted. At a window this starts within a few minutes.

And the jobs get answered. A repeater associated with this exit node, on its
schedule or on a press of **Read now**, produces a claim of up to two jobs; each
is one login and one request or two, paced by the budget the heartbeat carried,
and its reading is on the repeater's page within a poll interval. The job's row
shows the outcome, the transaction count, the path used and the round trip, and
its `log` is the same text the console printed.

What the page will *not* show is anything the mesh said: a message is counted
under `overheard` and dropped unread. The console's **`tx`** is the other half
of it — it counts one packet per transaction and nothing else, so `jobs` and
`status` have to agree, and a `tx` larger than the transactions `jobs` counted
is the bug this rail exists to catch.

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
| 06 | [a-node-that-stays-up](tasks/phase-1/06-a-node-that-stays-up.md) | watchdog, reconnect, buffering, [the field runbook](docs/field.md), the Heltec V4 |

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
  in RAM for the job and is overwritten after (a test reads the bytes back, and
  another greps the modules it passes through for the NVS and file APIs); the
  serial log redacts as the .NET agent does, and `log.frames on` prints a login
  that carries a password, and a `password` line and its echo, with their
  bodies as `*` (`lib/agent-core/include/agent/Credentials.h`). `dump` prints the log ring, which
  holds the bytes the port already printed and no others. Flash encryption is
  documented as the owner's choice, not assumed.
- **Nothing the mesh says is logged as text.** Overheard channel and contact
  messages are counted and dropped, never printed; an advert's name is read on
  demand with `mesh` and never logged. `log.frames on` prints the bytes of a
  job's own frames — the login, the request, the reply — and of nothing else.
  `MESH_PACKET_LOGGING` and `MESH_DEBUG` stay off, whatever upstream's examples
  do.
- **One send per command.** A `set` sent twice is a change made twice: single
  send, strictly increasing sender timestamps, the region loader's terminator.
- **It stays up, and says why when it did not.** A task watchdog on the loop
  that is also the network, a deliberate restart when the server has been
  unreachable against a live link for an hour and a half, and the last five
  reasons in RTC memory for `reboots` to print (`src/Health.h`). What the
  server was away for is buffered in RAM — bounded, oldest dropped, counted,
  and posted late with the bytes it was written with — and a job whose lease
  ran out while it waited is not put on the air at all. Every bound has a
  native test (`test/test_buffers`), and so has every answer the server can
  give and the wait it earns (`test/test_server_policy`): a 503 that carries
  `Retry-After` is believed to the second, and once WiFi is up and the clock
  came off the network the backoff stops doubling at **two minutes**, because
  then the only thing not answering is the server.
- **Path hashes are two bytes or more, never one.** Wherever this firmware
  chooses a path-hash width - every flood it originates
  (`AgentMesh::kFloodPathHashSize`), a trace it builds, anything a later task
  adds - the width is at least two bytes. One-byte hashes let two repeaters
  that share a first byte both repeat a direct packet, and the first bench
  showed a five-hop path of them. A path learned from a reply keeps the width
  it was learned at. Thomas, 3 October 2026, for all MeshCore work
  ([D-33](https://github.com/twinoak/meshmaster.dk/blob/master/spec/DECISIONS.md#3-new-decisions)).
- **The contract is the .NET agent's.** Every envelope is checked against
  `MeshMaster.Contracts`' fixtures, copied byte for byte into
  [`tests/fixtures/contracts/`](tests/fixtures/contracts/README.md), and every
  reply a repeater sends is read from the transcribed frames of
  [`tests/fixtures/companion/`](tests/fixtures/companion/README.md). A
  difference is a bug in the firmware, not a second dialect.

## Licence

MIT — [`LICENSE`](LICENSE), the one GitHub wrote when the repository was created
(holder: *twinoak*; twinoak/meshmaster.dk's says *Thomas Eg*, and both are the
same person's). MeshCore is MIT too and is a dependency, not a fork: nothing of
its tree is vendored here.
