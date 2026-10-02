# Pile 1.1 — while the board is in the post

Phase 1 is merged and nothing has been flashed: the Heltec V3 has not
arrived. Everything on the bench list waits for it. Two things do not, and
the second is the one that makes the wait worth having: the firmware's
protocol side — the claim loop, every handler, the credential buffer, the
restore, the apply engine — has only ever run as native unit tests, while
the Pi agent has been run end to end against a fake companion for every
task since phase 2. This pile gives the firmware the same thing on the
workstation, so that the day the board arrives the only questions left
are the radio's.

## Order and dependencies

| # | Task | Depends on | What it leaves behind |
|---|---|---|---|
| 07 | [the-small-ones](07-the-small-ones.md) | — | the contract fixtures at meshmaster.dk `b04799f` and `test_envelopes` reading the observation and claim files instead of inline expectations; `StatusPayload.version` honoured; the merged task branches gone from origin |
| 08 | [the-host-harness](08-the-host-harness.md) | 07 | `[env:host]`: the agent loop built for Linux against a fake mesh that answers as stock repeater firmware does and a plain-HTTP client, run end to end against meshmaster.dk's compose stack on the workstation and against a recorded server stub in CI — every job type claimed, run and settled without a radio |

07 → 08.

```powershell
.\tasks\run-all.ps1 -Phase 1.1
```

## Models

- **Opus** — 07, 08.
