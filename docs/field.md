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
   `-e heltec_v4` for a V4. Either keeps a provisioned board's settings and its
   identity, with the one exception README's *Flashing it* spells out: the
   **first** merged flash of a release carrying this firmware's partition table
   over a board provisioned under `v0.1.1` or earlier forgets the settings — not
   the identity — and is steps 4 to 7 again, with a new token.
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

**Nothing to type about the flood scope** (task p1.8-32): `flood.scope` is `dk`
out of the box, which is the Danish standard's region, and a board outside that
standard is the only one that needs `set flood.scope <region>` or
`set flood.scope *`. `get flood.scope` prints the region and the transport key
it derives, and `status`'s `flood` line counts which shape of flood was
answered — [`console.md`](console.md) has both.

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

The display **stays on**: `display.timeout` is the seconds of no button press
after which it goes dark, and its default is `0`, which never turns it off. A
**click on the user button** (PRG, GPIO 0) toggles it - off when it is on, on
when it is off. `set display.timeout 300` gives back the five minutes this
firmware did until the display task, without a rebuild and without a reboot.

```
ID: 46F6     12:03:14      <- who: the key's first four, and the clock (or "no time")
WiFi: yes Online: yes      <- the link, and whether the last call to the server was answered
Heartbeat: 12:03:14        <- when meshmaster.dk last heard from this board
Idle, last 12:34:56        <- what it is doing, or why it is doing nothing
```

Line one is `no identity` alone if the radio did not come up - the console
still works and the board hears nothing. Line two is the glance: *WiFi* is the
link, *Online* is the last call having been answered **and** nothing waiting
before the next - so a deploy's 503 that says *come back in 45 s* makes it
`no`, because that is a wait and not an answer. Line three is
`Heartbeat: none` until the first one.

**Line four is the one to read when something is wrong.** Line two's *no* says
something is wrong; line four says what:

| Line four | What it means | What to do |
|---|---|---|
| `Job 1234 acl.set` | a job is on the air, this id and this type | nothing; it is working |
| `Idle, last 12:34:56` | nothing is running; the newest job ended then | nothing |
| `Idle, no jobs yet` | nothing has run since this board started | nothing, unless line two says *no* |
| `no wifi` | the WiFi link is down, so the server cannot be reached either | `status` says how far the retry has got; §6 |
| `no token: set it` | the link is up and no agent token has been set | `set meshmaster.token <token>` from the exit node's page; §2 |
| `no server: retry 45s` | the link is up and there is a wait before the next call | §6; the countdown is the next attempt, whether it is a backoff or a 503's `Retry-After` |
| `token revoked/wrong` | the token was revoked, or is wrong | §6; nothing is sent until it is set again |

A board that is up but unprovisioned shows `WiFi: no Online: no` and `no wifi`:
there is no SSID to join. Give it one and the line becomes `no token: set it`
until the token is set, which is the state the first merged flash leaves
behind. Through a deploy of meshmaster.dk the line reads
`no server: retry Ns` and *Online* is `no` - the server answered 503 and said
when to come back, which is a wait and not an answer. The console's `status` and `server` have everything
the lines leave out - the IP address, the certificate root, the config call's
own answer, the poll interval.

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

**The page's Firmware tab says *handed over*, and then nothing for a while.**
That is the update running, and it is normal: since the release of task p1.15-61
a press of *Update* goes out in the reply to this board's next heartbeat — within
two minutes — and the board then does the steps itself, restarting once or twice.
The tab shows the step the board's own heartbeat last carried, which is all the
server hears until the end:

| the step | what the board is doing |
|---|---|
| `check` | deciding whether it can take this release at all |
| `adding-slot` | writing the one sector of its partition table; the restart follows |
| `fetching` | downloading the release into the slot that is not running |
| `switching` | the boot choice is set and the restart follows |
| `proving` | the new firmware is running and has ten minutes to prove itself |

and then one of four endings, which is what the tab finally says:

