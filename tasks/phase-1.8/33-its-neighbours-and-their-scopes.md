# 33 — Its neighbours and their scopes

**Depends on:** 31 (the heap gate that every publish goes through), 32 (`self.default_scope` is its `flood.scope`).
**Spec:** the observer firmware this one speaks the dialect of, agessaman/MeshCore branch `observer-firmware` (`d664d89`): `MQTT_IMPLEMENTATION.md` (*Neighbors*: the topic, the JSON, the 20-entry and 10 KB caps, `mqtt.neighbors`, `mqtt.neighbors.interval` 12–336 h default 24, `discover.scopes`, the `nbr:` line of `mqtt.status`), `examples/simple_repeater/MyMesh.cpp` (`onAdvertRecv`: a zero-hop repeater advert is a neighbour; `sendNodeDiscoverReq` ~L1000: `CTL_TYPE_NODE_DISCOVER_REQ` for repeaters, zero hop, 60 s; `sendAnonRegionsReq` ~L1958: `{tag(4)}{0x01}{0x00}` sent **direct** on an empty path; `neighborDiscoverQueryTimeoutMs`; `completeNeighborDiscoverEntry`: `responded` / `timeout` / `send_failed`), `src/helpers/bridges/MQTTBridge.cpp` (`publishNeighbors`: every connected slot, QoS 0, retained where the preset allows); the consumer, Kpa-clawbot/CoreScope (`cmd/ingestor/main.go` `handleNeighborsReport` ~L1990, `neighbors_guard.go`: `self.scopes` always trusted, a neighbour's scopes only on `responded`, pubkeys 64 hex, scopes ≤ 256 characters, a timestamp more than 5 minutes ahead dropped); the responder at the pin of our repeaters (`simple_repeater/MyMesh.cpp` `handleAnonRegionsReq`, answered only to a **direct** request, `anon_limiter` 4 per 3 minutes); OffbandMesh/meshcore-firmware issue #1387 (the same feature mapped onto a companion-based observer). Ours: `src/mesh/AgentMesh.{h,cpp}` (`onAdvertRecv` ~L177, `onRecvPacket` releases what is neither an advert nor the job's own frame ~L170, `setDirectPath`, `awaitReply`, `jobInFlight`), `lib/agent-core/include/agent/Airtime.h`, `src/observer/` (`Dialect`, `Topics`, `Connection`'s gate after 31, `Observer`), `src/settings/Keys.cpp`, `docs/observer.md`.
**Size:** one session, long.
**Model:** claude-fable-5-1

## Goal

Thomas, 31 October: MeshView's page for his Gessaman observer has a **Direct
neighbours** section, listing the repeaters it hears at zero hops, each with
its SNR and the scopes it is configured with. The page for FD9B says *Last
/neighbors Report: never*. The Gessaman observer publishes
`meshcore/{iata}/{device}/neighbors` once a day, and we publish nothing there.
It also says *—* for battery, noise floor and TX/RX: our `Dialect` has
`battery_mv`, `noise_floor`, `tx_air_secs` and `rx_air_secs`, but nothing in
`src/` ever sets them.

Most of what the report needs is already here. `onAdvertRecv` sees every
advert with its hop count, SNR and RSSI. The login is an anonymous datagram
sent direct with an awaited reply, and a regions request is the same with two
other bytes. The observer has the dialect, the slots, the retain rule per
preset and a JSON writer. Our repeaters run stock MeshCore at a pin that
answers the request.

## Deliverables

1. **The neighbour table.** Up to **20** repeaters heard at **zero hops**
   (advert type repeater, path length 0): key, SNR (quarter dB), RSSI, when
   last heard (millis). It is filled from adverts, and once per cycle from a
   **60-second node discovery** (`CTL_TYPE_NODE_DISCOVER_REQ` for repeaters,
   zero hop, its responses taken in `onControlDataRecv`). When full, the
   oldest entry is replaced. No heap allocation: a fixed array.
2. **The scope query.** For each neighbour, newest first then by SNR, one
   anonymous **regions** request (`ANON_REQ`, `{tag}{0x01}{0x00}`), sent
   **direct on an empty path**: a repeater answers it only when it is direct.
   The reply's text after the 8-byte prefix is the scope list, bounded to 256
   characters and cut at valid UTF-8. The timeout follows Gessaman's
   `neighborDiscoverQueryTimeoutMs`. The status is `responded`, `timeout` or
   `send_failed`. The sweep runs **only while no job is in flight**. Each
   request is a transaction on the exit node's own airtime budget
   (`Airtime.h`), and a claimed job waits for at most the one query on the
   air. The frames of the sweep are decoded the way a job's are (the peer is
   set for the query), and `onRecvPacket`'s release of everything else
   stays as it is.
