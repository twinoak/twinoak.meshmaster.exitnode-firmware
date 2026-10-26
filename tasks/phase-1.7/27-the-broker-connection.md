# 27 — The broker connection

**Depends on:** 26.
**Spec:** task 26's `src/observer/` and its tests; the observer firmware (`agessaman/MeshCore`, branch `observer-firmware`): `MQTT_IMPLEMENTATION.md` (retain, QoS, keepalive, reconnect, *about 40 KB of internal heap per TLS/WSS connection*, *at most two active TLS slots without PSRAM*); this repository's `src/agent/AgentClient.cpp` (the HTTPS uplink to meshmaster.dk, `WiFiClientSecure` with the CA bundle), `src/agent/TlsRoots.h` and `test/test_tls_policy/`, `src/net/WifiLink.cpp`, `src/net/Clock.cpp`, `src/Health.cpp` (the heap figures and the reboot guard), `src/main.cpp`, `src/ui/DisplayLines.cpp`; `platformio.ini` (`platformio/espressif32@6.11.0`, the Arduino core it brings and its ESP-IDF); the 1.6 review's `[Thomas]` item on the V3's heap.
**Size:** one session, long.
**Model:** claude-fable-5-1

## Goal

Put 26's messages on the air: one or two MQTT connections over TLS WebSocket,
signed in with the node's own token, publishing what the radio hears — on a
Heltec V3 that has no PSRAM, beside a MeshMaster uplink that must never be the
one to lose. The observer is off until a slot is configured, and the first
thing it gives up when memory is short is itself.

## Deliverables

- **The client.** ESP-IDF's `esp-mqtt` as the Arduino core at this pin ships it
  (check that `mqtt_client.h` and its WebSocket-over-TLS transport are in the
  core's SDK for `esp32s3`; if not, one pinned library with the one-line
  justification `tasks/README.md` asks for). Its own task; the mesh loop and
  the agent loop never wait on it: a publish is an enqueue.
- **TLS.** Each preset's host is verified against a root this firmware carries:
  read the live chains of `mqtt.meshview.dk:443` and `mqtt.meshmapper.net:443`
  from this machine (`openssl s_client -showcerts`), and add to `TlsRoots.h`
  exactly the roots they end at, with subject, validity and fingerprint written
  beside each as the existing one is; `test_tls_policy` learns them. No
  verification is ever skipped; a `custom` server that does not chain to a
  carried root is refused with the reason (or takes an explicit CA — say which
  and why).
- **What is published**, as the observer firmware does: every received packet
  (`logRxRaw`'s bytes, SNR and RSSI) as a `packets` message, QoS 0; a retained
  `status` at connect and every `mqtt.interval` (the observer firmware's
  default), with a retained `offline` as the last will. A bounded queue between
  the radio and the client (by bytes, oldest dropped and counted per slot); a
  slot that is not connected drops, it does not hoard.
- **Sign-in and renewal.** A slot waits for the clock (state
  *waiting-for-clock*) before it makes a token; the token is renewed before it
  expires as the observer firmware does; a refused sign-in backs off (5 s
  doubling to 10 min) and says why on the console.
- **Memory, in this order of priority: the radio, the MeshMaster uplink, the
  observer.** A slot connects only while free heap and the largest free block
  are above a reserve that leaves room for the uplink's own TLS handshake
  (measure what the uplink's handshake takes on the host if you can, and set the
  reserve from it with the reasoning in the code; otherwise from the observer
  firmware's 40 KB and say so). When the uplink's call fails for want of memory
  while a slot is up, the highest slot disconnects and stays down ten minutes
  (state *waiting-for-heap*), counted. Two slots at most on either board.
- **The console**: `mqtt.status` (one line per slot: preset or host, state,
  since, published, dropped, last error), and the settings of 26 take effect
  without a reboot where the observer firmware's do, with one where they do not
  (say which). `set mqttN.password …` and `set wifi.pwd …` are redacted on echo.
  The display's status line gains *MQTT 2/2* (or nothing when off).
- **The log** says connects, refusals and drops as counts and reasons, never a
  packet, a token or a password.
- **Docs**: `docs/observer.md` — what it is, the lines to type (this firmware's
  version of the Danish guide, below), what is published and to whom, what it
  costs in memory, and that it is off by default.

## Acceptance criteria

- [ ] A native test drives the slot state machine through connect, refused sign-in, clock missing, heap short, reconnect and renewal with fakes for the client and the clock.
- [ ] `TlsRoots.h` carries the roots the two hosts' live chains end at, with the evidence in the report; `test_tls_policy` green.
- [ ] Nothing reaches the log but counts and reasons (a test over the log ring after a run with the fakes).
- [ ] Both board builds green; RAM and flash deltas in the report; the V3's static RAM after this stated against its 320 KB.
- [ ] `pio test -e native` and the host matrix green.

## Not verified here

A real broker, and the V3's heap with WiFi, the uplink and two WSS connections
up at once — Thomas, on the bench (the README says what to read).

## Out of scope

The heartbeat (28). More than two slots. Publishing anything but what the radio
received.
