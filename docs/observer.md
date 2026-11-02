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

**On a Heltec V3, set MeshView only (slot 1) and leave MeshMapper to the V4s.**
Measured on 28 October with `v0.4.0`: a V3 has about 136 KB free with no broker,
one broker connection costs about 63 KB, and the MeshMaster uplink's call dips
about 80 KB — so with one broker up, a call took the board to 3 KB free, and a
second broker never gets memory to connect (it stays *waiting-for-heap*). Since
task p1.8-31 **a V3 has one slot and a V4 two** (`src/observer/Board.h`): on a V3
`set mqtt2.preset meshmapper` is refused with
`error: mqtt2.preset: this board has memory for one broker; a V4 carries two`, a
slot 2 an older image stored is shown off with the same words, and `get
mqtt.status` shows one slot. The guide's MeshMapper line is therefore the one
line that does not take on a V3; on a V4 it does ([*What it costs*](#what-it-costs-and-the-order-of-priority)).

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
| `meshcore/{iata}/{device}/neighbors` | the repeaters this node hears at **zero hops**, each with its SNR, when it was last heard and the flood regions it answers a regions request with — MeshView's *Direct neighbours* — once every `mqtt.neighbors.interval` hours (24) while `mqtt.neighbors` is on, and after `discover.scopes` ([*Neighbours*](#neighbours), task p1.8-33) | 0 | as the status |

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

## Neighbours

MeshView's page for a node has a **Direct neighbours** section: the repeaters
the node hears at zero hops, each with its SNR and the flood regions it is
configured with. It is drawn from the `neighbors` message the observer firmware
publishes once a day, and since task p1.8-33 an exit node publishes the same
one, built the same way:

- **The table.** A repeater's advert heard with **no hops** — the frame came
  from its own radio, not through another repeater — puts it in a table of
  twenty: its key, the SNR and RSSI the radio measured, when it was heard. A
  twenty-first replaces the one heard longest ago. The table is a fixed array
  (`src/observer/Neighbours.h`); nothing is allocated for it. Once per cycle it
  is refreshed by a **node discovery** — one `CTL_TYPE_NODE_DISCOVER_REQ` for
  repeaters, zero hop, the same request the repeater firmware's own
  `discover.neighbors` sends — and sixty seconds of taking the answers. Since
  MeshCore 1.18 (`f19733e4`, #3538) a repeater whose `flood.advert.interval` is
  0 — with its local `advert.interval` 0 too, which the repeater's own CLI makes
  it on the first `set` — is a *hidden node* that answers no node discovery, so
  the discovery misses it and only the adverts it sends, if any, put it in the
  table.
- **The scopes.** Then one anonymous **regions** request per neighbour, newest
  first: the login's datagram with two other bytes, `{tag}{0x01}{0x00}`, sent
  **direct on an empty path**, because a repeater answers it only when the
  request was direct, and at most four times in three minutes. The reply is the
  comma-separated names of the regions the repeater floods (`exportNamesTo` with
  `REGION_DENY_FLOOD`), which is what its own console's `region` lists with `F`;
  bounded to 256 characters, cut at valid UTF-8. A neighbour is `responded`,
  `timeout` or `send_failed`, with the observer firmware's own timeout: the
  repeater's response delay, its CAD window, one retry, and the air time of a
  full packet twice, at this radio's settings — a few seconds at SF 8.
- **The exit node's rules around it.** Nothing of the sweep goes on the air
  while a job is open or in flight; a request waits for the **airtime gate the
  jobs share** (`JobRunner::gate`) and is counted on it, so a sweep of twenty
  neighbours at the default budget of two a minute takes about ten minutes and
  a job claimed in the middle of it waits for the one query that is on the air
  and nothing more — a query is synchronous and bounded by the timeout. The
  frames of the sweep are decoded exactly as a job's are: the peer is set for
  the query and forgotten after it, and every other frame is released as it
  always was. The discovery's answers are the one new thing let through, for
  the sixty seconds the window is open, and they are plaintext.
- **The report.** `timestamp`, `origin`, `origin_id`, `total_neighbors`,
  `queried_neighbors`, `truncated`, `self: { scopes: "", default_scope: <flood.scope> }`
  — an exit node forwards no flood, so it has no regions of its own, which is
  what a repeater with none says; its `default_scope` is `flood.scope`, or `*`
  — and one entry per neighbour: `pubkey` (64 uppercase hex), `snr` (dB),
  `heard_secs_ago`, `scopes`, `status`, sorted by recency, then SNR, then key,
  at most twenty and at most 10 KB, byte for byte the observer firmware's field
  order (`test/test_observer`, `fixtures/neighbours.fixture`). It is built once
  into a buffer of its own (static, like the queues) and handed to each
  connected slot **one at a time** — the next only when the previous one's
  outbox has drained, because the client copies a message into its outbox and
  this one is ten kilobytes — at QoS 0, retained where the preset allows (the
  status topic's rule). It goes out only when the gate says the heap can take
  the outbox's copy on top of the uplink's next call, the floor and the margin;
  otherwise that cycle is **skipped** and `get mqtt.status` says so. It is never
  published from inside a MeshMaster call: the uplink's calls are synchronous,
  and the observer's loop never runs during one. CoreScope's ingestor (the
  software behind MeshView) reads `self.scopes` as the node's own, a neighbour's
  scopes only when its status is `responded`, drops a key that is not 64 hex
  and a timestamp more than five minutes ahead of its clock — all of which this
  report satisfies by construction.
- **When.** Every `mqtt.neighbors.interval` hours (12..336, default 24) while
  `mqtt.neighbors` is `on` — the default here; the observer firmware's is off,
  but an exit node's observer is already opt-in slot by slot, and the report is
  why one is on — and only when a broker slot is connected, because the report
  has nowhere else to go. The first cycle runs five minutes after the sweep is
  switched on, so the table has heard a round of adverts and the slots have had
  their handshakes. `discover.scopes` runs one cycle now, whatever the setting
  and the schedule say, and prints the table as it stands:

  ```
  > discover.scopes
  neighbours 2 heard at zero hops (table of 20); nbr: next in 23h 12m, last ok 47m 50s ago, 2 neighbours, 2 responded
    key           snr     rssi  heard       last cycle
    8F1A2B3C4D5E   12.25   -71      38 s ago  responded: dk,dk-aarhus
    C0FFEE0123AB    4.50   -98    1402 s ago  responded: *
  cycle     starts now: 60 s of node discovery, then one regions request per neighbour on the airtime budget; the report goes to every connected slot (get mqtt.status: nbr)
  ```

  The cycle runs in the background of the main loop; the log says how it went
  (`neighbours: node discovery sent …`, `neighbours: regions request to 8F1A: responded, scopes dk,dk-aarhus`,
  `neighbours: report built (812 bytes, 2 neighbours, 2 responded), going to 1 slot(s)`,
  `observer: slot 1 meshview: the neighbours report (812 bytes) is handed to the broker`),
  and `get mqtt.status`'s `nbr:` line has the schedule and the last outcome.
- **What it costs.** On the air: one zero-hop control packet and one anonymous
  request per neighbour per cycle — twenty-one transactions a day at most, on
  the jobs' own budget — and the repeaters' replies. In heap: nothing for the
  table or the report; the outbox's copy of the report, up to 10 KB per slot
  while the client sends it, which is what the gate checks before it is offered
  (`get mqtt.status`'s `memory` line has the figures; the row *the neighbour
  report's peak* in [*What it costs*](#what-it-costs-and-the-order-of-priority)).
  In static RAM: the table (about 1 KB), the sweep's snapshot (about 6 KB, the
  twenty entries with their scopes) and the report buffer (10 KB).
- **Which repeaters answer.** Any stock MeshCore repeater at the pin this
  firmware builds against: `handleAnonRegionsReq` is in its `MyMesh.cpp`,
  answered only to a direct request and limited by `anon_limiter` to four in
  three minutes. A repeater with no region map answers with nothing — an empty
  `scopes`, status `responded` — which is what MeshView shows as no scopes. A
  companion or a room server does not answer, and is not in the table either:
  only a repeater's advert enters it.
- **Two things that are not here**, by the task: RSSI in the report (the
  observer firmware's `feat/neighbors-rssi` is not in the released dialect; the
  console's table prints it) and a report on a slot whose custom topic has no
  `{type}` — it would land on the packets topic, so that slot gets none.

The status message, which MeshView showed with *—* for battery, noise floor and
TX/RX, now carries `noise_floor` from the radio, `tx_air_secs` and `rx_air_secs`
from the dispatcher's air-time counters, and `battery_mv` from the board's ADC
when it reads a volt or more (a board on USB with nothing on that pin reads
noise, which is left out).

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

**The radio first, the MeshMaster uplink second, the observer last.** The
figures are this firmware's own, measured on the bench, not the observer
firmware's (`src/observer/Board.h`, where each one says where it comes from):

| | Heltec V3 | Heltec V4 |
|---|---|---|
| internal heap free, idle, no slot | 136 580 (28 Oct 2026, `v0.4.0`, after a MeshMaster call) | not measured; the same core, so about the same |
| one slot costs | **63 564** internal (measured 28 Oct 2026: 136 580 → 73 016 with MeshView up) | **30 796** internal + **40 960** PSRAM (derived: the V3's figure less the two 16 KB TLS content buffers, which go to PSRAM; not measured on a V4) |
| the uplink's call dips | **81 116** (measured 28 Oct 2026, a handshake per call: 136 580 → 55 464); *the board replaces this with what its own last call dipped, once it has made one* | 48 348 (derived the same way; replaced the same way) |
| the floor a call must stay above | 16 384 (not measured: what WiFi, lwIP and the HTTP client need in flight, twice over) | the same |
| the gate's margin | 4 096 | the same |
| **slots the gate will open** | **1** | **2** |
| where the TLS buffers are | internal heap | PSRAM (every mbedtls allocation of 4 KB and over, through a hook on its allocator - `src/platform/TlsHeap.cpp`) |
| **expected after task p1.9-36** (the ESP-IDF built with dynamic TLS buffers; not measured - task 37 writes the bench's figures) | a connection *between* records holds neither 16 KB content buffer: about 34 KB of a slot's 63 564 and of the uplink's steady cost is not held, and the ~8 KB of certificates is freed after the handshake. A record in flight borrows its buffer back - 16 KB in, 4 KB out - so the *dip* of a call or a publish stays near the old figure while the *rest* falls by ~40 KB per connection. One slot beside the uplink should then fit with room, and the question for the bench is the handshake beside an open slot | the same ~40 KB per connection, from PSRAM rather than internal heap, since the buffers are PSRAM's there; the internal share of a slot (30 796) loses only the certificates |

So:

- A slot connects only while **free internal heap is at least one slot + the
  uplink's dip + the floor + the margin**, and the largest free block holds one
  TLS content buffer (17 KB) on a V3 or the handshake's largest internal
  allocation (4 KB) on a V4, whose PSRAM must in turn hold the slot's 40 KB and a
  17 KB block. With the figures above and no call measured yet that is 165 KB on
  a V3 - more than the board has, on purpose: the dip of a handshake per call is
  the conservative figure, and the board swaps in **what its own last call
  dipped** (`status`'s `uplink` line, *lowest during*) as soon as it has one. A
  call over the kept connection dips a few KB; the gate then asks for about
  63.5 + a few + 16 + 4 KB, which a V3 at rest has. Below the figure a slot waits
  (`waiting-for-heap`) and looks again every five seconds; `get mqtt.status`'s
  `memory` line prints the whole sum and which dip is in force.
- Two slots never open at once: the second waits for the first's handshake.
- **The gate keeps watching.** Every MeshMaster call's lowest free heap is read
  (task p1.8-30 measures it); a call that **dipped under the floor** while a slot
  was up - it went through, but nearly did not - closes the **highest slot for
  ten minutes**, exactly as a call that **failed for want of memory** does
  (mbedtls said an allocation failed, or the largest free block after the failure
  could not hold a TLS buffer). `get mqtt.status` counts both as `gave way N
  time(s)`, and the slot's `last error` says which it was and the figures. A
  handshake call on a V3 with a slot up is the case that trips it: the kept
  connection makes those rare (the server closes an idle one after about two
  minutes), and the bench walk of pile 1.8 is where `min ever` says whether one
  slot on a V3 truly fits.
- **A board has the slots its memory allows**: one on a V3, two on a V4. On a V3
  `set mqtt2.preset` takes `none` and nothing else, with the reason; a slot 2
  stored by `v0.4.0` is off with the same reason. Since task p1.9-36 the ESP-IDF
  is built from source with mbedtls's buffers allocated per record rather than
  per connection (README, *Building it*); the row marked **expected** above says
  what that should do to the figures, and the slot rule moves only when task 37
  has measured it.
- The client runs on its own task: a publish is an enqueue, and the mesh loop and
  the agent loop never wait on a broker. Its sizes are set with their arithmetic
  in `src/net/MqttClient.cpp`: a 512-byte receive buffer (nothing is subscribed),
  a 1 792-byte send buffer (the CONNECT with its 768-byte token and its 768-byte
  will, whole - larger than any packet message) and a 6 144-byte task stack
  (esp-mqtt's default, with the lowest it ever got printed by `get mqtt.status`
  so the bench can say whether a smaller one would do). Between the radio and the
  client is a queue of 2 KB per slot — about three packet messages — oldest
  dropped and counted when it fills; a slot that is not connected drops at once
  and hoards nothing. The brokers see the packets a connected node hears, not a
  backlog. The queue is static RAM, not heap, so it stays: shrinking it would
  free nothing the gate counts.
- Static RAM: the connections and their queues are about 7.6 KB of the image's
  RAM (52.7 % of the V3's 320 KB after task 31); the TLS contexts and esp-mqtt's
  tasks are heap, taken when a slot connects and given back when it closes.
  `status`'s `heap` line (and on a V4 its `psram` figures) is where to read it.
- **The neighbour report's peak** (task p1.8-33): the report is built into a
  static 10 KB buffer and costs the heap nothing until it is handed to a
  client, which copies it into its outbox — up to **10 KB of internal heap per
  slot** while the client sends it, one slot at a time. Before it is offered,
  the gate asks for *free heap ≥ the report + the uplink's dip + the floor + the
  margin* and a block the copy fits in; below that the cycle is skipped and
  `nbr:` says `skipped (heap)`. Not measured on a board yet: the bench walk of
  pile 1.8 reads `status`'s `min ever` after a cycle.

## What the console shows

```
> get mqtt.status
observer  slot 1 meshview connected, slot 2 meshmapper connected; iata AAR, origin "MeshMaster-7F3A"; 412 packet message(s) made, 0 did not fit; 2 of 2 slot(s) connected (get mqtt.status)
slot 1 meshview (wss://mqtt.meshview.dk:443/mqtt): connected since 12:03:20, root ISRG Root X1/X2; 413 published, 0 dropped, 0 queued; 1 connects, 0 refusal(s), 0 error(s), 0 renewal(s), gave way 0 time(s); last error: none
slot 2 meshmapper (wss://mqtt.meshmapper.net:443/mqtt): connected since 12:03:26, root ISRG Root X1/X2; 409 published, 4 dropped, 0 queued; 1 connects, 0 refusal(s), 0 error(s), 0 renewal(s), gave way 0 time(s); last error: none
memory    heltec_v4 opens 2 slot(s) while free heap >= 56856 (one slot 30796 + the uplink's dip 5580 measured + floor 16384 + margin 4096) and the largest block >= 4096 (now 121304 and 65524); and psram >= 40960 with a 17408 block (now 1894320 and 1835008): the TLS buffers, its last call's lowest 115724; a call under the floor closes the highest slot for 10 min
nbr: next in 22h 41m, last ok 1h 18m ago, 7 neighbours, 5 responded
uplink    0 memory failure(s) so far, 41 call(s); the last 121304 free before, lowest 115724 during, 121180 after
tls       buffers of 4096 bytes and over in psram: 9 allocation(s) routed there, 0 fell back to internal heap
stack     slot 1 task: 6144 bytes, the least ever free 2328
stack     slot 2 task: 6144 bytes, the least ever free 2312
```

That is a V4. A V3 prints one slot, `heltec_v3 opens 1 slot(s) while free heap >=
89624 (one slot 63564 + the uplink's dip 5580 measured + floor 16384 + margin
4096) and the largest block >= 17408 (now …)`, no `psram` figures, `tls buffers in
internal heap (this board has no psram)` and one `stack` line; and if an older
image left a slot 2 in it, `slot 2 meshmapper: off (this board has memory for
one broker; a V4 carries two)`.

**Read line by line**, because every number in it answers a question the bench
asks:

| Line | What to read in it |
|---|---|
| `observer  …` | the same line `status` prints: each slot and its state, then the region code, the origin name, how many packet messages this board has **made** (not published — that is per slot), how many did not fit the message buffer, and *N of M slot(s) connected*. When the observer is off it says `off:` and **why** — *no slot configured*, *mqtt.iata is not set*, *no identity* — which is the first line to read when nothing is happening |
| `slot N <preset> (<url>)` | which broker this slot is and how it is reached. `slot 2 none: off (preset is none)` is an empty slot and is not a fault |
| `… : <state> since <time>` | the state word and when it was entered, by this board's clock. The seven words are below; `backing off since …, next attempt in N s` and `waiting for heap since …, looks again in N s` carry the wait as well |
| `root ISRG Root X1/X2` | which CA store verified this connection — the two roots this firmware carries, or `the core's bundle` for a custom broker whose chain ends elsewhere. Never *unverified*: there is no such state |
| `N published, N dropped, N queued` | **published** is what the broker took, packets and status alike; **dropped** is packet messages this slot threw away — its 2 KB queue was full, or it was not connected; **queued** is what is waiting in that queue right now. `dropped` climbing while `published` does not is a slot that is down, not a slot that is slow |
| `N connects, N refusal(s), N error(s), N renewal(s), gave way N time(s)` | sessions the broker accepted; CONNECTs it refused; transport failures and lost connections; tokens made again before they expired (about one a day per slot); and how often this slot closed so the MeshMaster uplink could have the heap — after a call of the uplink's that failed for want of memory, or one that went through but dipped under the floor. **`gave way` climbing on a healthy board is the board being too small for the slot and a handshake at once**: the slot's `last error` says which call did it and the figures, and `src/observer/Board.h` and `Connection.h` (the floor) are where the constants are |
| `last error: <phrase>` | the last refusal or failure, one phrase and its numbers (`bad username or password (CONNACK 4)`, `tls handshake failed (tls -0x7780, errno 104)`), or `none`. This is the line to copy verbatim into a bench note; it is also the one the heartbeat carries, so MeshMaster's card shows the same words |
| `memory  …` | the board, how many slots it opens, and the sum a slot opens above — one slot, the uplink's dip (`measured` from this board's last call, or `assumed` from the table above until it has made one), the floor, the margin — then the block the largest allocation needs, the heap as it is **now** (free, and the largest block), on a V4 the same for PSRAM, the last call's lowest free heap, and the rule that closes a slot. If `now` is under the sum, a slot is waiting and its line says so |
| `nbr: …` | the neighbour report ([*Neighbours*](#neighbours)): when the next cycle runs (`next in 22h 41m`; `running (discovery, 42 s left)` or `running (query 3 of 7)` while one does; `due, waiting for a slot` when it is time but no broker is connected; `off (mqtt.neighbors off)`), how the last report went and how long ago (`ok`, `skipped (heap)`, `failed` — no slot took it — `no slot`, or `none yet`), how many repeaters are in the table now and how many of the last cycle's answered. `discover.scopes` prints the table itself |
| `uplink  …` | how often the uplink's call has failed for want of memory, how many calls it has made, and what the last one cost (`status`'s `uplink` line has the same, with the kept connection) |
| `tls  …` | where the TLS buffers are: PSRAM on a V4 (with how many allocations went there and how many had to fall back to internal heap - a V4 with fallbacks is a V4 whose PSRAM is full, which should not happen), the internal heap on a V3 |
| `stack  …` | each slot's task stack and the least of it that was ever free since the slot opened, read on the task itself; the bench's figure for whether the stack could be smaller |

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