3. **The report.** `meshcore/{iata}/{device}/neighbors` in Gessaman's JSON:
   `timestamp`, `origin`, `origin_id`, `total_neighbors`, `queried_neighbors`,
   `truncated`, `self: { scopes: "", default_scope: <flood.scope, or "*"> }`.
   The exit node forwards no flood, so it has no scopes of its own, which is
   what a repeater with no regions says. Each neighbour has `pubkey` (64 hex,
   upper case), `snr` (dB), `heard_secs_ago`, `scopes` and `status`. Entries
   are sorted by recency, then SNR, then key, and capped at 20 entries and
   10 KB. It is built once into one buffer and published to each connected
   slot **one at a time**, QoS 0, retained where the preset allows (the
   status topic's rule). It goes out only when 31's gate says the heap allows
   it; otherwise that cycle is skipped and says so. It is never published
   from inside a MeshMaster call.
4. **Settings and console**, with Gessaman's names: `mqtt.neighbors on|off`
   (default **on**: Gessaman's default is off, but an exit node's observer is
   already opt-in, slot by slot, and the report is why one is on);
   `mqtt.neighbors.interval` 12–336 hours (default 24). `discover.scopes` runs
   one cycle now and prints the table. `mqtt.status` gets
   `nbr: next <in>, last <ok|skipped (heap)|failed> <ago>, <n> neighbours, <m> responded`.
5. **The status fields MeshView shows as *—*.** `noise_floor` from the radio,
   `tx_air_secs` and `rx_air_secs` from the dispatcher's air-time counters, and
   `battery_mv` from the board where it measures one (omitted where it does
   not), all through the `statIfKnown` path that is already there.
6. **Words:** `docs/observer.md` (*Neighbours*: what is sent, how often, what it
   costs on the air and in heap, which repeaters answer), the Danish guide lines
   in the release notes (`set mqtt.neighbors on` is the default; `discover.scopes`
   to try it), and the *What fits* table of 31 with the report's peak.

## Acceptance criteria

- [ ] Native tests: a zero-hop repeater advert enters the table and a one-hop one does not; a chat node's does not; the 21st replaces the oldest. A discovery response enters it. The regions request's plaintext is `{tag}{0x01}{0x00}`, sent direct with path length 0. A reply's names are bounded and valid UTF-8, and a silent neighbour is `timeout`.
- [ ] The report of a fixed table matches a golden JSON byte for byte (Gessaman's field order), and a report with 20 long-scoped neighbours stays under 10 KB with `truncated` set correctly. CoreScope's guard would accept it: upper-case 64-hex keys, scopes ≤ 256, and a timestamp from the board's clock.
- [ ] No sweep frame goes out while a job is in flight. The report is not published when the gate says no, and `mqtt.status` says *skipped (heap)* (tests with the fake mesh and the fake gate).
- [ ] On the bench (Thomas): FD9B's MeshView page shows *Direct neighbours* with TwinOak Risskov's scopes, and Battery, Noise floor and TX/RX are filled; on a V3 with one slot, `status`'s `min ever` after a day stays above 31's floor.
- [ ] `pio run -e heltec_v3 -e heltec_v4` and the native tests green, CI green.

## Out of scope

Sending the table to MeshMaster (the heartbeat contract, a meshmaster.dk task).
RSSI in the report (Gessaman's `feat/neighbors-rssi` is not in the released
dialect). A neighbour report on a slot whose broker has no `{type}` in its
topic.
