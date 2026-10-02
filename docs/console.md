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
| `status` | firmware, board and reset reason, the public key, the radio and its frame counters, WiFi, time and where it came from, the server's last answer, the heartbeat, the heap |
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

A refusal is one line starting with `error:` and names what was wrong:
an unknown command or key, a missing value, a value out of range, a scheme
that is not `https://`, a line over 200 characters, a control character.

## Keys

| Key | Value | Fallback while unset |
|---|---|---|
| `wifi.ssid` | 1..32 characters | none: WiFi stays off |
| `wifi.pass` | 8..63 characters, the WPA passphrase; never printed | none |
| `meshmaster.url` | `https://host[:port][/path]`, the scheme required, at most 128 characters | none: no call is made |
| `meshmaster.token` | the agent token from `/exit-nodes/new` on the server: `mma_` and base64url; never printed | none: no call is made |
| `radio.freq` | MHz, 150..2500 | 869.618 |
| `radio.bw` | kHz, 7.8..500 | 62.5 |
| `radio.sf` | 5..12 | 8 |
| `radio.cr` | 5..8 | 8 |
| `radio.tx` | dBm, -9..22 | 22 |
| `log.level` | `error`, `warn`, `info`, `debug` | `info` |
| `log.frames` | `on`, `off`: one log line per frame, both directions. A frame of a job in flight - the login, the request, a CLI line and its reply, a trace - is printed as hex, header, path and payload; a login that carries a password, and the `password` line of a rotation and the reply that echoes it, are printed with their encrypted bodies as one `*` per byte, which is the .NET agent's redaction of `CMD_SEND_LOGIN` and of a sensitive CLI line; an advert and the mesh's own chat are the header line only, never their bytes | `off` |

The radio's fallbacks are the Danish MeshCore channel, the one the field
repeaters run; the ranges are MeshCore's own. A `set` takes effect at once:
`wifi.*` drops the link and rejoins, `meshmaster.*` starts the server loop
again (and forgets a 401), `radio.*` reprograms the SX1262, `log.*` changes
what is printed.

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
[+1s] I meshmaster-agent 0.0.0-dev on MeshCore companion-v1.17.1
[+1s] I board: heltec_v3, Power-on reset
[+2s] I radio: 869.618 MHz, bw 62.5 kHz, sf 8, cr 8, tx 22 dBm
[+2s] I identity: 7f3a...
[+2s] I clock: no time yet
[+2s] I wifi: connecting to Twinoak (attempt 1)
[+5s] I wifi: connected, 192.168.1.20, rssi -61 dBm
[+5s] I clock: sntp started against pool.ntp.org
[+7s] I server: GET https://meshmaster.dk/agent/v1/config (root: ISRG Root X1)
[12:03:12] I clock: 2026-09-28T12:03:12Z from sntp
[12:03:14] I heap: 187432 free after the call (231212 before), min ever 180020
[12:03:14] I server: config ok: agent 17 "Window at Grenaa", transport onboard, budget 2/min gap 20 s, poll 30 s, serverTime 2026-09-28T12:03:14Z
[12:04:19] I observations: 20 posted, 20 accepted
```

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
[13:04:51] I jobs: job 12400: ok after 20 transaction(s)
```

The job's own log, which the server shows on the job's page, says the same in
more words: which login was sent and what it was answered (`login (admin) try
1: logged in as admin, firmware level 2 (direct)`), each command and its
outcome, and for a repeater this exit node holds a grant on the session's last
command (`restore: cli 'setperm <own key> 1': ok`, then `the ACL entry of this
exit node's key is back at read-only, as granted; the session is now read-only
(task 60)`). A password is never an argument of any of those lines.

An `acl`-class `status` is the one read whose transaction count moves with what
the claim asked for: it fetches the firmware version with a second request
(`owner info (firmware)`) only when the claim's payload wants it, which the
server asks for once a day per repeater and not on the other forty-seven reads
(meshmaster.dk's task p4.3-68). So `jobs` shows that read as **2
transaction(s)** the first time after a grant and after each day's refresh, and
**1** the rest of the time; a payload that says nothing at all - an older
server - is two every time, which is what the count was before. Nothing is
logged when the request is skipped, because nothing was sent.

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

Four lines, on for five minutes after boot or after a press of the user button
(PRG, GPIO 0), then off until the next press:

```
wifi 192.168.1.20
12:03:14 UTC (sntp)
hb 12:03 . 17 adverts
id 7f3a9c02  agent
```

Line one is `wifi: no ssid`, `wifi: connecting`, `wifi: retry 20s` or the
address; line two is `no time` until the clock is set; line four is the
identity's first eight hex characters.

Line three is the exit node's state, and when something is missing it says
**which of the two** it is: `no wifi: no server` while the link is down (line
one has the link's own detail), `no server: retry 45s` while the link is up and
meshmaster.dk is not answering, counting down to the next attempt. Otherwise:
until a heartbeat has been answered it is the config call's - `no server set`,
`server: waiting`, `config ok HH:MM:SS`, `http <status>` or `tls/net failed`;
after that it is the heartbeat and the adverts, `hb HH:MM` and a middle dot and
the count. A refused token replaces it with `token revoked/wrong`, and it stays
there until the token is set again.

The radio keeps running and hearing through all of it: the mesh is pumped by
the same loop, the adverts go into the ring, and they are posted when the
server comes back.

## The bench walk

1. Flash (README.md), open the port, see the banner and the prompt.
2. `status`: an identity (64 hex characters), the radio parameters, `wifi off`,
   `no time`, `server not configured`, a heap figure.
3. `set wifi.ssid ...`, `set wifi.pass ...`: within a few seconds
   `wifi: connected` and an address; then `clock: ... from sntp`. Pull the
   network cable from the router's uplink to see the retry lines back off (5 s,
   10 s, 20 s ... up to five minutes) and the link come back by itself.
4. `set meshmaster.url https://meshmaster.dk` and the token from
   `/exit-nodes/new`: `server: GET ...`, then `heap: ... free after the call`
   and `server: config ok: agent <id> "<label>" ...`. A wrong token is
   `server: http 401`, and that **halts the loop**: nothing more is sent until
   `set meshmaster.token <token>` or a reboot, and the OLED says
   `token revoked/wrong`. A URL the chain does not verify for is
   `server: no reply: connection refused (tls -9984: ...)` and the next attempt
   tries the core's certificate bundle. `status` then shows
   `server ... config ok at HH:MM:SS ... root ISRG Root X1` and the heap after
   the TLS call - the number every later task budgets from.
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
12. *Read the configuration* on the repeater's page: 18 transactions and the
    login, 20 in all with the restore, and `log.frames on` shows the last CLI
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
    new password.
18. Staying up (task 06). Pull the router's uplink for ten minutes with a job
    in flight: the log backs off (`server: next attempt in 30 s (the 30 s
    step, jittered)`, then 60, 120 …), the OLED's third line says
    `no server: retry Ns`, `mesh` keeps counting adverts, and `jobs` shows the
    result waiting under `buffer`. Plug it back in: `jobs: N result(s) that
    waited went through` and the job's row on the server fills in, late and
    complete. Pull the *WiFi* instead and the third line says `no wifi: no
    server`. Then `errors` (every counter, and how many failures from a
    restart), `reboots` (`Power-on reset` and an empty log on a board that has
    just been plugged in), and `dump` (the last 4 kB of exactly what the port
    printed - no secret can be in it that was not on the terminal already).
