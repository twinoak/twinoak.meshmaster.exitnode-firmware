# Pile 1.17 — 0.7.3 does not start

Written on 20 November.

**One task, one launch:**

```powershell
.\tasks\run-all.ps1 -Phase '1.17'
```

Nothing here waits for another repository, and nothing of the contract with meshmaster.dk
changes.

## What happened

Thomas flashed 0.7.3 onto his own board on 20 November, as the plain `.bin` over USB. It
restarted for ever with *Stack canary watchpoint triggered (loopTask)*, straight after
`clock: no time yet`. 0.7.2 brought it back with its key and its token. He removed the release
the same morning. No other board had taken it, and nobody had been mailed about it:
meshmaster.dk had no released version recorded.

The cause is in pile 1.15's code, which 0.7.3 was the first release to carry. The record of an
update is 2 764 bytes, most of them its journal, and reading it at boot puts three of them on
the loop task's stack: 9 232 bytes where there are 8 192. Every board, at every boot
(`tasks/phase-1.15/FOLLOWUPS.md`, *0.7.3 does not boot*).

## What 65 builds

A record is read into the object that keeps it, and the compiler refuses to copy one. The
build refuses a function of ours with a frame over 1 024 bytes. The loop task gets 16 384
bytes, and the board says at boot and in `status` how much of them was never used.

## What no task here can do

Start an image. The tests run on a PC, and a PC's stack is not a board's. Whether 0.7.4 starts
is read off a board with the cable in.

## Order and dependencies

| # | Task | Depends on | What it leaves behind |
|---|---|---|---|
| 65 | [a-record-is-read-where-it-lives](65-a-record-is-read-where-it-lives.md) | — | a record is loaded and emptied in place and cannot be copied; the build refuses a frame over 1 024 bytes; the loop task has 16 384 bytes and the boot and `status` say what is left of them |

## Models

- **Opus** — 65.

## Afterwards (Thomas)

- **The release.** `VERSION` is 0.7.4 since the notes pull request of v0.7.3 was merged.
- **Your own board first, with the cable in**, as the plain `.bin`: the boot passes `clock:`
  and prints its `stack` line. If it restarts instead: 0.7.2's plain `.bin`, and tell me what
  it printed.
- **`status` after ten minutes online**: the `stack` line's least ever free. Tell me the
  number, or write it into `FOLLOWUPS.md`.
- **Then the bench of pile 1.16's README**, from `update slot` on.
- **Until a board has run it for a while:** nothing imported or recorded on meshmaster.dk, and
  nobody told.

## Follow-ups

Review findings go into [FOLLOWUPS.md](FOLLOWUPS.md).
