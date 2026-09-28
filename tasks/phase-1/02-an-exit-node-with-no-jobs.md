# 02 — An exit node with no jobs

**Depends on:** 01.
**Spec:** [EXIT-NODES §5, §6, §10](https://github.com/twinoak/meshmaster.dk/blob/fbf4625a0352663e1c26bd6877e45e6c555d4f7c/spec/EXIT-NODES.md), [API §3](https://github.com/twinoak/meshmaster.dk/blob/fbf4625a0352663e1c26bd6877e45e6c555d4f7c/spec/API.md), [UI §6, §7](https://github.com/twinoak/meshmaster.dk/blob/fbf4625a0352663e1c26bd6877e45e6c555d4f7c/spec/UI.md).
**Size:** one session.
**Model:** claude-opus-5

## Goal

Milestone 1: the board is an exit node on meshmaster.dk. It heartbeats,
its page shows *online* with the firmware and the radio parameters read
from the board itself, and the adverts it hears feed the unknown-nodes
list and reachability — everything the .NET agent does between jobs, with
`jobTypes: []` so the server hands it nothing yet.

## Deliverables

- **The mesh loop.** MeshCore's `Mesh` running with our identity in the
  main loop, `onAdvertRecv` (and the packet-heard hook) feeding an
  observation ring: public key, path and hops, SNR and RSSI, name,
  position and role as decoded — the fields `Observation` carries
  (`Envelopes.cs`), nothing else, and never a message's text.
- **Heartbeat** every `pollIntervalS` the server last answered (30/120),
  `AgentHeartbeat` field for field: `agentVersion`, `transport: "onboard"`,
  `companionPublicKey` = our own key, `companionFirmware` = the MeshCore
  tag, `radio`, `jobTypes: []`, `budget` as configured, `companionConnected:
  true`, `queueDepth: 0`, `errors` with the counters this firmware keeps
  (`overheard`, `wifiReconnects`, `httpFailures`, `observationsDropped`).
  The reply's `serverTime` corrects the clock when NTP has not; `budget`
  and `pollIntervalS` are taken; `jobsWaiting` is logged and ignored until
  03.
- **Observations** batched per `POST /agent/v1/observations` with a
  client-generated batch id (idempotent on the server), sent when the
  batch holds twenty or every sixty seconds, whichever first; a **bounded
  ring** (RAM only: decide the size from 01's heap number, say it in the
  report) that drops the oldest when the server or WiFi is away, counting
  the drops. The .NET agent's shapes are the fixture.
- **Rate and errors.** Never more than the agent policy's 120 requests a
  minute; a 429 honours `Retry-After`; a 401 stops everything but the
  console and says *token revoked or wrong* on the OLED; a 5xx backs off.
- **The console** gains `mesh` (adverts heard, last five with names),
  `server` (last heartbeat, next, last observation batch, the counters),
  and `log.level debug` prints each heartbeat's reply in one line.
- **The OLED**'s third line becomes *hb 12:03 · 17 adverts*; the fourth the
  identity.
- **Docs**: the project README's "what you should see on meshmaster.dk"
  section — the exit node online, its page's radio line, the unknown nodes
  arriving.

## Acceptance criteria

- [ ] Native tests: the heartbeat and observation batch JSON equal the .NET fixtures byte for byte for the same input; the ring drops the oldest and counts; the batch timer fires at twenty or sixty seconds.
- [ ] `pio run -e heltec_v3` green; the firmware string in the heartbeat matches `status`.
- [ ] A 401 halts the network loop and is visible on console and OLED; a 429 waits `Retry-After`.

## Not verified here (Thomas, with the board)

The exit node's page on meshmaster.dk online with `onboard` as transport
(task 66), the radio parameters as set on the bench, unknown nodes
appearing within a few minutes at a window — and the heap over an hour.

## Out of scope

Claiming anything (03).
