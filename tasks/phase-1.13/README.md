# Pile 1.13 — a batch keeps its id over a sleep

Written on 16 November.

**What happened.** The Walter unit loses the answer to its observations post at nearly every
wake. That is its own fault and its own pile (phase 15 there). What makes it expensive is here:
the loop resends a batch under the id it was built with, so that the server stores nothing
twice, and it keeps that id in RAM. A board that sleeps wakes without it. On 16 November
meshmaster.dk stored the same adverts from that unit at every wake, six and more times each.

One task, in the library. Nothing changes for a board that does not sleep.

## Order and dependencies

| # | Task | Depends on | What it leaves behind |
|---|---|---|---|
| 55 | [a-batch-keeps-its-id-over-a-sleep](55-a-batch-keeps-its-id-over-a-sleep.md) | — | the kept image holds the pending batch's id and count; a resend after a sleep is the same batch; version 1 images are still read; a constant for the largest image |

```powershell
.\tasks\run-all.ps1 -Phase '1.13'
```

## Models

- **Opus** — 55.

## Afterwards (Thomas)

- **Release** with the button and merge its notes pull request. The Heltec exit nodes need not
  be updated for this.
- **The Walter takes the library at that release**: its `upstream` workflow opens the pull
  request, and its phase 15's task 04 is what is left there.

## Follow-ups

Review findings go into [FOLLOWUPS.md](FOLLOWUPS.md).