| the ending | what it means, and what to do |
|---|---|
| *updated to X* (`committed`) | the new firmware runs and has proved itself. Nothing to do |
| *rolled back* | it ran, did not prove itself, and the old one runs again. **Nothing to do at the window**: the exit node is itself again. The journal says what was missing — no wifi, no server, or *restarted during the trial* — so fix that and press *Update* again |
| *refused* | nothing was switched, and the word says why: `inProgress` (an update was already in hand), `table` (this board's table is one the firmware will not write — `update` at the console says so), `current` (the release asked for is the one it runs), `fetch` (the release could not be downloaded) or `check` with a number 1 to 5 (the signature, the manifest, the image for this board, the file's hash, the descriptor) |
| *stuck* | the board cannot say what happened. Walk to it: `update` on the console prints the ask, the step it reached and the journal |

`table` is the refusal of **`other` alone**: a table this firmware does not
recognise is one it will not write over, and that board needs the merged image
over USB (§9). A board whose `update` says **`one`** is *not* refused — since the
release of p1.15-61 it adds the second slot itself at the first ask, which is
what the `adding-slot` step above is, and `update slot` at the console does the
same thing by hand.

A *stuck* that came out of `adding-slot` says in its journal which of two things
happened: *the table is the one the board had*, which is a board that is exactly
as it was and can be asked again, or *the sector holds no partition table*, which
is a board to walk to now — §9 says what it is doing about it and what not to do.

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
| the server, link proven | 30 s | ×2 | **2 min** |
| the server, link not proven | 30 s | ×2 | 10 min |

and each wait is that step plus or minus a quarter. The log says both numbers:
`server: next attempt in 88 s (the 120 s step, jittered)`.

**The link proven** is WiFi connected *and* a time that came off the network
(`status` says `from sntp`, or `from server`). Then the board's own end is
working and the only thing left not answering is meshmaster.dk, which deserves
a call within two minutes rather than within ten: a release of the server costs
a minute or two and the wait used to outlast it by a quarter of an hour, so the
exit node read *quiet* long after the server was back (bench, 3 October 2026).
A board whose link is **not** proven — a WiFi that associates and goes nowhere, a
captive portal, a DNS that answers nothing — keeps the ten minutes it always
had, because then the suspect is at this end.

**And a server that says when to come back is believed.** A 503 (or a 502 or a
504) carrying `Retry-After` holds for exactly that, to the second, and nothing
doubles — the log says `server: http 503; the wait is Retry-After's 90 s`. That
is the 429's rule, which rate limiting always had, extended to the three
statuses a deploy answers. A 5xx with no header backs off as above.

Through all of it **the mesh keeps running**: the radio is listening, adverts
go into the ring, and the ring holds 64 of them — when it is full the oldest is
dropped and counted, and the count is the heartbeat's `observationsDropped`.
Job results the server would not take are kept the same way, oldest dropped and
counted (`resultsDropped`), and posted late with the bytes and the client id
they were written with, which is why a late result is still a good result: the
server takes it and de-duplicates.

