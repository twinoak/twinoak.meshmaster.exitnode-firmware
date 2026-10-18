# Pile 1.6 — the library, as its first consumer found it

Written on 18 October from the review of pile 1.5
([`../phase-1.5/FOLLOWUPS.md`](../phase-1.5/FOLLOWUPS.md), its five `[1.6]`
items), what the Walter's p4-01 found building the library as an ESP-IDF
component, and one consequence of meshmaster.dk's pile 4.15: the contracts
move to the relaxed JSON encoder (Thomas's decision of 18 October), and this
firmware's writer copies the old one byte for byte. Five tasks.

**Prerequisite:** pile 1.5 merged and `v0.2.0` tagged (both are). Task 24
also waits for meshmaster.dk's **pile 4.15 merged** (129 and 131): start this
pile after that, or run it now and let 24 stop until then — it checks first
and resumes with `-From 24`.

`v0.2.0` made the agent a library and its first consumer used it the same
week. What it found: the library does not build under a `gnu++` dialect or at
`-O2` with GCC 13 (20); a wake's byte and uplink budgets leak at the job step
(21); a deep sleep loses the results a wake could not post, and their jobs
stay leased (22); and the interface cannot say `native`, cannot say *I cannot
do that*, and asks for two fields nobody reads (23). 24 takes the new wire
dialect and closes `v0.3.0`. **The Heltec's behaviour does not change** in
20–23, and in 24 only by the bytes the relaxed rule saves.

## Order and dependencies

| # | Task | Depends on | What it leaves behind |
|---|---|---|---|
| 20 | [built-like-a-consumer](20-built-like-a-consumer.md) | — | `unix` → `epoch` and a check for predefined names; the host check as a matrix (`c++17`/`gnu++17` × `-O0`/`-O2`) on CI's GCC 13; ArduinoJson's warning removed narrowly |
| 21 | [a-wake-keeps-its-budget](21-a-wake-keeps-its-budget.md) | 20 | every post of a wake inside its byte cap and its uplink seconds; results in order under any cap; the property test extended |
| 22 | [what-a-sleep-keeps](22-what-a-sleep-keeps.md) | 21 | an optional store for one image a wake: kept results first, then adverts, whole or not at all, checksummed, never a credential; the Heltec passes none |
| 23 | [the-interface-says-what-it-is](23-the-interface-says-what-it-is.md) | 22 | `HeartbeatIdentity::transport`; a mesh that can say what it cannot serve, answered `unsupported`; `viaPath`/`snr` read or gone; `agent-core` 0.3.0 |
| 24 | [the-relaxed-dialect](24-the-relaxed-dialect.md) | 23, meshmaster.dk 4.15 | fixtures synced; `JsonWriter` on the relaxed rule, pinned by `JsonEscaping.json`; the no-wake heartbeat fixture; `docs/library.md` §2 for a private repository; `CHANGELOG.md` `v0.3.0` closed |

20 → 21 → 22 → 23 → 24.

```powershell
.\tasks\run-all.ps1 -Phase '1.6'
```

(The quotes: PowerShell reads a bare `1.6` as a number.)

## Models

- **Fable** — 22, 23. A storage format a unit lives with across thousands of
  sleeps, and an interface two firmwares compile against.
- **Opus** — 20, 21, 24.

## What the other repositories wait for

- **The Walter** (`twinoak.meshcore.walter-firmware`) syncs `v0.3.0` after
  Thomas tags it: it drops its local `unix` patch (20), implements the store
  (22), sends `native` and declines `trace` over its UART (23), and takes the
  new dialect (24). Its p4-02 *local door* wording (`failed` vs
  `unsupported`) is settled by whichever design 23 picks.
- **meshmaster.dk** waits for nothing here.

## The exit criterion

The library builds clean in all four configurations on GCC 13; a wake spends
no byte and no second outside its budget; a kept result survives a simulated
deep sleep and its job does not stay leased; a consumer can send `native`
and refuse a type; every contract fixture, including `JsonEscaping.json`,
matches this writer byte for byte; Thomas tags `v0.3.0`.

## For Thomas, after the pile

- Tag `v0.3.0` on the merge of 24 (CI green); the release workflow makes the
  images.
- The bench walk of `docs/console.md` on that image, with one `frame tx` line
  and one job result's raw body pasted into a report. AarhusC and Grindsted
  still want flashing (they are on `v0.1.3`); 1.4's four guest-door checks go
  on the same flash.

## Follow-ups

Review findings go into [FOLLOWUPS.md](FOLLOWUPS.md), tagged with the task that
must pick them up.
