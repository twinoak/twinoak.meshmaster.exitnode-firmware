# Pile 1.10 — collecting

Opened on 5 November for small things that ship with the next release of the
firmware. It has no release task yet: add one, or a task for something bigger,
when there is enough to release.

**7 November: 40 and 41 added** from the first repeater-v2 unit's bench. The Walter firmware's pile 9
syncs `lib/agent-core` at the commit of `master` that has them, so **no release is needed** for them,
and the Walter's pile waits for them (its task 05 stops if they have not landed). Run this pile first,
or alongside it.

**8 November: 42–44 added**, after 38–41 merged. 42 and 43 are again for the Walter, whose pile 10
syncs at the commit that has them (its task 01 stops if they have not landed). 42 is the one that
matters: on 8 November the unit woke every 15 minutes, and meshmaster.dk heard it on about every
sixth wake, because a wait after a failed call was spent only while awake. 44 is the follow-up from
meshmaster.dk's p4.23-165. Still no release task: 38, 39 and 44 are for the Heltec, and a release is
Thomas's call.

## Order and dependencies

| # | Task | Depends on | What it leaves behind |
|---|---|---|---|
| 38 | [line-by-line](38-line-by-line.md) | — | the result's log one entry per line, as the .NET agent sends it, so *The agent's log* on a job's page reads line by line; a full buffer drops whole entries |
| 39 | [a-trace-of-two-bytes](39-a-trace-of-two-bytes.md) | — (after 38) | a trace to a direct neighbour at the board's two-byte width, not one byte (D-33); the library told the width through `IMesh` |
| 40 | [a-local-job-is-not-on-the-air](40-a-local-job-is-not-on-the-air.md) | — (after 39) | `IMesh` can say a job is answered locally (defaulted false); such a job neither waits for nor spends the airtime budget. For the Walter's local door: a 34-line config read of a unit's own repeater no longer waits 11 minutes, or 40 s at once |
| 41 | [no-password-for-a-local-login](41-no-password-for-a-local-login.md) | 40 | a local job logs in with no material at every class: a unit can configure its own repeater with no admin password stored on meshmaster.dk |
| 42 | [a-wait-counts-the-sleep](42-a-wait-counts-the-sleep.md) | — | `restore()` is told how long the board was away, and a hold or a Retry-After loses that time: a 30 s backoff no longer silences a sleeping unit for hours |
| 43 | [a-local-job-is-priced-as-local](43-a-local-job-is-priced-as-local.md) | 40 | the wake knows before `beginJob` that a job is local and prices it at its reply waits, not at the gaps of the air |
| 44 | [an-empty-get-is-a-value](44-an-empty-get-is-a-value.md) | — | `"> "` is the empty value, so an empty `guest.password` reaches meshmaster.dk as *none* |

```powershell
.\tasks\run-all.ps1 -Phase '1.10'
```

## Models

- **Opus** — 38–44.

## Follow-ups

Review findings go into [FOLLOWUPS.md](FOLLOWUPS.md), tagged with the task that
must pick them up.