**The connection it keeps, and a router that drops idle sessions.** Since task
p1.8-30 the uplink keeps one TLS connection to meshmaster.dk between calls
rather than building one per call ([`console.md`](console.md), the `uplink`
line of `status`). The server closes an idle connection after about two
minutes (IIS's default, measured at 128 s on 31 October 2026), which is
longer than the idle heartbeat's 120 s, so most calls go over the kept
connection and the handshake - the expensive part, in heap and in seconds -
is paid once per connection rather than once per call. A home router that
drops a TCP session that has been quiet for less than that is the one thing
in the way: the board cannot see a silent drop, so its next call goes over the
dead connection, waits out the 15 s reply timeout, and is then made again on
a new connection **inside the same call** (`uplink: the kept connection was
dead under the request; reopened inside the call`). The call is answered, the
loop counts no failure, and the only sign is that call's extra fifteen seconds
and the `reopened in-call` count on the `uplink` line. A router that does that
at every idle interval turns the kept connection back into one per call,
fifteen seconds slower; if the count climbs with the calls, raise the router's
TCP idle timeout above two minutes or accept it. A connection the server or the
router closes *properly* costs nothing at all: the board sees the close before
the request and handshakes anew.

**What the board will do by itself.** After twelve consecutive unanswered
calls *with the WiFi link up*, **and** an hour and a half of them, it decides
the TLS stack is wedged and restarts, having written the reason into RTC memory
first. Both conditions, because at the two-minute ceiling above twelve failures
can be twenty minutes end to end and twenty minutes of a server being away is
an ordinary deploy, not a wedged stack. That is the whole of its self-healing,
and `reboots` is where it confesses to it (`12 server calls in a row got no
answer in 94 min, wifi up`). It will not do that while the link is down: a
router that is off is not something a reboot fixes. `errors` prints how far
along both conditions are.

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
  figure that walks downward is a leak and is worth a report. The `uplink`
  line's handshakes should be a small fraction of its calls, and `reopened
  in-call` near zero - a count that climbs with the calls is the router
  dropping idle sessions (§6).
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
- `flood` on `status` — the three counts of flooded transactions (task
  p1.8-32). On a settled board most of them are `scoped`, because every
  repeater MeshMaster knows a path to is reached **directly** and a flood only
  happens when it does not. `unscoped N` is the one to read: it is how often the
  second, scope-less try was the one that answered, which means a repeater out
  there has no region map yet. `none` climbing is a repeater nobody can reach in
  either shape, and `discover` on its page is the next step.

## 9. Updating a board that is already in the field

By flashing, and by nothing else
([`docs/ops/agent.md` §8](https://github.com/twinoak/meshmaster.dk/blob/master/docs/ops/agent.md#8-updating-the-agent--and-the-board-that-is-one)):
there is no package and no service to restart. Somebody stands at the board
with a USB cable.

- **The token and the identity survive.** Both live outside the application
  image — the token in NVS, the identity in SPIFFS — so the exit node comes
  back as itself, with its history, its budget and its associations. Either
  image keeps them: this firmware's partition table puts NVS above the
  application, where a merged image cannot reach it
  ([`boards/README.md`](../boards/README.md)). A board that is wiped is a
  revoke and a fresh registration.
- **One flash, once per board, forgets the token and the WiFi** and nothing
  else: the **first** merged image of a release carrying this firmware's
  partition table, flashed over a board provisioned under `v0.1.1` or earlier
  (whose table kept NVS in the span a merged image overwrites —
  [`README.md`](../README.md), *Flashing it*). The identity stays, `status`
  shows the same key, and the exit node is still itself with its associations;
  it needs its WiFi again and a **new** token from *Generate a new token* on
  its page — not a fresh registration. Upgrading such a board with the
  application image at `0x10000` keeps its old table and its settings, and
  postpones that to its next merged flash. After that one flash, no image
  reaches the settings again.
- **What is in RAM is lost**: unposted results and observations, and the reboot
  log. Do it when the mesh is quiet.
- **A V4 is not a V3.** Flash `heltec_v4` images to a V4 and `heltec_v3` to a
  V3; the pin maps and the transmit power differ. A V4 out of the box transmits
  at 10 dBm because its external power amplifier adds the rest — `radio.tx 22`
  on a V4 is not 22 dBm at the socket, it is far more, and it is neither legal
  nor good for the amplifier. Leave `radio.tx` unset unless you know why you
  are changing it.

### The second application slot, and the one command that writes flash

Since the release of task p1.15-57 the partition table has a second
application slot — `app1`, the place an update will be fetched into — and a
board flashed with the **merged** image has it from the first boot. A board
**upgraded with the plain `.bin` keeps the table it already has**, which is
right: the web flasher erases the whole board before it writes a merged image,
and that costs the key, the token and the WiFi. Such a board adds the slot
itself, once, when you ask it to.

```
> update
table     one
app0      0x010000, 0x330000 bytes
app1      none: this is the table boards were flashed with before the second slot existed
          type `update slot` to add it, then `reboot`; the settings and the identity are untouched
> update slot
added: app1 at 0x350000, 0x320000 bytes. Type `reboot`; the table is read at the next boot
> reboot
```

- `update` says one of three words and nothing else happens: **`two`** — the
  slot is there, and the two offsets are printed; **`one`** — the table is the
  one every merged image wrote before this release, and the slot can be added;
  **`other`** — the table is not one this firmware recognises, and it will not
  be written over. `other` is not a fault to fix at the window: flash the
  release's merged image over USB with `esptool` (never the web flasher) and
  the board has two slots at once.
- `update slot` is **the only command on this console that erases or writes
  flash**. It writes one sector of 4 kB at `0x8000`, reads it back and
  compares; it touches nothing else, and it restarts nothing. Settings,
  identity, token and WiFi are all outside it. **Do not pull the power while
  it runs.** A board cut off in that second needs the merged image over USB
  before it boots again — the only moment in this work that can leave a board
  needing a cable.
- On a board that is already `two`, `update slot` says so and writes nothing.

**A write that fails is tried again.** The new table is written and read back up
to three times; when it will not hold, the table the board came with is put back
the same way and as many times, because a board whose table is the one it had is
a board that boots. So `update slot` has three endings, and it names the one that
happened — with the tries, when it took more than one:

| what it says | what has happened | what to do |
|---|---|---|
| *added* (`added`) | the sector holds the new table, and the board has `app1` from the next boot | `reboot`, then `update` to see `two` |
| *the board is as it was* (`as-it-was`) | the new table would not hold, and the sector holds the one the board came with, byte for byte | nothing at the window: the board boots and runs as before. Try again later; if it keeps failing, flash the release's merged image over USB with `esptool` |
| *the sector holds NO table* (`no-table`) | neither table would hold. **This board does not boot if it restarts now** | **do not cut the power.** The firmware keeps putting the old table back, once a minute, for as long as it runs, and says so in the first line of `update` and of `status` until one holds. Leave it on the power and watch those lines. If you must move it, flash the merged image over USB first |

The third is rare and loud: the firmware has already asked six times by the time
it says it. While it stands, `update` and `status` both begin with

```
!!        DO NOT CUT THE POWER: the partition table's sector holds no table and the old one is being put back once a minute (2 more tries so far). A board that reboots first needs the merged image over USB
```

and that line goes away by itself the moment a write holds — the board is then as
it was, without the second slot, and can be asked again.

The server's own ask does the same writing with nobody at the window, so an ask
that ends *stuck* at `adding-slot` carries whichever of the last two it was in
its journal (§5).

### Fetching a release into the second slot

Since the release of task p1.15-59 a board with two slots can fetch a release
off meshmaster.dk into the slot it is not running and check it there. **Nothing
is switched by it**: the board goes on running the firmware it was running, and
the fetched image sits in the other slot until a later release can switch to it.
A person starts every fetch; nothing fetches a release because one exists.

```
> update fetch 12
fetch     /agent/v1/firmware/12: the manifest and its signature first, then the image in pieces; the steps follow
[12:03:11] I fetch: manifest.json: 1612 bytes
[12:03:11] I fetch: manifest.json.sig: 108 bytes
[12:03:12] I fetch: checks 1, 2 and 3 passed: twinoak-exitnode-firmware 0.8.0+1a2b3c4: the image exit-node.heltec_v3.868, 2 file(s)
[12:03:12] I fetch: meshmaster-agent-heltec_v3-v0.8.0.bin: 1446640 bytes wanted, by its hash
[12:03:13] I fetch: range 0-65535: http 206, 65536 bytes (65536 of 1446640 held)
...
[12:03:52] I fetch: checks 4 and 5 passed: ... the one descriptor at 0x1189C agrees with it
[12:03:52] W update: fetched 0.8.0+1a2b3c4, 1446640 bytes, sha 4f2a91c0b7d2e5a8...
> update
other     holds 0.8.0+1a2b3c4, 1446640 bytes, sha 4f2a91c0b7d2e5a8..., fetched from /agent/v1/firmware/12
```

- **The release id** is the number meshmaster.dk shows for the release. Nothing
  but digits is accepted, because the id goes into the path the board asks for.
- **It does not block the board.** The file comes down in ranges of 64 kB, one a
  pass of the main loop, so the radio keeps hearing, the heartbeat keeps going
  out and this console keeps answering while it runs. It is seconds to a minute
  on a good WiFi link.
- **The ending is one of four words.** `fetched` — the slot holds a judged
  image, and `update` says which. `check` with a number from 1 to 5 — the
  release was had and refused: 1 the signature, 2 the manifest, 3 no image for
  this board, 4 the size or the hash of what was written, 5 what the image says
  inside itself. `fetch` — the release could not be had: no link, a 404, a
  redirect, or three failures. `slot` — this board has no second application
  slot; `update slot` adds one.
- **Anything but `fetched` leaves the slot unbootable**, by erasing its first
  sector. There is never half an image waiting to be switched to.
- **A restart does not continue a fetch**; it begins again. The record of a
  *judged* image does survive a restart, and `update` still shows it.
- **Nothing here erases or writes anything the board needs.** The settings, the
  identity, the token, the WiFi and the running application are all outside the
  slot being written.

### Switching to it, and the ten minutes that decide

Since the release of task p1.15-60 a judged image can be switched to, and the
firmware that comes up then has to **prove itself** before it is kept. A
firmware that boots is not yet a firmware that works: what an exit node is for
is reaching meshmaster.dk and the mesh, and an image that cannot do either would
leave a board nobody can ever tell to update again. So it runs on trial.

```
> update apply
apply     switching; the board restarts now
[12:06:02] I update: switching: 0.7.2+4a4e791 to 0.8.0+1a2b3c4, 1446640 bytes, in app1
... the board restarts, and the new firmware comes up ...
[12:06:31] I update: proving: on trial for 10 min; a reset ends it and the bootloader undoes it
> status
firmware  meshmaster-agent 0.8.0+1a2b3c4 on MeshCore companion-v1.18.0 (81dbae59) - on trial
> update
trial     on trial, 9 min 2 s left: mesh yes, wifi and clock yes, heartbeat not yet
          0.8.0+1a2b3c4, switched to from 0.7.2+4a4e791 at 2026-11-20T12:06:02Z; the bootloader undoes it if this boot does not end
[12:06:48] I update: committing: mesh, wifi and the clock, and a heartbeat answered 17 s into the trial
> update
last      committed: 0.8.0+1a2b3c4, from 0.7.2+4a4e791, switched at 2026-11-20T12:06:02Z
```

- **The three conditions**, all of them in that one boot: the mesh started, the
  WiFi joined **and** the clock was set off the network, and a heartbeat was
  answered `2xx`. Then, and at no moment before, the image is marked valid.
  `update` shows each of them and the minutes left while the trial runs, and
  `status` says *on trial* in its first line.
- **Ten minutes, and then the old firmware is back.** Nobody has to be there.
  Ten minutes without all three and the image marks itself invalid and the board
  restarts into the old one. A crash, the watchdog or a power cut during the
  trial has the same end, and nothing in the application does it: the bootloader
  undoes an image that is still pending at a reset. **On a board whose
  bootloader came from other firmware** that may not happen — the journal's
  `proving` line then says *this bootloader leaves it new, not pending* — and
  the firmware sets the boot choice back itself instead, on the same three
  conditions and the same ten minutes.
- **What to do with a board that went back.** Nothing, at the window: the old
  firmware is running and the exit node is itself again. `update` says what was
  missing — `rolled back: no wifi…`, `rolled back: no server…`, or
  *restarted during the trial* with the reset's own reason — and `reboots`
  says when.
  Fix what the sentence names (the access point, the token, the server) and
  `update fetch` and `update apply` again. The image that failed stays in the
  other slot and is **not** switched to again by itself.
- **`update apply` is refused, and nothing is switched**, when the other slot
  holds nothing judged, when the bytes in it no longer hash to what the record
  says (read back from the slot before anything is written), when an update is
  already in hand, or when this board has no second slot.
- **And `update fetch` is refused while an update is in hand**, in a sentence:
  the slot a fetch writes is the one holding the image the board would go back
  to, so nothing writes over it until `update` says *committed* or
  *rolled back*.
- **Nothing starts an update but a person** — `update apply` here, or, from the
  next release, one press on meshmaster.dk. A release existing starts nothing.
- **A flash over USB is still the last word, for a firmware the board has never
  run.** A board that has taken an update runs from the second slot, and the web
  flasher writes the plain `.bin` to the first one whatever the board boots from.
  So at its next boot the board finds a firmware there it has never run, switches
  to it once and puts *it* on trial. An image it already tried and rolled back is
  not taken again — and neither is the release it was switched *from*, which is
  what the next section is about.
- **A trial that nothing could send back says `stuck`, not `rolled back`.** The
  framework can refuse to mark a failed image invalid; the board then restarts,
  because a restart is what undoes an image that is still on trial, and the boot
  after it says what is running. Only if that boot comes up in the failed image
  *again* and the boot choice cannot be set back either is there nothing to be
  done, and `update` then says `stuck` with the version that is running. Nothing
  is called rolled back before the board is back.
- **On the OLED** nothing new appears: the panel shows the clock, the link and
  the open job as it always did, and the version it shows is simply the version
  that is running. The trial is on the console and in the log.

### Going back a release

Since the release of task p1.16-64 there is `update back`.

**Flashing the release before over USB does not bring it back** on a board that
has been updated from the server. After an update from A to B the board runs B
from the second slot and the first slot still holds A; the paragraph above
switches at boot only to a firmware the board has **never run**, and it has run
A — A is the version it was switched from. The bytes in the slot are the same
whether somebody flashed A a minute ago or it has lain there since the update,
so the firmware cannot tell the two apart and does not guess. The flasher
succeeds, and the board comes up on B.

What it does instead is **say so at the boot**, once, and wait to be asked:

```
[12:31:04] W update: the first slot holds 0.7.2+4a4e791, which this board has run, so it is not switched to: `update back` goes back to it
> update
other     holds nothing this firmware will boot
back      app0 holds 0.7.2+4a4e791
          `update back` switches to it and restarts; it then has ten minutes to prove itself
> update back
back      switching; the board restarts now
[12:31:40] I update: switching: back to 0.7.2+4a4e791, which lies in app0
... the board restarts, and 0.7.2 comes up on trial ...
[12:32:08] I update: proving: on trial for 10 min; a reset ends it and the bootloader undoes it
```

- **It is on trial like any other**, with the same three conditions and the same
  ten minutes, so a release that cannot reach the mesh, the WiFi or the server
  from this board is not what you are left with: a trial it fails ends in the
  image the command was typed in.
- **The `back` line of `update` is not the `other` line.** `other` is what a
  *fetch* judged, read from a record; `back` is what is **in** the slot, read out
  of the slot itself. A board that has taken an update has nothing judged at all
  and still has a firmware in the other slot.
- **It is refused in a sentence, and nothing at all is switched**, when an update
  meshmaster.dk asked for is in hand, when a fetch is running, when a firmware is
  on trial or an update has not been restarted into yet, when this board has one
  application slot, when the other slot holds nothing this firmware would boot,
  when it holds an image with no descriptor, and when it holds another product or
  another board's firmware.
- **An image with no descriptor is never switched to**, by this command or at the
  boot. Every release older than the second slot has none, so the only way back
  to one of those is the merged image over USB — and that erases the key, the
  token and the WiFi. The boot says which it is rather than passing it over in
  silence:

```
[12:31:04] W update: the first slot holds an image with no descriptor, so it is not switched to: a release older than the second slot carries none, and only a merged image over USB reaches one
```

- **The line at the boot is printed once a boot**, and nothing switches by itself
  that did not before: the one switch a boot makes is still the one above.

### Started from meshmaster.dk instead

Since the release of task p1.15-61 everything above can be done by one press on
the exit node's Firmware tab, and the board does the steps by itself. It is the
same machine: the same table, the same fetch, the same switch, the same ten
minutes.

- **The press reaches the board in the reply to its next heartbeat**, which is
  thirty seconds to two minutes away. There is no job for it — a job belongs to a
  repeater — and nothing is pushed to the board: it asks, as it always does.
- **The board adds the slot itself.** A board that still says `one` does not need
  `update slot` typed: the first ask writes the sector and restarts, and the ask
  goes on after that restart without being asked again. A power cut in that one
  moment leaves a board that needs a merged image over USB, and it is the only
  moment that can.
- **No job is claimed while it runs**, and a job that was already running is
  finished and its result posted first. The adverts the radio heard still go to
  the server throughout.
- **One result, at the end**, posted until the server takes it — a restart in the
  middle does not lose it. `update` on the console prints the same thing: the ask,
  its step, and how it ended.
- **An ask is taken once.** The same press again while it runs changes nothing;
  the same press after it ended has the same result posted once more, and nothing
  is done twice.
- **Nothing starts an update but a person.** A release existing starts nothing.

```
> update
...
asked     update 17 in hand at `fetching`: release 9, 0.8.0+1a2b3c4, 1446640 bytes
          no job is claimed until it has ended; 0.7.2+4a4e791 is what was running
```

### ⚠ Waiting for a board on the bench

These cannot be checked without hardware, and they come **before any owner is
told the firmware can be updated**. On a **V3** and on a **V4**, each
provisioned and running the release before this one, then upgraded with this
release's plain `.bin`:

- [ ] **⚠ The word is `one`, and the identity is the same.** `update` says
      `one`; `status` shows the same 64 hex characters it showed before the
      upgrade.
- [ ] **⚠ The slot is added and survives the reboot.** `update slot` says
      *added* with `app1`'s offset, then `reboot`; after it, `update` says
      `two` and prints both slots.
- [ ] **⚠ Nothing else moved.** After that reboot: `status` shows the same 64
      characters, the WiFi comes up by itself, and the exit node's page on
      meshmaster.dk shows a heartbeat **with the token it already had** — no
      re-provisioning, no new token.
- [ ] **⚠ A merged flash is `two` at once.** A board flashed with this
      release's merged image through `esptool` says `two` on its first
      `update`, with no `update slot`.
- [ ] **⚠ `update slot` on a `two` board writes nothing.** Typed a second
      time, it says the slot is already there and names its offset.
- [ ] **⚠ The image says what it is, and the keys are there.** `update`'s
      first two lines: `image  exit-node heltec_v3, band 868, version X.Y.Z+<sha7>`
      — the board's own name, and the version `status` prints — and
      `keys  1 release key trusted: 73efdd383a957596`. Neither *carries no
      descriptor this firmware can read* nor *no release key in this build*
      may appear on a released image: the first would mean the build did not
      place the 96 bytes, the second that it trusts nobody and would take no
      update at all.

**With a release of this firmware imported on meshmaster.dk and served to this
board**, on a V3 and on a V4 that both say `two`:

- [ ] **⚠ `update fetch <id>` ends *fetched*.** The ending line names the
      release's version, its byte count and the first sixteen characters of its
      hash, and the steps above it show the manifest, the signature, *checks 1,
      2 and 3 passed*, the ranges and *checks 4 and 5 passed*. Note **how long
      it took** and what the last call left of the heap — the ending line
      carries both figures, and they are the ones no session can measure.
- [ ] **⚠ `update` shows it in the other slot.** The `other` line names the
      same version, byte count and hash the ending did.
- [ ] **⚠ Nothing else changed while it ran.** `status` and `server` show
      the heartbeat still going out on its interval during the fetch, `mesh`
      shows adverts still being heard, and the console answered throughout.
- [ ] **⚠ After `reboot` the old firmware runs and `update` still shows the
      fetched one.** `status` shows the version it showed before the fetch, and
      the `other` line is unchanged — nothing was switched.
- [ ] **⚠ A release of the other board is refused with a number.** Fetch
      the V4's release on the V3: the ending is `check` 3, naming the board.
- [ ] **⚠ A release id that is not there is refused.** `update fetch 99999`
      ends `fetch` with *there is no such resource*, and `update`'s `other` line
      then says *holds nothing this firmware will boot* — a fetch clears the
      record before it asks for anything, because the slot is about to be
      written over.
- [ ] **⚠ A board that says `one` refuses before it asks.** On a board that
      has not had `update slot`, `update fetch 12` ends `slot` at once and the
      exit node's page shows no download against it.

**With a second release to update *to*** — imported on meshmaster.dk and
served to the board — on a **V3** and on a **V4** that both say `two`, with
release A
running and release B fetched:

- [ ] **⚠ `update apply` commits B inside two minutes.** After `update fetch`
      and `update apply` the board restarts, `status`'s first line says
      *on trial* and names B, and `update` shows the three conditions going
      `yes` one after another; inside two minutes `update` says
      `last      committed` with B and with A as the version it came from. The
      identity is **the same 64 characters** as before, and the exit node's page
      on meshmaster.dk shows B.
- [ ] **⚠ With no WiFi, ten minutes later the old one is back.** From B,
      fetch a third release, switch the access point **off**, then
      `update apply`. Ten minutes after the new image boots the board restarts
      by itself and B is running again. Switch the WiFi back on: `update` says
      `rolled back: no wifi...` and names the release it tried.
- [ ] **⚠ The power pulled one minute in is the same ending.** The same
      again, and the power pulled about a minute after the new image starts. At
      power-up **the old image runs** — the bootloader did it, not the
      application — and `update` says `rolled back` with *restarted during the
      trial* and the reset's reason.
- [ ] **⚠ A flash over USB still wins.** With the board running from the
      second slot (that is, after a committed update), flash the plain `.bin` of
      any release through the web flasher. After the flash the board restarts
      once more by itself and **that release runs**; `update` then shows it as
      the version it was switched to, from the one that was running.
- [ ] **⚠ Flashing the release it came *from* changes nothing, and the boot
      says so.** With B committed from A, flash **A**'s plain `.bin` through the
      web flasher. The flasher succeeds and the board still comes up on **B**;
      the log of that boot carries one line naming A and `update back`, and
      `update`'s `back` line names A too. This is the thing that was silent
      before task p1.16-64.
- [ ] **⚠ `update back` commits A inside two minutes.** From B, `update back`:
      the board restarts, `status` says *on trial* and names A, and inside two
      minutes `update` says `last      committed` with A, from B. The identity
      is the same 64 characters, and the exit node's page shows A.
- [ ] **⚠ And a `update back` whose trial fails ends where it was typed.** From
      A, with the access point switched **off**, `update back`: the board
      switches to B, B's ten minutes go by without the WiFi, and the board comes
      back on **A** — the image the command was typed in. Switch the WiFi back
      on: `update` says `rolled back: no wifi...` and names B.
- [ ] **⚠ `update back` is refused on a board that has never updated.** On a
      board that says `two` with nothing fetched, `update back` says the other
      slot holds nothing this firmware would boot, and nothing is switched.

**With meshmaster.dk's pile 4.42 deployed**, release A on a board that has
**never had a second slot** and release B released or canary for it, on a **V3**
and on a **V4**:

- [ ] **⚠ *Update to B* commits, from a board that says `one`.** Press *Update*
      on the exit node's Firmware tab: within two minutes the page says
      *handed over*; the board restarts **twice** (once for the slot, once into
      B); the page ends *updated to B* with a journal of `check`, `adding-slot`,
      `fetching`, `switching`, `proving` and the commit. `update` at the console
      says `asked     update <id> ended committed` and `two`, and `status` shows
      the same 64 hex characters it showed before.
- [ ] **⚠ The same press again refuses.** The page says *the version is the one
      it runs* — the refusal `current` — and nothing is fetched: the download
      count against the release does not move.
- [ ] **⚠ A job queued for a repeater of that exit node while the update runs is
      carried after it, and not lost.** Queue a `status` for one of its repeaters
      from the moment the page says *handed over*; it is claimed and run once the
      update has ended, and its result is on the job's page.
- [ ] **⚠ A job that was already running is finished first.** Start a
      `config.read` (eleven minutes at two transactions a minute), press
      *Update* while it runs: the job finishes and its result is posted, and only
      then does the board begin the update's steps.
- [ ] **⚠ The result survives a restart.** With the WiFi off, let an update be
      refused (`update fetch` cannot reach the server), then `reboot`, then put
      the WiFi back: within one heartbeat the page shows the result.
- [ ] **⚠ A board on an `other` table is refused and nothing is written.** On a
      board whose `update` says `other`, the press ends *refused* with `table`
      and the console's `update` shows the same table it showed before.

