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
| `server` | the exit node's loop: the last heartbeat and the next, the last observation batch, and the four counters the heartbeat itself carries |
| `wifi` | a blocking scan (a few seconds): every network with channel, RSSI and security, then the link's state |
| `reboot` | restart |
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
| `log.frames` | `on`, `off`: one log line per received frame - payload type, route, hops, length, SNR, RSSI - and never a payload byte | `off` |

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

What it never prints: the passphrase, the token (not in a reply, not in a log
line, not in `status`), and anything the mesh says - frames are counted by type
and dropped, and `log.frames on` prints a header line per frame with no payload
byte in it.

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

Line three is the exit node's state. Until a heartbeat has been answered it is
the config call's - `no server set`, `server: waiting`, `config ok HH:MM:SS`,
`http <status>` or `tls/net failed`; after that it is the heartbeat and the
adverts, `hb HH:MM` and a middle dot and the count. A refused token replaces it
with `token revoked/wrong`, and it stays there until the token is set again.

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
7. The air: `mesh` after some minutes at a window shows adverts heard and the
   last five with their names; `observations: N posted` follows in the log, and
   the keys turn up under **Unknown nodes** on the server. `status` still shows
   **`tx 0`** - this firmware sends nothing on the radio.
8. Leave it: `status` after an hour shows frames received and `tx 0`, and the
   heap figure has not moved.
