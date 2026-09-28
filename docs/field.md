# In the field: a board at a window, and what to do when it goes quiet

Written to be the only page open on a laptop beside a windowsill. It takes a
Heltec V3 or V4 from a cable on a bench to an exit node that has been up for a
week, and it says what every line the board can show you means.

The desk-bound half of this — every command and every key — is
[`console.md`](console.md); flashing and the images are
[`README.md`](../README.md); the server's side of an exit node is
meshmaster.dk's own [`docs/ops/exit-nodes.md`](https://github.com/twinoak/meshmaster.dk/blob/master/docs/ops/exit-nodes.md).
This page is the order of operations and the troubleshooting.

---

## 1. What you need with you

- The board — a **Heltec V3** or a **Heltec V4**, with its antenna screwed on.
  *Never power a LoRa board without an antenna.*
- A USB-C cable that carries **data**. A charge-only cable is the single most
  common wasted half-hour.
- A laptop with a terminal at 115 200 baud (`pio device monitor -b 115200`,
  PuTTY, `screen /dev/ttyACM0 115200`).
- The WiFi name and passphrase of the house the board will live in.
- A login to meshmaster.dk that may register an exit node, with a second factor
  inside the re-authentication window — *Register* is a gated action.

A board is provisioned **over its USB port and nowhere else**: there is no
serial console over the network and no captive portal
([EXIT-NODES §5](https://github.com/twinoak/meshmaster.dk/blob/master/spec/EXIT-NODES.md#5-the-agent),
"What it cannot do"). Whatever is typed here is typed once, at the bench.

## 2. The bench: from a dark board to a heartbeat

1. **Flash it.** `pio run -e heltec_v3 -t upload`, or the merged image from a
   release with `esptool` — [`README.md`](../README.md) has both lines. Use
   `-e heltec_v4` for a V4.
2. **Open the port.** The banner, then a `>` prompt. If nothing appears, it is
   the cable.
3. **`status`.** An identity of 64 hex characters, the radio parameters,
   `wifi off`, `no time`, `server not configured`. Write the identity down:
   that is the board's **companion public key** and it is what the exit node is
   known by. It is made once from radio noise, lives in SPIFFS, and survives a
   reflash and an `erase confirm`.
4. **`set wifi.ssid …` and `set wifi.pass …`.** The passphrase echoes as
   asterisks and is printed by nothing afterwards. Within a few seconds
   `wifi: connected` and an address, then `clock: … from sntp`.
5. **Register the exit node** on meshmaster.dk (`/exit-nodes/new`): owner,
   label, location, hardware, transport **onboard**. The token is shown once.
6. **`set meshmaster.url https://meshmaster.dk`** and
   **`set meshmaster.token mma_…`**. The log says `server: GET …/agent/v1/config`
   and then `server: config ok: agent <id> "<label>" …`. Within one poll
   interval the exit node's row is **online** and its transport column says
   *onboard · Heltec V3*.
7. **`server`** and **`errors`**: the heartbeat, when the next one is due, and
   every counter at zero.

Nothing is stored anywhere else, and no reboot is part of provisioning: every
`set` takes effect at once.

## 3. The window

- **One hop is the goal, two is acceptable, three is diagnostic-only**
  ([EXIT-NODES §12](https://github.com/twinoak/meshmaster.dk/blob/master/spec/EXIT-NODES.md#12-placement-guidance-for-the-uis-help-text)).
  Put the board where its antenna can see the sky in the direction of the
  repeaters, not behind a radiator or a metal blind.
- **Vertical antenna**, a hand's width from the window glass and from anything
  metal. A board flat on a sill with the antenna horizontal loses most of its
  range to polarisation alone.
- **USB power from a wall charger**, not a laptop that sleeps. The board draws
  little and has no battery management worth relying on for this.
- **Out of the sun.** An ESP32-S3 behind south-facing glass in July gets hot
  enough to matter.
- Leave it ten minutes and read `mesh`: the adverts heard, and the last five
  with their names, roles, hops and SNR. If the repeater you care about is not
  in that list at zero hops, move the board before you do anything else. The
  keys it does hear turn up under **Unknown nodes** on the server within a
  minute or two.

## 4. The OLED's four lines

The display is on for five minutes after boot and after a press of the user
button (PRG, GPIO 0), then off. A board with a dark display is a board that is
fine; press the button.

```
wifi 192.168.1.20          <- the link: an address, or no ssid / connecting / retry 20s
12:03:14 UTC (sntp)        <- the clock: this, or "no time"
hb 12:03 . 17 adverts      <- the exit node: the last heartbeat and the adverts heard
id 7f3a9c02  agent         <- the identity's first eight hex characters
```

**Line three is the one to read when something is wrong**, and it names which
of the two is missing:

| Line three | What it means | What to do |
|---|---|---|
| `hb HH:MM · N adverts` | all well: the last heartbeat and the adverts heard | nothing |
| `no wifi: no server` | the WiFi link is down, so the server cannot be reached either | line one says how far the retry has got; §6 |
| `no server: retry 45s` | the link is up and meshmaster.dk is not answering | §6; the countdown is the next attempt |
| `no server set` | `meshmaster.url` or `meshmaster.token` is unset | §2, step 6 |
| `config ok HH:MM:SS` | the first call worked and no heartbeat has been answered yet | wait one poll interval |
| `http 401` / `token revoked/wrong` | the token was revoked, or is wrong | §6; nothing is sent until it is set again |
| `tls/net failed` | the certificate chain or the network | §6 |
| `no identity` | the radio did not come up | the console still works; the board hears nothing |

## 5. What the exit node's page says, and what to do about it

`online` is a heartbeat within three poll intervals, `quiet` is one within
24 hours, `offline` is neither
([EXIT-NODES §10](https://github.com/twinoak/meshmaster.dk/blob/master/spec/EXIT-NODES.md#10-exit-node-lifecycle-and-health)).

**The page says *quiet* or *offline*.** The board cannot be asked over the
network — there is no ssh here. Walk to it with a cable, and read four things
in this order:

```console
> reboots
this boot Task watchdog reset, up 0d 00:04:12; watchdog 90 s on the loop task
counted   3 restart(s) since the last power cut, 2 of them this firmware's own
last five newest first: when, uptime at the time, why
  09:14:33   after 1d 07:22:41  Task watchdog reset
  02:51:09   after 0d 19:04:55  12 server calls in a row got no answer, with wifi up
  ...
> errors
> server
> dump
```

- **`reboots`** first, because it answers "has it been restarting?" in one
  line. Three kinds of entry:
  - *asked for on the console* — somebody typed `reboot`.
  - *N server calls in a row got no answer, with wifi up* — the board decided
    its own TLS stack was wedged and restarted to clear it. One of these after
    a long server outage is normal; a string of them means the server (or the
    route to it) has been down, not the board.
  - anything else — the board's own name for the reset: *Task watchdog reset*,
    *Brownout reset*, *Panic reset*. A watchdog reset is the loop stuck for
    90 seconds, which on this firmware means a network call that never came
    back. A brownout is the power supply: change the charger or the cable.
  - *log empty: the RTC memory did not survive* — a power cut. The log lives in
    RTC memory, which a reflash and a watchdog reset keep and a loss of power
    does not.
- **`errors`** second: the six counters the heartbeat carries, and the ones
  beside them. `httpFailures` climbing with `observationsDropped` and
  `resultsDropped` at zero is a short outage the buffers absorbed;
  `resultsDropped` above zero means a job's answer was lost for good.
- **`server`** third: the last heartbeat, the next, the backoff in force.
- **`dump`** last, which prints the final four kilobytes of the log — about
  fifty lines, the same bytes the port printed when nobody was watching. That
  is where the reason usually is.

**The page says *online* but the jobs come back `unreachable`.** That is the
radio, not the network: `mesh` to see whether the repeater is heard at all, and
a `discover` from its page to find a path. Moving the board a metre is often
the whole fix.

**The page says *companion changed*.** The board's identity changed, which
happens only if SPIFFS was wiped or the board was replaced. Every ACL grant
named the old key: confirm the new key on the exit node's page and re-run each
association's grant.

## 6. When the board cannot reach the server

Both retries of this firmware back off the same way — a doubling sequence with
**jitter**, so that a houseful of boards that lost the same router do not all
come back at the same second:

| | first | then | ceiling |
|---|---|---|---|
| WiFi | 5 s | ×2 | 5 min |
| the server | 30 s | ×2 | 10 min |

and each wait is that step plus or minus a quarter. The log says both numbers:
`server: next attempt in 88 s (the 120 s step, jittered)`.

Through all of it **the mesh keeps running**: the radio is listening, adverts
go into the ring, and the ring holds 64 of them — when it is full the oldest is
dropped and counted, and the count is the heartbeat's `observationsDropped`.
Job results the server would not take are kept the same way, oldest dropped and
counted (`resultsDropped`), and posted late with the bytes and the client id
they were written with, which is why a late result is still a good result: the
server takes it and de-duplicates.

**What the board will do by itself.** After twelve consecutive unanswered
calls *with the WiFi link up* — about an hour and a half at the backoff above —
it decides the TLS stack is wedged and restarts, having written the reason into
RTC memory first. That is the whole of its self-healing, and `reboots` is where
it confesses to it. It will not do that while the link is down: a router that
is off is not something a reboot fixes.

**What it will not do.** It has no journal: a restart, a reflash or a power cut
loses whatever was in the buffers. Flash a board when the mesh is quiet, or
accept the gap.

**A 401.** `server: the token was refused (http 401)` halts everything but the
console, because retrying fixes neither a revoked token nor a wrong one. Set
`meshmaster.token` again and the loop starts without a reboot.

## 7. Pulling the log ring

```console
> dump
log       3980 of 4096 bytes, 412 line(s) written, 369 overwritten
---- the log, oldest first ----
[09:12:44] I wifi: connected, 192.168.1.20, rssi -67 dBm
[09:12:46] I server: config ok: agent 17 "Window at Grenaa", ...
...
---- end ----
```

Copy it out of the terminal. Four kilobytes is about fifty lines — enough for a
boot, a join, a clock, a config call and a job or two, which is the window
around whatever went wrong. It holds exactly what the port printed: no secret
can be in it that was not on the terminal already, because the redaction
happens where a line is written and not here. If you want more than that, set
`log.level debug` and leave a terminal attached.

## 8. The week at the window

The exit criterion for this firmware is a board that has answered every job
type at least once against a real repeater and has been up for a week with no
hand on it. On the seventh day, with a cable:

- `status` — `up 7d hh:mm:ss`, and a `heap free` and `min ever` that have not
  moved since the first day. Every buffer this firmware has is `.bss`; a heap
  figure that walks downward is a leak and is worth a report.
- `reboots` — ideally *none remembered* and `0 restart(s) since the last power
  cut`.
- `errors` — `overheard` will be large at a busy window and is not a fault;
  `observationsDropped` and `resultsDropped` should be 0 or should match an
  outage you know about.
- `jobs` — the last ten, all `ok`, and a transaction count that matches what
  the server's job rows say.
- `dump` — the last four kilobytes, for the record.
- `mesh` — the repeaters still heard, at the hop counts they were heard at on
  day one.

## 9. Updating a board that is already in the field

By flashing, and by nothing else
([`docs/ops/agent.md` §8](https://github.com/twinoak/meshmaster.dk/blob/master/docs/ops/agent.md#8-updating-the-agent--and-the-board-that-is-one)):
there is no package and no service to restart. Somebody stands at the board
with a USB cable.

- **The token and the identity survive.** Both live outside the application
  image — the token in NVS, the identity in SPIFFS — so the exit node comes
  back as itself, with its history, its budget and its associations. A board
  that is wiped is a revoke and a fresh registration.
- **What is in RAM is lost**: unposted results and observations, and the reboot
  log. Do it when the mesh is quiet.
- **A V4 is not a V3.** Flash `heltec_v4` images to a V4 and `heltec_v3` to a
  V3; the pin maps and the transmit power differ. A V4 out of the box transmits
  at 10 dBm because its external power amplifier adds the rest — `radio.tx 22`
  on a V4 is not 22 dBm at the socket, it is far more, and it is neither legal
  nor good for the amplifier. Leave `radio.tx` unset unless you know why you
  are changing it.
