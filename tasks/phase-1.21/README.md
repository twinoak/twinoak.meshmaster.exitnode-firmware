# Pile 1.21 — A reply comes back the way the request went

Written on 22 November.

**One task, one launch:**

```powershell
.\tasks\run-all.ps1 -Phase '1.21'
```

Nothing here waits for another repository, and nothing of the contract moves.

## What happened

On 22 November a trace of TwinOak Ebeltoft through Risskov read 7 to 12 dB on all six hops,
and the status read two minutes later had two logins unanswered. A trace comes back along the
hops it names. A reply does not: a repeater answers along the way back a client has told it,
this firmware tells none, and so every reply to an exit node is flooded
(`tasks/phase-1.20/FOLLOWUPS.md`, *A reply does not come back the way the request went*).

## What Thomas decided

That the exit node tells the repeater the way back, and that it does so by itself: no button.

## What the task builds

- **71** — before every login that goes out direct the exit node tells the repeater the way
  back: the path reversed. Replies then return by the hops a trace measures and are repeated
  by nobody else. A job's log says how each reply came, and a way back the repeater did not
  take is told once more.

**Chosen by me, his to change before the run:**

- **Before the login, not after it.** The login's own reply then takes the way back, and a
  pin that changed is told before anything has to come back along the old one.
- **At every direct login, each try.** A repeater forgets: a restart, a guest pushed out of
  its table. Sessions last ten minutes, so an hourly read tells it once an hour.
- **In the login's slot of the airtime gate**, not a transaction of its own. The exit node
  sends one short packet more; the mesh is spared a flood for every reply.
- **How a reply came is in the job's log only.** No field in a result, no contract change.

**What gets less forgiving:** a reply has one way home. A link that is good out and poor back
fails where a flood might have got through. The trace shows both directions; the pin is the
cure. A pair that is not pinned gets its flood back from meshmaster.dk's task 256.

## Order and dependencies

| # | Task | Depends on | What it leaves behind |
|---|---|---|---|
| 71 | [the-way-back-is-told](71-the-way-back-is-told.md) | — | the way back told before every direct login and when a reused session's path changed; a reply's route in `MeshReply` and in the job's log; told once more when a reply still came flooded; the fake repeater keeps an out path as the pin does |

## Models

- **Opus** — 71.

## Afterwards (Thomas)

- **The notes pull request of v0.7.6** merged first if it is still open (`VERSION` reads
  0.7.6 and `release-notes/unreleased/` holds 67 and 68).
- **Release**, import it on meshmaster.dk, and update Risskov and Aarhus C from their
  Firmware tabs. The release carries pile 1.20 too, so **meshmaster.dk's pile 4.47 is
  deployed before it**.
- **Then look:** *Read now* on TwinOak Ebeltoft. The log should say the replies came direct.
- **The Walter** takes the library at its next sync. Nothing changes for it.

## Follow-ups

Review findings go into [FOLLOWUPS.md](FOLLOWUPS.md).
