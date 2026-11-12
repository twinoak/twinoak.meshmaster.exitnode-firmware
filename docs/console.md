# The serial console

The board is provisioned, watched and reset over its USB serial port: 115 200
baud, one command per line, a `>` prompt. Any terminal will do
(`pio device monitor -b 115200`, PuTTY, `screen`). Typed characters are echoed
by the board, so leave local echo off; the value of `set wifi.pass` and
`set meshmaster.token` echoes as asterisks.

The parser is a pure function (`src/console/Parser.cpp`) and every line shape
and every refusal below has a native test (`test/test_console_parser`). The
provisioning session in [`README.md`](../README.md) is replayed by the same
test, so it is the session the console accepts, line for line.

## Commands

| Command | What it does |
|---|---|
| `help` | the commands and the keys, with one line of help each |
| `set <key> <value>` | store a setting in NVS; it takes effect at once (see below). The value is the rest of the line, trimmed; a WiFi name with spaces is fine |
| `get <key>` | print one setting. `wifi.pass` and `meshmaster.token` print `set` or `unset` and never the value |
| `get` | print every setting, one per line, in the order of the table below; an unset key shows its fallback in brackets |
| `status` | firmware, board and reset reason, the public key, the radio and its frame counters, WiFi, time and where it came from, the server's last answer, the heartbeat, the flood scope and what each shape has earned (task p1.8-32, below), the observer (its two slots and their states, or why it is off, how many packet messages it has made and how many slots are connected), the heap, and the uplink's kept connection with what the last call cost (task p1.8-30, below) |
| `mesh` | the adverts heard: how many, how many are waiting to be posted and how many the ring dropped, and the last five with their names, roles, hops, SNR and RSSI |
| `server` | the exit node's loop: the last heartbeat and the next, the last observation batch, the job counters, and the four error counters the heartbeat itself carries |
| `jobs` | the airtime budget in force and what it has spent, how many claims were made, the thirteen job types this firmware advertises, and the last ten jobs with their outcome, their transaction count and when they ended |
| `sessions` | the repeater logins the board holds: per repeater the first twelve characters of its key, the class the session serves, the level the repeater granted and its age. A session is a key, a class, a role byte and a time; no secret is in one, and none is printed |
| `errors` | every counter the heartbeat carries, under the name the exit node's page shows it by, with what it means and the figures beside it that never leave the board |
| `reboots` | why this board is running: this boot's reset reason, how many restarts there have been since the last power cut, and the last five with their reason, the time and the uptime they happened at. The log is in RTC memory, so it survives a watchdog reset and a `reboot`, and a power cut clears it |
| `dump` | the last 4 kB of the log, oldest first, whole lines only — a board's answer to the Pi agent's journal. The same bytes the port printed and no others |
| `wifi` | a blocking scan (a few seconds): every network with channel, RSSI and security, then the link's state |
| `reboot` | restart; the reason is noted so that `reboots` after it says *asked for on the console* |
| `erase confirm` | forget every setting. The identity is not a setting and stays, so the exit node keeps its key on meshmaster.dk. Without the word, `erase` refuses and says what the word is |
| `get flood.scope` | the region every flood carries, and a second line with the region's transport key and the code of a fixed example payload, so two boards can be compared (task p1.8-32, below) |
| `get mqtt.presets` | the broker presets an `mqttN.preset` can name — `meshview`, `meshmapper`, `custom`, `none` — with each one's broker and sign-in (task p1.7-26) |
| `get mqtt.status` | the observer's broker slots — two on either board (tasks p1.8-31 and p1.9-37) — one line each: preset or host, state (`waiting-for-wifi`, `waiting-for-clock`, `waiting-for-heap`, `connecting`, `connected`, `backing-off`), since when, published, dropped, queued, connects, refusals, errors, token renewals, how often it gave way to the uplink, and the last error — then the memory rule the slots live under with this board's figures and what the heap is now, the neighbour report's `nbr:` line (task p1.8-33: when the next cycle runs, how the last report went, how many neighbours are in the table and how many answered), what the uplink's last call cost, where the TLS buffers are (PSRAM on a V4) and the least stack each slot's task ever had (task p1.7-27, [`observer.md`](observer.md)). A board whose profile opens one slot takes `none` and nothing else in the slots beyond it: `error: mqtt2.preset: this board has memory for one broker only` — neither board this repository builds is one since task p1.9-37 |
| `discover.scopes` | the neighbour table — the repeaters heard at zero hops, each with its SNR, RSSI, when it was last heard and what the last cycle's regions request found — and a cycle of the scope sweep started now: sixty seconds of node discovery, then one anonymous regions request per neighbour, newest first, each a transaction on the airtime budget and none while a job is open; the report goes to every connected broker slot (task p1.8-33, [`observer.md`](observer.md#neighbours)). The observer firmware's command, as typed there |

A refusal is one line starting with `error:` and names what was wrong:
an unknown command or key, a missing value, a value out of range, a scheme
that is not `https://`, a line over 200 characters, a control character.

Two lines of the observer firmware's guide are **answered rather than
stored**, with one line each and no `error:`: `set timezone …` (this firmware
keeps UTC from the server or SNTP, and what it publishes is stamped in UTC) and
`set bridge.source …` (an exit node publishes what it receives and forwards
nothing). `set wifi.pwd …` is that guide's name for `set wifi.pass …` and is
the same key: redacted on echo, `set` or `unset` in `get`.

## Keys

