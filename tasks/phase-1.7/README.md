# Pile 1.7 — the exit node as an observer

Written on 26 October from Thomas's question of that day — *could the exit
node do what observer.gessaman.com's firmware does, with presets for
MeshView.dk and MeshMapper.net?* — the Danish observer guide he gave with it
(below), his choice that it is configured on the console and only shown in
MeshMaster, and the review of pile 1.6
([`../phase-1.6/FOLLOWUPS.md`](../phase-1.6/FOLLOWUPS.md)). Four tasks.

**Prerequisite:** pile 1.6 merged and `v0.3.0` tagged (both are). Task 28 also
waits for meshmaster.dk's **pile 4.17 merged** (136 and 140): start this pile
after that, or run it now and let 28 stop until then — it checks first and
resumes with `-From 28`.

An *observer* publishes every packet it hears to MQTT brokers that draw the
community's maps and analysers, signed in with a token made from its own key —
no shared secret. The lines of the Danish guide below are the console of the
`observer-firmware` branch of `agessaman/MeshCore` (MIT), the firmware
observer.gessaman.com flashes. This pile teaches
the exit node the same dialect and the same lines, beside its MeshMaster
uplink, with that uplink always first.

### The Danish guide, as Thomas gave it (26 October)

```
# Grundlæggende opsætning
set timezone CEST
set bridge.source rx
reboot

# WiFi opsætning
set wifi.ssid navn_ssid
set wifi.pwd password
reboot

# MQTT opsætning, sæt IATA til en af: AAL, AAR, BLL, ODE, CPH eller RNN
set mqtt.iata XXX
set mqtt.owner [PublicKeyPåDinNode]

# MeshView opsætning i slot 1
set mqtt1.preset custom
set mqtt1.server wss://mqtt.meshview.dk:443/mqtt
set mqtt1.audience mqtt.meshview.dk
set mqtt1.topic meshcore/{iata}/{device}/{type}

# MeshMapper opsætning i slot 2
set mqtt2.preset meshmapper

reboot
```

After this pile the same lines configure an exit node, and `set mqtt1.preset
meshview` is the four MeshView lines in one. `timezone` and `bridge.source`
are answered with what this firmware does instead; `wifi.pwd` is another name
for `wifi.pass`.

## Order and dependencies

| # | Task | Depends on | What it leaves behind |
|---|---|---|---|
| 25 | [the-strings-are-utf-8](25-the-strings-are-utf-8.md) | — | `JsonWriter` never writes invalid UTF-8 (maximal subparts as U+FFFD); kept adverts without a clock dropped and counted; a declined type declined before it is priced; `agent-core` 0.3.1 |
| 26 | [the-observer-dialect](26-the-observer-dialect.md) | 25 | `src/observer/`: packet and status messages byte for byte as the observer firmware writes them, topics, the Ed25519 token, the presets `meshview`, `meshmapper`, `custom`, the console grammar the Danish guide uses; host-tested, no network |
| 27 | [the-broker-connection](27-the-broker-connection.md) | 26 | `esp-mqtt` over TLS WebSocket, two slots, the roots the two hosts chain to, renewal and backoff, a heap reserve that keeps the uplink first, `mqtt.status`, `docs/observer.md` |
| 28 | [the-observer-in-the-heartbeat](28-the-observer-in-the-heartbeat.md) | 27, meshmaster.dk 4.17 | fixtures synced; `observer` in the heartbeat through a provider; `failedAt` in every failed result; `agent-core` 0.4.0 |

25 → 26 → 27 → 28.

```powershell
.\tasks\run-all.ps1 -Phase '1.7'
```

(The quotes: PowerShell reads a bare `1.7` as a number.)

## Models

- **Fable** — 26, 27. A token signed with the node's own key, and two TLS
  connections on a board without PSRAM beside the one that must not fail.
- **Opus** — 25, 28.

## Before you flash it (Thomas)

1. **Measure first.** On a V3 with `v0.3.0`, after WiFi is up and one
   MeshMaster call is done, read `status`'s heap line (free, minimum ever,
   largest block) and put it in `../phase-1.6/FOLLOWUPS.md`'s `[Thomas]` item.
   Static RAM is 49 % of 320 KB today, and nothing measured on a board says
   how much a TLS connection leaves.
2. **The first observer flash on one node** (the Risskov test node), one slot
   (MeshView), then two: read `mqtt.status` and `status`'s heap after an hour,
   and check the node on meshview.dk and meshmapper.net. Then the rest.
3. Tag `v0.4.0` after 28 has merged.

## Privacy, in one place

The observer forwards the raw bytes the radio received — as every observer on
these maps does — to the brokers the node's owner configured, and to nobody
else. Nothing in this firmware decodes them, logs them or shows them;
MeshMaster receives only the observer's state (meshmaster.dk D-46).

## The exit criterion

The Danish guide's lines configure an exit node; its packets and status reach
both brokers in the observer firmware's dialect, signed with its own key; the
MeshMaster uplink keeps working with both slots up on a V3; MeshMaster's
Observer card shows both slots; Thomas tags `v0.4.0`.

## Follow-ups

Review findings go into [FOLLOWUPS.md](FOLLOWUPS.md), tagged with the task that
must pick them up.
