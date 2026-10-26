# 26 — The observer dialect

**Depends on:** 25.
**Spec:** Thomas's request of 26 October (below) and the Danish observer guide he gave the same day (in the README of this pile); the observer firmware this is modelled on — `agessaman/MeshCore`, branch **`observer-firmware`** (MIT): `MQTT_IMPLEMENTATION.md` (topics, payloads, JWT, slots), `src/helpers/MQTTPresets.h` (`MQTTPresetDef`, the `meshmapper` row), and the bridge sources it names — read them at a commit you cite, and copy nothing you cannot cite; observer.gessaman.com (its flasher and docs); this repository's `src/mesh/AgentMesh.{h,cpp}` (`logRx`, and MeshCore's `Dispatcher::logRxRaw(snr, rssi, raw, len)` hook at the pin), `src/settings/Keys.cpp`, `src/console/`, `lib/agent-core/include/agent/JsonWriter.h`, `docs/host.md`; MeshCore at the pin for `LocalIdentity::sign` (Ed25519).
**Size:** one session, long.
**Model:** claude-fable-5-1

## Goal

Thomas, 26 October: *could the exit node do what observer.gessaman.com's
firmware does, with presets for MeshView.dk and MeshMapper.net?* An observer
publishes every packet it hears to MQTT brokers, signed in with a token made
from its own key. This task writes the **dialect** — every byte the observer
will send, and the token — as portable, host-tested code, with no network in
it; 27 puts it on the air. The goal is that the observer firmware's
configuration lines, typed on this firmware's console, mean the same thing, and
that what reaches the brokers is what they already take from that firmware.

## Deliverables

- **Where it lives.** `src/observer/` for the board half, and its portable
  half — formatting, topics, token, presets, the settings' grammar — in files
  `[env:native]` compiles beside a `test/test_observer/` suite. Not in
  `lib/agent-core/`: the Walter has no use for it (say so in `docs/library.md`).
- **The packet and status messages**, field for field as the observer firmware
  writes them (`MQTT_IMPLEMENTATION.md`: `origin`, `origin_id`, `timestamp`,
  `type`, `direction`, `time`, `date`, `len`, `packet_type`, `route`,
  `payload_len`, `raw`, `SNR`, `RSSI`, `score`, `hash`, `path`; status with
  `status`, `timestamp`, `origin`, `origin_id`, `model`, `firmware_version`,
  `radio`, `client_version`, `repeat`, `stats`), including what it writes as a
  string and what it leaves out; how `hash` and `score` are computed, read from
  its source. Fixtures under `test/test_observer/fixtures/` (not `tests/fixtures/`, which only the sync script writes) written from that source
  by hand **with the file and line each field came from** in a header comment,
  and the formatter held to them byte for byte. Only `rx`: an exit node
  forwards nothing (`repeat: false`).
- **Topics** from a template with `{iata}`, `{device}` (the node's public key,
  uppercase hex) and `{type}` (`packets`, `status`), default
  `meshcore/{iata}/{device}/{type}`.
- **The token**: the observer firmware's JWT, exactly — header, claims
  (`publicKey`, `aud`, `iat`, `exp`, and `owner` / `email` when set), the
  base64url encoding, the Ed25519 signature by the node's own identity, the
  username `v1_<PUBLIC KEY>` and the token as the password, its lifetime and
  when it is renewed. A host test signs with a fixed test identity and
  verifies the signature with the public key (MeshCore's ed25519 in the native
  build), and decodes header and claims back. The private key never leaves the
  identity and nothing of the token is logged.
- **Presets**, a table like `MQTTPresetDef` with only what this needs:
  - `meshview` — `wss://mqtt.meshview.dk:443/mqtt`, JWT with audience
    `mqtt.meshview.dk`, topic `meshcore/{iata}/{device}/{type}` (the Danish
    guide's *custom* lines, as one name);
  - `meshmapper` — copied from the observer firmware's row (URL, audience,
    retain, keepalive), cited;
  - `custom` — `mqttN.server`, `mqttN.audience`, `mqttN.topic` (and
    `mqttN.username` / `mqttN.password` for a broker that wants those instead
    of a token);
  - `none`.
  No preset ships a shared secret.
- **The settings and their grammar**, the observer firmware's names where it
  has one: `mqtt.iata` (three letters A–Z; the Danish codes AAL, AAR, BLL, ODE,
  CPH, RNN in its help), `mqtt.owner` (64 hex digits), `mqtt.email`,
  `mqtt1.*` and `mqtt2.*` (`preset`, `server`, `audience`, `topic`, `username`,
  `password` — a Secret, never printed or echoed), `get mqtt.presets`. Two slots.
  `wifi.pwd` is accepted as another name of `wifi.pass` (and redacted on echo
  like it); `set timezone …` and `set bridge.source …` are answered with one
  line each saying what this firmware does instead (its clock is UTC from the
  server or SNTP; an exit node publishes what it receives). The Danish guide's
  lines, typed in order, end in a valid configuration of two slots — a test
  that feeds them to the parser.
- **Licence.** The observer firmware is MIT. Code copied from it, rather than
  written from its description, keeps its notice where this repository keeps
  third-party notices, and the file says which lines.
- **Nothing decoded.** The observer carries the raw bytes it was handed and
  the radio's numbers; it never decodes a payload, and nothing it does writes a
  packet's content to the log or the display (the `log.frames` rule stands).

## Acceptance criteria

- [ ] `test/test_observer/` holds the packet and status formatters to the fixtures byte for byte, the topics to the template, the token to a verified signature, and the Danish guide's lines to the two slots they mean.
- [ ] Every field and rule copied from the observer firmware cites its file and line at a named commit.
- [ ] Nothing in this task opens a socket; both board builds green and their size delta in the report; `pio test -e native` green.

## Not verified here

A broker taking it (27, and the bench).

## Out of scope

The connection (27). The heartbeat (28). Configuring from MeshMaster.