| Key | Value | Fallback while unset |
|---|---|---|
| `wifi.ssid` | 1..32 characters | none: WiFi stays off |
| `wifi.pass` | 8..63 characters, the WPA passphrase; never printed | none |
| `meshmaster.url` | `https://host[:port][/path]`, the scheme required, at most 128 characters | `https://meshmaster.dk`: the project has one server, and this is only set to point a board elsewhere |
| `meshmaster.token` | the agent token from `/exit-nodes/new` on the server: `mma_` and base64url; never printed | none: no call is made |
| `radio.freq` | MHz, 150..2500 | 869.618 |
| `radio.bw` | kHz, 7.8..500 | 62.5 |
| `radio.sf` | 5..12 | 8 |
| `radio.cr` | 5..8 | 8 |
| `radio.tx` | dBm, -9..22 | 22 |
| `log.level` | `error`, `warn`, `info`, `debug` | `info` |
| `log.frames` | `on`, `off`: one log line per frame, both directions. A frame of a job in flight - the login, the request, a CLI line and its reply, a trace - is printed as hex, header, path and payload; a login that carries a password, and the `password` line of a rotation and the reply that echoes it, are printed with their encrypted bodies as one `*` per byte, which is the .NET agent's redaction of `CMD_SEND_LOGIN` and of a sensitive CLI line; an advert and the mesh's own chat are the header line only, never their bytes | `off` |
| `display.timeout` | seconds, 0..86400: how long the OLED stays lit after a button press | `0`: it never turns off. `300` is the five minutes this firmware did before task p1.2-09 |
| `flood.scope` | the region every flood carries: a region name of at most 30 characters (the firmware's own region-name characters — letters, digits and `-`; a leading `#` is accepted and dropped), or `*` for no transport code at all. A private region (`$name`) is refused: the setting takes names, never keys | `dk`, the Danish standard's region |
| `flood.unscoped` | `retry`, `never`: whether the **second** flood of a transaction drops the scope, which is one try at the shape a repeater with no region map still forwards | `retry`, while unscoped floods are forwarded at all |
| `mqtt.origin` | 1..31 characters: the observer's name in what it publishes | `MeshMaster-` and the first four characters of the key |
| `mqtt.iata` | three letters, any case typed, stored uppercase: the region the brokers file this node under — `AAL`, `AAR`, `BLL`, `ODE`, `CPH` or `RNN` in Denmark | none: no slot on the default topic publishes until it is set |
| `mqtt.owner` | the owner's public key, 64 hex digits, named in the sign-in token | none: the token names no owner |
| `mqtt.email` | the owner's email, `name@host`, at most 63 characters, named in the sign-in token | none |
| `mqtt.neighbors` | `on`, `off`: the neighbour report — the repeaters heard at zero hops and the scopes each one answers with — published to every connected slot once per interval (task p1.8-33, [`observer.md`](observer.md#neighbours)). The observer firmware's default is `off`; here an observer is already opt-in slot by slot, and the report is why one is on | `on` |
| `mqtt.neighbors.interval` | hours between neighbour reports, 12..336 | `24` |
| `mqtt1.preset`, `mqtt2.preset` | `none`, `meshview`, `meshmapper` or `custom` (`get mqtt.presets`) | `none`: the slot is off |
| `mqttN.server` | `custom` only: `wss://host[:port][/path]` or `mqtts://host[:port]`, at most 63 characters. TLS only — `ws://` and `mqtt://` are refused, because every connection this firmware makes is verified | none |
| `mqttN.audience` | `custom` only: the broker's name as the token's audience, a host name of at most 63 characters. Set, the slot signs in with the token; unset, with `mqttN.username` and `mqttN.password` | none |
| `mqttN.topic` | `custom` only: a topic template with `{iata}`, `{device}` (the key, uppercase hex) and `{type}` (`status`, `packets`); printable, no space, no `+` or `#`, at most 95 characters | `meshcore/{iata}/{device}/{type}` |
| `mqttN.username` | `custom` only, 1..31 characters: for a broker that wants a username instead of a token | none |
| `mqttN.password` | `custom` only, 1..63 characters; never printed | none |

The radio's fallbacks are the Danish MeshCore channel, the one the field
repeaters run; the ranges are MeshCore's own. A `set` takes effect at once:
`wifi.*` drops the link and rejoins, `meshmaster.*` starts the server loop
again (and forgets a 401), `radio.*` reprograms the SX1262, `log.*` changes
what is printed, `display.timeout` restarts the panel's timer, `mqtt.*` and
`mqttN.*` make the observer read its settings again — a slot whose broker,
sign-in or topics changed closes and connects again with the new ones, the other
keeps its session, and no `mqtt` key needs a reboot.

The `mqtt` keys are the observer's (task p1.7-26, `src/observer/`): the names
and the shapes are the observer firmware's
([agessaman/MeshCore, branch observer-firmware](https://github.com/agessaman/MeshCore/blob/d664d89df66bdacc1319900a2e2d152fa31b9dc9/MQTT_IMPLEMENTATION.md)),
so its Danish guide typed on this console ends in the same two slots. What is
published, to whom, over which roots and what it costs in memory is
[`observer.md`](observer.md): two TLS WebSocket connections at most, signed in
with the node's own token, verified against the roots in `src/agent/TlsRoots.h`,
and the first thing to give way when the MeshMaster uplink is short of heap.

`radio.freq`, `radio.bw`, `radio.sf` and `radio.cr` are also what the heartbeat
reports as its `radio` block, which is what the exit node's page on
meshmaster.dk shows and what it warns about when it differs from the network
standard ([EXIT-NODES §10](https://github.com/twinoak/meshmaster.dk/blob/master/spec/EXIT-NODES.md#10-exit-node-lifecycle-and-health)).
`radio.tx` is the board's own and is not reported:
the record has no field for it.

Everything is stored in NVS under one namespace, `meshmaster`, one entry per
key. Flash encryption is the owner's choice and is not assumed: without it, NVS
is readable by anyone with the board in hand and a cable.

## What the port prints besides replies

The log: `[HH:MM:SS] I text` once the clock is known, `[+123s] I text`
before that, at `log.level` and below. Every state change of the link, the
clock and the server call is one line, so a bench walk is reading:

```
[+1s] I meshmaster-agent 0.0.0-dev on MeshCore companion-v1.18.0
[+1s] I board: heltec_v3, Power-on reset
[+2s] I radio: 869.618 MHz, bw 62.5 kHz, sf 8, cr 8, tx 22 dBm
[+2s] I identity: 7f3a...
[+2s] I clock: no time yet
[+2s] I wifi: connecting to Twinoak (attempt 1)
[+5s] I wifi: connected, 192.168.1.20, rssi -61 dBm
[+5s] I clock: sntp started against pool.ntp.org
[+7s] I server: GET https://meshmaster.dk/agent/v1/config (root: ISRG Root X1)
[12:03:12] I clock: 2026-09-28T12:03:12Z from sntp
[12:03:14] I uplink: handshake 1 for call 1 (the first call)
[12:03:14] I heap: 187432 free after the call (231212 before, lowest 152880 during), min ever 152880
[12:03:14] I server: config ok: agent 17 "Window at Grenaa", transport onboard, budget 2/min gap 20 s, poll 30 s, serverTime 2026-09-28T12:03:14Z
[12:04:19] I observations: 20 posted, 20 accepted
[12:06:31] I uplink: handshake 2 for call 7 (the server closed it)
```

`uplink: handshake N for call M (why)` is printed at every TLS handshake and
never for a call that went over the kept connection (those are one line each at
`debug`), so at `info` the log says how often the connection is remade and why:
`the first call`, `the server closed it` (its idle timeout, about two minutes -
see the `uplink` line below), `the root changed`, `the last call failed`,
`dropped on purpose` (a `set meshmaster.*`), or `the WiFi link was remade`. A
kept connection that turns out dead when a request goes over it is
`uplink: the kept connection was dead under the request; reopened inside the
call`, and the call is answered on the new connection: nothing the loop sees.

### The `uplink` line of `status`

```
heap      free 136580, min ever 97212, largest block 73716; after the tls call 136580
uplink    connection kept since 12:03:14 (root ISRG Root X1); 2 handshake(s) for 41 call(s), 39 reused, 0 reopened in-call; last call 136580 free before, lowest 97212 during, 136412 after
```

Since task p1.8-30 the uplink keeps **one** TLS connection to meshmaster.dk
between calls (`src/agent/KeepAlive.h`): the handshake happens at the first call
and again only when the connection is gone. The line says:

| | |
|---|---|
| `connection kept since HH:MM:SS (root …)` | there is a connection, made at that time, verified against that root. Otherwise `no connection kept (why)`: `the server closed it`, `the last call failed`, `the server answered and closed`, `the root changed`, `dropped on purpose` |
| `N handshake(s) for M call(s)` | TLS handshakes since boot against calls since boot. One per connection, not one per call: on a board whose calls are 30 s apart this is a handful against hundreds. `reused` is the calls answered on a kept connection; `reopened in-call` the kept connections that were dead under a request and remade inside the same call |
| `last call … free before, lowest … during, … after` | what the last call cost the heap. *Lowest* is the lowest free heap seen during the call: **exact for mbedtls's own allocations** - every one of them goes through a wrapper of its allocator while a call is in flight, which is the whole handshake and both 16 KB content buffers - sampled at four points for the rest (lwIP, HTTPClient's buffers), and exact for everything if the board's `min ever` moved during the call. The handshake is the peak; a reused call dips far less |

**How long the server keeps an idle connection.** Measured from a PC on
31 October 2026 with one TLS connection, one request and then silence:
meshmaster.dk (IIS, its default `connectionTimeout` of 120 s) closed it after
**128 s** of idling; a second request after 30 s and after 100 s of idling was
answered on the same connection. The heartbeat is every 30 s with work and
every 120 s idle, timed from the end of the previous call, so the kept
connection is normally still there at the next heartbeat; a job, a hold or a
backoff in between outlasts it, and the next call handshakes again - one
`uplink: handshake` line, and not a failure. The two figures are what task 31
sets the observer's gate from.

### The `flood` line of `status`, and `get flood.scope`

```
flood     scope dk, answered scoped 41 / unscoped 3 / none 2 since boot; the second flood of a transaction goes unscoped
```

A *flood* is what this board sends when it holds no path to a repeater — a
login, and path discovery — and nothing else. Since task p1.8-32 every one of
them carries a **transport code** for the region `flood.scope` names
(meshmaster.dk's D-47), which is what a repeater matches its region map against
before it forwards one; the first flood of a transaction is scoped and the
second drops the scope, while `flood.unscoped` is `retry`. Direct-routed
traffic, which is most of what MeshMaster sends, is untouched: a repeater only
checks the scope of a flood.

The three counts are **per flooded transaction** since boot — a transaction
being one retry loop: a login's two tries, a request and its retry, a discover's
flooded login and the path-discovery request after it — and each transaction
moves exactly one of them, by which of its floods was answered. A transaction
that never flooded moves none, which is every transaction of a board whose
repeaters all have paths. `unscoped` is the number the whole thing exists for:
it says, in figures, whether the second try still finds anything. The clause
after the counts is the rule in force: *the second flood of a transaction goes
unscoped*, *every flood is scoped (flood.unscoped never)*, or *every flood goes
out with no transport code (flood.scope \*)*.

`get flood.scope` prints a second line with the region's key and a code to
compare boards by:

```
> get flood.scope
flood.scope unset (dk)
scope     dk: key 4a447e7539b0418bd14cf28aa8241529 (sha-256 of "#dk", first 16 bytes; public by construction), example payload -> transport code 0x234b
```

The key is the first sixteen bytes of SHA-256 over `#` and the name
(`TransportKeyStore::getAutoKeyFor` at the pin), so it is public by
construction and printing it gives nothing away — a *private* region (`$name`),
whose key lives in a node's own keystore, is refused by `set` for exactly that
reason. The example payload is fixed so that the code is comparable: payload
type 0 and the eight bytes `00 01 02 03 04 05 06 07`
(`src/mesh/TransportScope.h`). Two boards given the same scope print the same
two values; a board that prints others is not flooding the same region.

At `log.level debug` there is one line per heartbeat reply and one per advert
heard - the key's first four bytes, the role, the hops and the SNR, and never
the name, which is read on demand with `mesh`:

```
[12:03:16] D server: heartbeat reply: serverTime 2026-09-28T12:03:16Z, budget 2/min gap 20 s, poll 120 s, jobs waiting no
[12:03:41] D advert: a1b2c3d4, repeater, hops 0, snr 7.25; 3 waiting to be posted
```

A job is one line per step, and the same text goes to the server as the job's
`log`:

```
[12:31:02] I jobs: 2 claimed
[12:31:02] I jobs: job 12345 status for repeater 17 "Risskov Water Tower" (acl)
[12:31:07] I jobs: job 12345: ok after 2 transaction(s)
[12:31:27] I jobs: job 12346 telemetry for repeater 17 "Risskov Water Tower" (acl)
[12:31:31] I jobs: job 12346: ok after 1 transaction(s)
[13:00:04] I jobs: job 12400 config.read for repeater 17 "Risskov Water Tower" (admin)
[13:04:51] I jobs: job 12400: ok after 21 transaction(s)
```

The job's own log, which the server shows on the job's page, says the same in
more words: which login was sent and what it was answered (`login (admin) try
1: logged in as admin, firmware level 2 (direct)`), each command and its
outcome, and for a repeater this exit node holds a grant on the session's last
command (`restore: cli 'setperm <own key> 1': ok`, then `the ACL entry of this
exit node's key is back at read-only, as granted; the session is now read-only
(task 60)`). A password is never an argument of any of those lines.

**That last line cannot be asked twice** (task p1.10-48), and neither can the
grant, which is the same line: the login table is the access list, so once
`setperm <own key> <level>` has run this board is no longer admin on that
repeater and every further console line from it is dropped without a word. It is
therefore sent **once**, and when nothing answers it the entry is read back with
a blank login instead of the line being retried:

    cli 'setperm c3d4e5f60718... 1' try 1: no reply (direct)
    cli 'setperm c3d4e5f60718... 1': no answer, and this line cannot be asked
      twice - if it ran, this key is no longer admin and the repeater drops a
      retry from it without a word. What it did is read back with a blank login
      instead (task p1.10-48)
    the entry check (blank login): answered at read-only (direct)
    the blank login was answered at read-only, which is the level the line asked
      for: the line ran and its "OK" was lost - confirmed by the login and not by
      an "OK" (task p1.10-48)

A check answered **at admin** means the line never arrived: the line goes once
more and is checked once more (`the line did not arrive, and the session is
admin again - sending it once more`), two rounds at most, and after that it is
not confirmed. A check answered **at guest** confirms a level-0 line - the key
is off the access list, which is what a revoke asked for - and a check nothing
answers confirms nothing at all (`the blank login was not answered either: a
repeater with a guest password answers a key it does not hold with silence, and
so does one that is off the air`). So `jobs` shows an `acl.set` or an admin job
whose restore had to be checked at **one transaction more** than before, and the
result of a grant that could not be confirmed says so for meshmaster.dk's verify
step to settle. After a **confirmed** grant of this board's own key the session
is **not kept** (`the entry set is this exit node's own: the session is not
kept, so the next job for this repeater logs in by key`), so the verify that
follows is a login by this board's key and not a reuse of the password's
session.

A login is tried twice: `login (admin) try 1: no reply (flood, scope dk)`, then
`try 2: … (flood, unscoped)` — the word beside `direct` or `flood` is the
*shape* the packet went out in, which since task p1.8-32 names the flood scope
(below),
and two silences are `unreachable`. **Only a login reply ends a login's wait**
(task p1.10-47): a login reply carries the repeater's own clock and no tag of
ours, so the next response from that repeater reaches the runner whatever it is,
and one that is not a login reply - most often the late reply to the request that
had just timed out - is dropped with a line of its own (`login (acl) try 1: the
late reply to a request this job sent arrived instead; dropped, and the login's
wait goes on`) while the wait runs to its end. Stock firmware answers a password it does
not know with the same silence a repeater off the air gives, and in one place
that matters: a job handed the **new** candidate of an unverified credential
rotation (meshmaster.dk's CONFIGURATION §7 step 4), where the server's fallback
to the old candidate is keyed to a refusal. So on that path alone a silent first
try is followed by one **blank login** - this board's own key and no password,
the `acl` login every status read makes, which any repeater on the air that
holds the key's entry answers - before the second try: `reachability probe
(blank login): answered at read-only (direct)`, and when the second try is then
silent too the job is `unauthorized` rather than `unreachable`, its log ends
`the repeater answered a blank login at read-only and then said nothing to the
new candidate: on stock firmware that silence is a refusal of the password (task
p4.7-95); the server will try the old candidate next`, and its `data` is
`{"reason":"silentAfterReachable","probeAnsweredAs":"read-only"}`. A probe that
is silent too (`the blank login was not answered either`) proves nothing and the
job is `unreachable` as before. There is no third answer: since task p1.10-47 a
response that is not a login reply does not end the probe's wait, it is dropped
(`reachability probe (blank login): a reply that is not a login reply arrived
instead`) and the wait goes on, so `probeAnsweredAs` is one of the four levels
and never `refused` - nothing of the firmware this board reads refuses a login
at all. One transaction more, only there - `jobs`
counts **3** for that job - and no session is stored by the probe. An `old`
candidate, or a job with none, never probes. The serial console says the same
in one line per probe (`jobs: job N: reachability probe (blank login): …`).

A **guest** job whose claim carried no password at all logs in **blank**, and its log
says so in one line: `no guest password in the claim: the blank login, which a
repeater with no guest password answers at guest`. That is meshmaster.dk's guest
door in its empty form (its tasks p4.7-94 and p4.11-111, and task p1.4-14 here),
and the pair's `discover` runs at class `guest` too: `handleLoginReq` compares the
password with `_prefs.guest_password`, which the firmware leaves empty, so a
repeater whose owner never set one answers that login *at guest* and the job runs
on the session it opened (`the guest session is open for 600 s`). A repeater that
*does* have a guest password answers the same login with silence, and the job is
`unreachable` after the two tries - by design on both sides. An **admin** job whose
claim carried no admin password is still `unauthorized` with nothing sent at all
(`the claim carried no admin material; nothing to log in with`): there is no admin
login without one.

A `status` is the one read whose transaction count moves with what the claim
asked for: it makes a second request (`owner info`) only when the claim's
payload wants the reply, which the server asks for once a day per repeater and
not on the other reads of that day (meshmaster.dk's task p4.3-68). So `jobs`
shows such a read as **2 transaction(s)** the first time after a grant and after
each day's refresh, and **1** the rest of the time; a payload that says nothing
at all - an older server - is two every time, which is what the count was
before. Nothing is logged when the claim's payload is what skipped it, because
nothing was sent.

That request goes at **every class** since meshmaster.dk's task p4.30-181, not
at `acl` alone: its reply is three lines - the firmware version, the node name
and the owner info - and `handleRequest` checks no permission at all for it, so a
guest login is answered the whole of it. The result carries all three
(`firmware`, `nodeName`, `ownerInfo`), each absent when the request was not made;
a value longer than the firmware's own field (31 characters for the name, 119 for
the owner info) is kept, cut, and the log says it was cut. **One repeater is not
asked**: a login reply that said `FIRMWARE_VER_LEVEL` below 2 is a repeater that
does not know the request and would answer it with silence, so the request is
skipped and the log says why in one line - `owner info not asked for: the login
said firmware level 1, and the request arrived at level 2`. A job that reused an
earlier job's session was told no level, and *not knowing is not a reason to
skip*: it asks.

The writes (task 05) say what they sent and what came back, and the .NET agent's
words for it: `apply plan 3f2a…: 2 command(s), single attempt, stop at the first
refusal or silence`, then one line per command (`'set advert.interval 120' try
1: ok`, or `flood.max: the repeater answered 'set flood.max 100' with 'Error,
max 64', and this line wanted 'OK'`), the re-read (`re-read 'get
advert.interval' try 1: ok`), and `applied 1 of 3 command(s) as the repeater
answered them; re-read 2 of 2 touched key(s)`; a `cli` says `the repeater
answered 'neighbors': …`; a `reboot` says `no restore on this session … (task
60)` and then `reboot sent; the firmware does not answer one`; a `password.set`
says `cli 'password <new>' try 1: ok`, `the repeater confirmed the new admin
password`, the restore, and `the session is dropped`.

What it never prints: the passphrase, the token (not in a reply, not in a log
line, not in `status`), a repeater's password (the claim carries one for every
admin job - a `discover`, an `acl.set`, a `config.read`, and the six writes -
and the new one of a `password.set` beside it; each lives in one fixed buffer
for the job, is zeroed when the job ends, reaches no log line and no NVS key,
and `log.frames on` prints the login that carried it, the `password` line and
the echo that answers it with their bodies as stars), and
anything the mesh *says* - an advert's name is read on demand with `mesh` and
never logged, the mesh's own chat is counted by type and dropped unread, and
`log.frames on` prints the bytes of a job's own frames and of nothing else.

## The OLED

Four lines, redrawn once a second, and the panel **stays on**: `display.timeout`
is the seconds of no button press after which it goes dark and its default is
`0`, which never turns it off. A **click on the user button** (PRG, GPIO 0)
toggles it - off when on, on when off - and restarts the timer when one is set.
Holding PRG through a reset is the ESP32-S3's own bootloader and is nothing this
firmware does.

```
ID: 46F6     12:03:14
WiFi: yes Online: yes
Heartbeat: 12:03:14
Idle, last 12:34:56
```

| Line | What it says |
|---|---|
| 1 | the first four characters of the companion key - the prefix meshmaster.dk shows this exit node by - and the clock right-aligned, or `no time` until it is set. `no identity` alone when the radio made none. The IP address is not here; `status` has it |
| 2 | `WiFi` is the link, `Online` is *the last call to meshmaster.dk was answered and the next one is not waiting*: a heartbeat has happened, nothing has gone unanswered since, the token has not been refused, and no backoff or `Retry-After` hold is running |
| 3 | when the server last heard from this board, `Heartbeat: none` until the first. With an observer slot configured the word is cut to `HB` and the broker connections take the right edge: `HB 12:03:14  MQTT 2/2` is connected slots over configured slots (task p1.7-27) |
| 4 | what it is doing, and when it is doing nothing, why - the table below |

**Line four is the one to read when something is wrong.** Line two's *no* says
something is wrong; line four says what.

| Line four | What it means |
|---|---|
| `Job 1234 acl.set` | a job is on the air: its id and its type, live, because the job's own waits pump the display |
| `Idle, last 12:34:56` | nothing is running; the newest job ended then |
| `Idle, no jobs yet` | nothing has run since this board started |
| `token revoked/wrong` | a 401: nothing is sent until `set meshmaster.token <token>` or a reboot |
| `no wifi` | the link is down, so the server cannot be reached either |
| `no token: set it` | the link is up and `meshmaster.token` is unset: `set meshmaster.token <token>` |
| `no server: retry 30s` | the link is up and there is a wait before the next call - a backoff, or a 503's own `Retry-After` through a deploy - and the countdown is how long |

The radio keeps running and hearing through all of it: the mesh is pumped by
the same loop, the adverts go into the ring, and they are posted when the
server comes back. What the four lines say is a pure function of a struct
(`src/ui/DisplayLines.h`) and every line above has a native test
(`test/test_display_lines`); the toggle and the timeout need the board.

## The bench walk

1. Flash (README.md), open the port, see the banner and the prompt.
2. `status`: an identity (64 hex characters), the radio parameters, `wifi off`,
   `no time`, `server not configured`, a heap figure.
3. `set wifi.ssid ...`, `set wifi.pass ...`: within a few seconds
   `wifi: connected` and an address; then `clock: ... from sntp`. Pull the
   network cable from the router's uplink to see the retry lines back off (5 s,
   10 s, 20 s ... up to five minutes) and the link come back by itself.
4. The token from `/exit-nodes/new` (`set meshmaster.url` only for a server
   that is not meshmaster.dk): `server: GET ...`, then `heap: ... free after the call`
   and `server: config ok: agent <id> "<label>" ...`. A wrong token is
   `server: http 401`, and that **halts the loop**: nothing more is sent until
   `set meshmaster.token <token>` or a reboot, and the OLED's fourth line says
   `token revoked/wrong`. A URL the chain does not verify for is
   `server: no reply: connection refused (tls -9984: ...)` and the next attempt
   tries the core's certificate bundle. `status` then shows
   `server ... config ok at HH:MM:SS ... root ISRG Root X1`, the heap after
   the TLS call, and the `uplink` line: `connection kept since HH:MM:SS`,
   `1 handshake(s) for 1 call(s)` and what that first call cost - the numbers
   every later task budgets from. After ten minutes the handshake count should
   still be a handful while the call count has climbed.
5. Block UDP 123 at the router and reboot: `no time` until the config call,
   then `clock: ... from the server (corrected by ... s)`.
6. The loop: `server` shows the last heartbeat, when the next is due, the poll
   interval the server answered (30 s or 120 s) and the counters. The exit
   node's page on meshmaster.dk goes *online* within one interval.
7. The air, still passive: `mesh` after some minutes at a window shows adverts
   heard and the last five with their names; `observations: N posted` follows
   in the log, and the keys turn up under **Unknown nodes** on the server. With
   no repeater associated to this exit node, `status` still shows **`tx 0`**.
8. The jobs. Associate a repeater with the board's key on meshmaster.dk (the
   wizard, as with the Pi), then press **Read now** on its page. Within a poll
   interval the log shows `jobs: 2 claimed` and a line per job; `jobs` on the
   console shows them with their outcome and their transaction count, and the
   repeater's page shows the reading. `status`'s `tx` is now the number of
   transactions and nothing more: **one packet per transaction** is the rail,
   and `jobs` is where the arithmetic is checked.
9. `neighbours` and `trace` from the repeater's **Monitoring** tab: the first
   costs one request per nine neighbours, the second none at all if no path is
   known yet (`unreachable`, and the log says *run discover first*).
10. Leave it: `status` after an hour shows frames received, a `tx` that matches
    the transactions `jobs` counted, and a heap figure that has not moved.
11. The association through the board (task 04). Run the wizard on meshmaster.dk
    with this exit node: the `discover` logs in with the admin password (the log
    says `login along the path the server holds` or `flooded login`, then
    `logged in as admin`), the grant's `acl.set` reuses that session (`session
    reused (admin, admin, N s old)`, one login across the two jobs, and `the
    entry set is this exit node's own: the session is now read-only`), and the
    verify's `status` at class `acl` logs in blank. `sessions` between the steps
    shows the one session and its level moving from admin to read-only.
12. *Read the configuration* on the repeater's page: 19 transactions and the
    login, 21 in all with the restore, and `log.frames on` shows the last CLI
    frame sent is `setperm <this board's key> 1`. The next read's `acl` value
    lists the board's key at level 1 (`<12 hex>:1`), and the result's
    `aclRestored` is `true`. With `set log.frames on` the login frame prints as
    hex up to the key and then stars, never the body.
13. A night of probes with the association `active`: `sessions` in the morning
    shows a read-only session or none, never an admin one, and the association
    on the server has not moved to `needs-regrant`.
14. The first apply through the board (task 05; meshmaster.dk's
    `tasks/phase-4/FIRST-APPLY.md`, step one): one harmless key,
    `advert.interval`, from the Configuration tab. The log shows the plan line,
    `'set advert.interval 120' try 1: ok`, the re-read of that key and `ver`,
    `applied 1 of 1`, and the restore last; `jobs` counts **5**: the login, the
    set, the two gets, the restore. `get advert.interval` on the repeater's own
    console must say what MeshMaster says.
15. The refusal, then the partial plan (FIRST-APPLY steps two and three): `set
    advert.interval 15` from the CLI console passes the server's blocked list
    and comes back `Error: interval range is 60-240 minutes`, which the job
    records verbatim and reports `failed`; a plan whose second command the
    repeater refuses leaves the third `not-sent` and reads back the two that
    were sent. With `log.frames on`, every `set` prints exactly **once** as a
    `frame tx txt` line, and the timestamps in those frames (the first four
    payload bytes are ciphertext; read the job's log instead) are never repeated.
16. The small jobs from the repeater's **Ask the repeater** row: `advert`
    (`OK - Advert sent`, and the board hears the advert within the minute -
    `mesh` shows it), `time <unix>` (`OK - clock set: HH:MM - D/M/Y UTC`, or
    `(ERR: clock cannot go backwards)` recorded as the refusal it is; the value
    sent is this board's clock, so `status` must show a time from SNTP first).
    **Reboot** only with the cable in: the job is `ok` as *sent* on silence, the
    log says why no restore was sent, and the next status read confirms it by
    the uptime on the server.
17. A rotation on a test repeater (`docs/ops/credentials.md` §6 on
    meshmaster.dk): `cli 'password <new>' try 1: ok`, `the repeater confirmed
    the new admin password`, then `restore: cli 'setperm <own key> 1': ok`
    **after** it, then `the session is dropped`; `sessions` shows none for that
    repeater. With `log.frames on` the `password` frame and its reply print as
    two hex bytes and then stars. `jobs` counts **3** through an acl pair: the
    login, the password line, the restore. The next admin job logs in with the
    new password. **A rotation whose reply was lost** (task p1.3-13): make the
    repeater's answer go missing (power it off between the `password` line and
    its echo, or rotate to a password the repeater will not take), so the
    server marks the row `unverified`. The next admin job's log says `the claim
    carries the new candidate of an unverified credential: logging in afresh
    with it`, `login (admin) try 1: no reply`, then `reachability probe (blank
    login): answered at read-only` and, after `try 2: no reply`, `the repeater
    answered a blank login at read-only and then said nothing to the new
    candidate …`; `jobs` counts **3** and `unauthorized`, the serial line says
    `reported unauthorized (silentAfterReachable)`, the job's page shows
    `reason: silentAfterReachable`, and the attempt after it is handed the old
    candidate and logs in. With the repeater off the air instead, the probe
    says `no reply` and the job is `unreachable` at 3, nothing noted.
18. Staying up (task 06). Pull the router's uplink for ten minutes with a job
    in flight: the log backs off (`server: next attempt in 30 s (the 30 s
    step, jittered)`, then 60, then 120 and **120 again** — two minutes is the
    ceiling once WiFi is up and the clock came off the network, task p1.2-10),
    the OLED's fourth line says `no server: retry Ns`, `mesh` keeps counting
    adverts, and `jobs` shows the result waiting under `buffer`. Plug it back in: `jobs: N result(s) that
    waited went through` and the job's row on the server fills in, late and
    complete. Pull the *WiFi* instead and the fourth line says `no wifi`.
    Then `errors` (every counter, and how many failures from a
    restart), `reboots` (`Power-on reset` and an empty log on a board that has
    just been plugged in), and `dump` (the last 4 kB of exactly what the port
    printed - no secret can be in it that was not on the terminal already).
19. The guest door at its empty form (task p1.4-14), on a repeater whose owner
    never set a guest password - meshmaster.dk's repeater 10 through this exit
    node is the one waiting. Associate it from the wizard's second door, *I have
    the guest password only — or none*, with the field left empty. The pairing
    `discover` now runs at class **guest** instead of being answered
    `unsupported`: the log says `no guest password in the claim: the blank login,
    …`, then `flooded login, for the path its reply comes back along: logged in
    as guest`, `path of N hop(s), learned from the login` and `the guest session
    is open for 600 s`; the job is `ok` with `loggedInAs: guest`, the verify's
    `status` at guest reuses that session, and the pair goes `active`. `sessions`
    shows one **guest** session, and the Neighbours tab's read - which asks at
    guest once the pair is active - fills. On a repeater that *does* have a guest
    password the same `discover` is `no reply` twice and `unreachable`, which is
    the right answer and not a fault.
20. The flood scope (task p1.8-32). `get flood.scope` prints `dk` and the key
    `4a447e7539b0418bd14cf28aa8241529` with the example code `0x234b`; a second
    board prints the same two. Then a **flooded** login: press *Discover* for a
    repeater this exit node holds no path to, or `set` a wrong path and let one
    go stale. The job's log reads `flooded login, for the path its reply comes
    back along: … (flood, scope dk)`, and `status`'s `flood` line moves by one -
    `answered scoped 1` when the repeater's region map holds `dk`, `answered
    unscoped 1` when it does not and the second try got through, `none 1` when
    neither did. A repeater that follows the Danish standard (`region` on its own
    console lists `dk` with `F`) answers the scoped try; one with an empty region
    map answers only the unscoped one, which is what `unscoped` counts and the
    reason the retry exists. `set flood.unscoped never` and repeat on the
    repeater with no region map: both tries now say `flood, scope dk` and the
    count is `none`. `set flood.scope *` and repeat: every line says
    `flood, unscoped`, and the exit node's page on meshmaster.dk shows the scope
    and the three counts within a heartbeat.
21. A guest answer the job can use (task p1.10-46; meshmaster.dk's p4.30-180).
    On a test repeater with **no guest password** set and an `active` `acl` pair
    with this exit node, take the entry away from its own console:
    `setperm <this exit node's key> 0`. Then press *Read the neighbours* on the
    repeater's page, which asks at class `acl`. The job's log should read
    `logged in as guest, below the read-only this job asked for … but this
    firmware answers a neighbours request to any login, so the read runs at
    guest (task p4.30-180)`, then the `neighbours page at 0` and its reply;
    `jobs` shows **ok** at 2 transactions, the job's page shows the neighbour
    list **and** says the login was answered at guest, and the association moves
    to `needs-regrant` with the fallbacks offered - the same move the
    `unauthorized` made before this, with the reading kept. `sessions` shows one
    **guest** session for that repeater. A *Read the status* of the same
    repeater is the same thing at 3 transactions, with the battery reading and
    the owner info on the page. Then press *Read the telemetry*: it must still
    be **unauthorized**, `{"loggedInAs": "guest", "needed": "read-only"}`, with
    `jobs` showing a **second** login - the guest session is invisible to a job
    of the class that asked for more - and no telemetry request on the air. Put
    the entry back (`setperm <key> 1`, or *Re-grant* from the page) and the next
    read says nothing about a tier at all.
22. A slow repeater, and the reply that came too late (task p1.10-47;
    meshmaster.dk's p4.32-186). The walk a session cannot do: a repeater far
    enough away, or busy enough, that a reply misses its wait. On a repeater with
    an `active` `acl` pair through this exit node, press *Read the neighbours*
    twice within a session's lifetime - the second job finds the session live
    (`session reused (acl, read-only, N s old)` in its log) - and watch for the
    sequence this task exists for:

        neighbours page at 0 try 1: no reply (direct)
        no reply on a reused session: logging in again once
        login (acl) try 1: the late reply to a request this job sent arrived
          instead; dropped, and the login's wait goes on (task p1.10-47)
        login (acl) try 1: logged in as read-only, firmware level 2 (direct)

    The job must end **ok** with the neighbour list, and the association must
    stay `active`. What must **never** appear again is `login (acl) try 1:
    refused` - and with it an `unauthorized` carrying no data, which is what
    parked ten working pairs as *the ACL entry is gone* on 12 November 2026. The
    first line is the one to look for: it is printed to the serial log as well
    (`jobs: job NN: login (acl) try 1: a late reply to one of this job's own
    requests was dropped`), so a repeater that does this often is visible on the
    console without the job's page. A repeater that answers inside its wait every
    time prints none of these lines, and that is the common case: the walk is
    *watch for it*, not *make it happen*. The far end of it, if you can arrange
    a repeater with **no neighbours** at all: its empty page is the one reply
    that would have read as *logged in as guest*, so such a read must still say
    `logged in as read-only` and carry no tier on the result.
23. A grant whose "OK" is lost (task p1.10-48; meshmaster.dk's p4.32-188). On a
    test repeater, take the entry off from its own console (`setperm <this exit
    node's key> 0`) and run the association wizard's grant again from
    meshmaster.dk - or press *Run the grant again* on a pair in
    `needs-regrant`. The ordinary run is unchanged: `cli 'setperm
    <key>... 1' try 1: ok`, `the repeater answered 'OK'`, and `jobs` counts
    **2**. Then make the answer go missing: the surest way on a bench is to put
    the repeater at the far end of the garden, or to run the grant while it is
    busy answering something else, so that the reply misses the 16 s wait. The
    job's log must then read `no answer, and this line cannot be asked twice`,
    `the entry check (blank login): answered at read-only` and `confirmed by the
    login and not by an "OK"`; `jobs` counts **3** and the job is **ok**, with
    `confirmed: true` on its page and no `reply` beside it. `get acl` on the
    repeater's own console must list this board's key at `01`. What must never
    happen again is a grant that ends `unreachable` with the entry set - 20 of
    meshmaster.dk's 52 failed grants of 12 November 2026 were that. Watch the
    **next** job too: the pair's verify must show a login of its own (`login
    (acl) try 1: logged in as read-only`) and never `session reused`.
24. A revoke of a key that is not last in the table (the same task). This one is
    read off the firmware and has never been tried on a board, which is why it
    is here. Grant two keys on a test repeater - this exit node's **first**, then
    any other key (`setperm <some other key> 1`) - and then *Revoke* the pair
    from meshmaster.dk. `applyPermissions` deletes our entry and moves the later
    one down, while `onPeerDataRecv` addresses the reply to the slot the sender
    held before the line ran, so the "OK" should go to the *other* key and never
    come back to us: the log must read `no answer, and this line cannot be asked
    twice`, then `the entry check (blank login): answered at guest` and `this
    exit node's key is off it, so the line ran`, and the job must be **ok** with
    `confirmed: true` at **3** transactions. `get acl` must show our key gone and
    the other key still at its level. Then the same revoke with ours **last** in
    the table: the "OK" should come home as usual, at 2 transactions. If the
    first case answers its line after all, the reading of the pinned source in
    `test/support/FakeMesh.h` is wrong and this walk is how we find out. A
    repeater with a **guest password** set is the third case: there the blank
    login is silence, the job is `unreachable` with `confirmed: false`, and
    meshmaster.dk's p4.32-188 says the command went out and no answer came back,
    which is usual for a key's own removal.
