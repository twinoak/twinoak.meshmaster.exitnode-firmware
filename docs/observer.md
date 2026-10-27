# The observer: an exit node on the community's maps

An *observer* is a MeshCore node that publishes every packet it hears to the
MQTT brokers behind the community's maps and analysers —
[meshview.dk](https://meshview.dk) and [meshmapper.net](https://meshmapper.net)
in Denmark — signed in with a token made from its own key, no shared secret
anywhere. The firmware those brokers were written for is
[`agessaman/MeshCore`, branch `observer-firmware`](https://github.com/agessaman/MeshCore/blob/d664d89df66bdacc1319900a2e2d152fa31b9dc9/MQTT_IMPLEMENTATION.md),
the one [observer.gessaman.com](https://observer.gessaman.com/) flashes. Since
pile 1.7 an exit node speaks the same dialect, from the same console lines, beside
its MeshMaster uplink — and the uplink always comes first.

**It is off by default.** A board does nothing of this until an `mqtt1.preset`
(or `mqtt2.preset`) is set on the console, and it stops the moment both are set
back to `none` or `erase confirm` forgets them. MeshMaster sees none of what is
published; it receives only the observer's **state**, in the heartbeat, and
shows it on an *Observer* card
([below](#what-meshmaster-shows); meshmaster.dk
[D-46](https://github.com/twinoak/meshmaster.dk/blob/master/spec/DECISIONS.md)).
There is no field, no form and no job in MeshMaster for any of this, and there
is not going to be one: the brokers are somebody else's service, the sign-in is
this node's own identity, and an installation that could turn a stranger's
observer on would be a worse thing than the convenience is worth. This page is
where it is written down.

## The lines to type

The Danish observer guide, as the console of this firmware takes it. `set` takes
effect at once — no `reboot` is needed for any line here, though the guide's
`reboot` lines are accepted and do what they say.

```
# Grundlæggende opsætning
set timezone CEST            answered: this firmware keeps UTC and publishes in UTC
set bridge.source rx         answered: an exit node publishes what it receives, forwards nothing

# WiFi opsætning
set wifi.ssid navn_ssid
set wifi.pwd password        wifi.pwd is wifi.pass; echoes as asterisks

# MQTT opsætning, sæt IATA til en af: AAL, AAR, BLL, ODE, CPH eller RNN
set mqtt.iata XXX
set mqtt.owner [PublicKeyPåDinNode]     your own node's key, 64 hex: the token names you as the owner

# MeshView opsætning i slot 1
set mqtt1.preset meshview    the guide's four `custom` lines as one name (below)

# MeshMapper opsætning i slot 2
set mqtt2.preset meshmapper
```

`set mqtt1.preset meshview` is the guide's four lines —
`set mqtt1.preset custom`, `set mqtt1.server wss://mqtt.meshview.dk:443/mqtt`,
`set mqtt1.audience mqtt.meshview.dk`, `set mqtt1.topic meshcore/{iata}/{device}/{type}` —
which are also accepted as typed. `get mqtt.presets` lists the presets; `get mqtt.status`
shows the two slots; `status`'s `observer` line is the summary.
[`console.md`](console.md) has every key.

A broker of your own is a `custom` slot: `set mqttN.server wss://host:port/path`
(or `mqtts://host:port`; `ws://` and `mqtt://` are refused, see *TLS* below),
then `set mqttN.audience host` for the token sign-in, or `set mqttN.username …`
and `set mqttN.password …` for a broker that wants those; `set mqttN.topic …`
for a template other than `meshcore/{iata}/{device}/{type}`.

## What is published, and to whom

To each configured slot's broker, and to nobody else:

| Topic | Message | QoS | Retained |
|---|---|---|---|
| `meshcore/{iata}/{device}/packets` | every packet the radio received — the frame's raw bytes as hex, its hash, type, route, lengths, SNR, RSSI, the rebroadcast score and the path — as the observer firmware's `packets` message, byte for byte | 0 | no |
| `meshcore/{iata}/{device}/status` | `online`: the node's name and key, board, firmware, radio settings, uptime, heap, how many slots are up — at connect and every five minutes (the observer firmware's `mqtt.interval` default) | 1 | where the preset allows: meshmapper yes, meshview no (a custom slot's rule) |
| the same topic, as the last will | `offline`, left by the broker when the connection is lost without a goodbye | 1 | as above |

`{device}` is this node's public key, 64 uppercase hex — the same string
`status` prints and meshmaster.dk shows. Nothing in this firmware decodes a
payload, logs it or shows it; the raw bytes go from the radio into the message
and out, as on every observer on these maps. The messages are
`src/observer/Dialect.h`'s, held to fixtures written from the observer firmware's
source (`test/test_observer`).

**The sign-in** is the observer firmware's JWT: header `{"alg":"Ed25519","typ":"JWT"}`,
claims with this node's public key, the broker's name as audience, `iat`, `exp`
(24 hours, 5 minutes less for slot 2 so the two do not renew together),
`mqtt.owner` and `mqtt.email` when set and `client` — signed with the node's own
Ed25519 key, the signature as 128 hex characters. Username `v1_<KEY>`, password
the token. It is renewed five minutes before it expires, with a new session,
because MQTT takes a password at CONNECT and nowhere after. The private key never
leaves MeshCore's identity; nothing of a token is ever printed.

## TLS

Every connection is verified; none is ever told to accept an unverified chain
(`test/test_tls_policy` greps the tree for the ways of saying so). The roots are
in `src/agent/TlsRoots.h` with their subject, validity and fingerprint beside the
PEM:

- **ISRG Root X2** and **ISRG Root X1** for the two presets. Read off the live
  chains on 27 October 2026: `mqtt.meshview.dk` and `mqtt.meshmapper.net` both
  serve Let's Encrypt's ECDSA chain (leaf ← YE1/YE2 ← Root YE ← ISRG Root X2,
  cross-signed by ISRG Root X1), which verifies against either root alone.
- A **custom** broker is verified against the same two first and, if its chain
  ends elsewhere, once against the ESP32 core's own certificate bundle — the
  Mozilla roots — exactly as the MeshMaster uplink does for meshmaster.dk. A chain
  that verifies against neither is refused with the reason, and the slot backs off.
- `ws://` and `mqtt://` are refused at `set mqttN.server`: the observer firmware
  skips certificate checks on those, and this one has nothing to skip them with.

## What it costs, and the order of priority

**The radio first, the MeshMaster uplink second, the observer last.** A Heltec V3
has 320 KB of RAM and no PSRAM; a TLS connection takes about 40 KB of heap
(the observer firmware's figure for one WSS slot, `MQTT_IMPLEMENTATION.md`),
and so does the uplink's own handshake every time it calls meshmaster.dk. So:

- A slot connects only while **free heap is at least 88 KB and the largest free
  block at least 34 KB** (`src/observer/Connection.h`: its own 40 KB, the
  uplink's 40 KB and 8 KB over; two TLS content buffers of 16 KB). Below that it
  waits (`waiting-for-heap`) and looks again every five seconds.
- Two slots never open at once: the second waits for the first's handshake.
- **When the uplink's call fails for want of memory** while a slot is up — mbedtls
  said an allocation failed, or the largest free block after the failure could
  not hold a TLS buffer — the **highest slot closes and stays down ten minutes**,
  and `get mqtt.status` counts how often (`gave way N time(s)`).
- The client runs on its own task: a publish is an enqueue, and the mesh loop and
  the agent loop never wait on a broker. Between the radio and the client is a
  queue of 2 KB per slot — about three packet messages — oldest dropped and
  counted when it fills; a slot that is not connected drops at once and hoards
  nothing. The brokers see the packets a connected node hears, not a backlog.
- Static RAM: the connections and their queues are about 7.6 KB of the image's
  RAM (52 % of the V3's 320 KB after this task, 49.8 % before); the TLS contexts
  and esp-mqtt's tasks are heap, taken when a slot connects and given back when it
  closes. The figure that matters — free heap with WiFi, the uplink and two WSS
  connections up on a V3 — is the bench's, and `status`'s `heap` line is where to
  read it.

## What the console shows

```
> get mqtt.status
observer  slot 1 meshview connected, slot 2 meshmapper connected; iata AAR, origin "MeshMaster-7F3A"; 412 packet message(s) made, 0 did not fit; 2 of 2 slot(s) connected (get mqtt.status)
slot 1 meshview (wss://mqtt.meshview.dk:443/mqtt): connected since 12:03:20, root ISRG Root X1/X2; 413 published, 0 dropped, 0 queued; 1 connects, 0 refusal(s), 0 error(s), 0 renewal(s), gave way 0 time(s); last error: none
slot 2 meshmapper (wss://mqtt.meshmapper.net:443/mqtt): connected since 12:03:26, root ISRG Root X1/X2; 409 published, 4 dropped, 0 queued; 1 connects, 0 refusal(s), 0 error(s), 0 renewal(s), gave way 0 time(s); last error: none
memory    a slot opens while free heap >= 90112 and the largest block >= 34816 (now 121304 and 65524); the uplink's 0 memory failure(s) so far
```

**Read line by line**, because every number in it answers a question the bench
asks:

| Line | What to read in it |
|---|---|
| `observer  …` | the same line `status` prints: each slot and its state, then the region code, the origin name, how many packet messages this board has **made** (not published — that is per slot), how many did not fit the message buffer, and *N of M slot(s) connected*. When the observer is off it says `off:` and **why** — *no slot configured*, *mqtt.iata is not set*, *no identity* — which is the first line to read when nothing is happening |
| `slot N <preset> (<url>)` | which broker this slot is and how it is reached. `slot 2 none: off (preset is none)` is an empty slot and is not a fault |
| `… : <state> since <time>` | the state word and when it was entered, by this board's clock. The seven words are below; `backing off since …, next attempt in N s` and `waiting for heap since …, looks again in N s` carry the wait as well |
| `root ISRG Root X1/X2` | which CA store verified this connection — the two roots this firmware carries, or `the core's bundle` for a custom broker whose chain ends elsewhere. Never *unverified*: there is no such state |
| `N published, N dropped, N queued` | **published** is what the broker took, packets and status alike; **dropped** is packet messages this slot threw away — its 2 KB queue was full, or it was not connected; **queued** is what is waiting in that queue right now. `dropped` climbing while `published` does not is a slot that is down, not a slot that is slow |
| `N connects, N refusal(s), N error(s), N renewal(s), gave way N time(s)` | sessions the broker accepted; CONNECTs it refused; transport failures and lost connections; tokens made again before they expired (about one a day per slot); and how often this slot closed so the MeshMaster uplink could have the heap. **`gave way` above 0 on a healthy board is the reserve being too tight** — the constants are in `src/observer/Connection.h` |
| `last error: <phrase>` | the last refusal or failure, one phrase and its numbers (`bad username or password (CONNACK 4)`, `tls handshake failed (tls -0x7780, errno 104)`), or `none`. This is the line to copy verbatim into a bench note; it is also the one the heartbeat carries, so MeshMaster's card shows the same words |
| `memory  …` | the two thresholds a slot opens above, and the heap as it is **now** — free, and the largest block — followed by how many times the uplink's own call has failed for want of memory. If `largest block` is under the second figure, the second slot is waiting and the line above says so |

A slot's state is one of `off`, `waiting-for-wifi`, `waiting-for-clock` (a token
carries an issue time; nothing is sent before SNTP or the server set the clock),
`waiting-for-heap`, `connecting`, `connected`, `backing-off` (a refused sign-in,
a failed handshake or a lost connection: 5 s, doubled each time to ten minutes,
with the reason). **Those seven words are a contract**, not a display string:
the heartbeat carries each slot's state and meshmaster.dk shows it, so renaming
one here changes what the Observer card says
(`test/test_connection` holds them).

The OLED's third line gains `MQTT 2/2` against its right edge while a slot is
configured — `HB 12:03:14  MQTT 2/2` — and says nothing about MQTT when none is.

The log says connects, refusals and drops as counts and reasons and nothing else:

```
[12:03:14] I observer: slot 1 meshview: wss://mqtt.meshview.dk:443/mqtt, token, topics meshcore/AAR/7F3A…/packets
[12:03:19] I observer: slot 1 meshview: connecting to wss://mqtt.meshview.dk:443/mqtt with token, root ISRG Root X1/X2
[12:03:20] I observer: slot 1 meshview: connected (root ISRG Root X1/X2; 1 connects)
[12:03:25] I observer: slot 2 meshmapper: connecting to wss://mqtt.meshmapper.net:443/mqtt with token, root ISRG Root X1/X2
[12:03:26] I observer: slot 2 meshmapper: connected (root ISRG Root X1/X2; 1 connects)
[13:41:02] W observer: slot 2 meshmapper: connection lost; next attempt in 5 s (0 refusal(s), 1 error(s))
[13:41:08] I observer: slot 2 meshmapper: connected (root ISRG Root X1/X2; 2 connects)
```

Never a packet, a token or a password: `test/test_connection` runs the whole
state machine against a fake broker and reads the log ring back to hold it.

## What MeshMaster shows

The heartbeat carries the observer's **state** and nothing else, and only while
the observer is on: a board with both slots `none` sends a heartbeat with no
observer object in it at all, byte for byte the heartbeat it sent before this
existed. When it is on, the object is the region code and one entry per slot —
the preset, the broker, the state word, when the slot entered that state, how
many messages were published, how many were dropped, and the last error. That is
the whole of it: **not a packet, not a byte of one, not the radio's numbers for
one, and not a count of them per key.** What a slot sent is between this node
and that broker.

The exit node's page, `/exit-nodes/{id}`, then draws an **Observer** card from
the newest object it has received:

- A lead line, *It publishes every packet it hears to its community brokers, set
  up on its own console* — or, when a slot is down, *…and one of its brokers is
  not taking it now.*
- One row per slot: **slot 1 · meshview — connected since 4 minutes ago**, the
  broker under it with *1420 published, 3 dropped*, and the slot's last error in
  amber when there is one. A slot that is off reads *no broker*.
- *Reported <when>*, and a Help panel saying what **dropped** and **published**
  count and that MeshMaster relays and stores no packet.
- The exit nodes list gains an **Observer** column, *connected over configured* —
  `2/2`, `0/1` — and it is **empty** for an exit node that is not one, because
  most are not and a column of dashes is noise.

Two things that card does deliberately. A state word MeshMaster has never heard
of is **kept and shown as it came** rather than refused, so this firmware can
gain a state before the server has heard of it; only `connected` is ever counted
as connected. And an observer object with a field over the server's bound is
**refused by name while the rest of the heartbeat is still taken** — a broker's
name being too long must never take an exit node off the air. The bounds are
`preset` 32, `host` 64, `state` 32 and `lastError` 120 characters, and six
slots; everything this firmware can produce is inside them.

The card **disappears a day** after the observer is switched off — a day of
heartbeats carrying none, which is long enough that a reboot or a reflash does
not empty it.

## Privacy, in one place

The observer forwards the raw bytes the radio received — as every observer on
these maps does — to the brokers the node's owner configured, and to nobody
else. Nothing in this firmware decodes them, logs them or shows them; MeshMaster
receives only the observer's state (meshmaster.dk D-46). `mqtt.owner` and
`mqtt.email` go into the token and nowhere else; the brokers use them to show
you your own node.
