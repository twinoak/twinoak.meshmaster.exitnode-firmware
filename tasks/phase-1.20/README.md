# Pile 1.20 — A wake that takes what is waiting

Written on 22 November.

**Two tasks, one launch:**

```powershell
.\tasks\run-all.ps1 -Phase '1.20'
```

Nothing here waits for another repository to run: the four new lines of the contract are in
both tasks and wait in `tests/fixtures/contracts-ahead/`. **meshmaster.dk's pile 4.47 is
deployed before this is released**: a claim from this firmware carries `holding`, which an
older server has never seen.

## What happened

On 21 November a NoiseScope sample from the Walter at Bellevue took five wakes, because a wake
is one claim of two jobs; and an hour before, a claim's answer was lost on the way and the
server had leased two jobs to a unit that never knew.

## What Thomas asked for

That the Walter takes jobs until none are left or the battery says sleep.

## What the two build

Both are the library's, so the Heltecs carry the code and the Walter takes it at its next sync.

- **69** — `wake()` claims again when it has finished what it was handed, while the server
  says more is waiting and the wake has the time, the transactions and the claims for it. The
  caller says how many claims; a caller that says nothing gets one, as today.
- **70** — every claim says what the board holds, and a claim that did not come back is made
  once more. The server's task 254 answers it with what was lost.

**Chosen by me, his to change before the run:** two jobs a claim stays; the number of claims is
the board's own bound and is not told to the server; a failed claim is made once more and not
twice; `loop()` does not act on `jobsWaiting`.

## Order and dependencies

| # | Task | Depends on | What it leaves behind |
|---|---|---|---|
| 69 | [a-wake-claims-again](69-a-wake-claims-again.md) | — | `WakeIntent::maxClaimsThisWake`; a wake claims again on the server's `jobsWaiting`, bounded by time, transactions and that number; `transactionsLeft` in the later claims; two waiting fixtures |
| 70 | [a-claim-that-was-lost](70-a-claim-that-was-lost.md) | 69 | `holding` in every claim; one more claim after one that did not come back; a job handed over twice run once; two waiting fixtures |

## Models

- **Opus** — 69, 70.

## Afterwards (Thomas)

- **meshmaster.dk first:** its pile 4.47 merged and deployed.
- **Then the fixtures:** `.\tools\sync-fixtures.ps1 -Commit master`, the four files compared
  byte for byte with the waiting ones, the tests pointed at the copied tree (a small task, as
  pile 1.16's 62 was).
- **Release**; the Heltecs take it from their Firmware tab. Nothing changes for them but
  `holding` in a claim.
- **The Walter:** its `upstream` workflow against the release, the sync task, then its pile 18.

## Follow-ups

Review findings go into [FOLLOWUPS.md](FOLLOWUPS.md).
