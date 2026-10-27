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
published; it receives only the observer's state, from task 28 on
(meshmaster.dk D-46).

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

A slot's state is one of `waiting-for-wifi`, `waiting-for-clock` (a token needs
the time; nothing is sent before SNTP or the server set it), `waiting-for-heap`,
`connecting`, `connected`, `backing-off` (a refused sign-in, a failed handshake
or a lost connection: 5 s, doubled each time to ten minutes, with the reason).
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

## Privacy, in one place

The observer forwards the raw bytes the radio received — as every observer on
these maps does — to the brokers the node's owner configured, and to nobody
else. Nothing in this firmware decodes them, logs them or shows them; MeshMaster
receives only the observer's state (meshmaster.dk D-46). `mqtt.owner` and
`mqtt.email` go into the token and nowhere else; the brokers use them to show
you your own node.
