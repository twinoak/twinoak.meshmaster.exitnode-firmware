# Pile 1.18 — A receiver put back, and a line where the words say

Written on 20 November.

**Two tasks, one launch,** in the order of their numbers:

```powershell
.\tasks\run-all.ps1 -Phase '1.18'
```

Nothing here waits for another repository, and nothing of the contract with meshmaster.dk
changes. Neither task depends on the other.

## A night Risskov did not hear (66)

From the evening of 19 November until its restart the next morning the exit node at Risskov
got no reply to most of what it asked, from repeaters that were there and heard it as well as
ever (`tasks/phase-1.17/FOLLOWUPS.md`). MeshCore resets a radio's receiver on a timer for
this, and the timer is off unless the application turns it on. Task 66 turns it on: a setting,
`agc.reset.interval`, four seconds unless set, which is what the standard profile gives the
repeaters. Thomas, 20 November: *this will happen again*.

**What it cannot do:** show that the receiver was the cause. A night like that one cannot be
brought on at will.

## The stack line (67)

Task 65's line for the loop task's stack is in `get mqtt.status` and the words say `status`.
Task 67 puts it in `status` too and corrects the words.

## Order and dependencies

| # | Task | Depends on | What it leaves behind |
|---|---|---|---|
| 66 | [the-receiver-is-put-back-on-a-timer](66-the-receiver-is-put-back-on-a-timer.md) | — | the exit node resets its radio's receiver every `agc.reset.interval` seconds, 4 unless set; a noise floor being measured again is no reading; `status` says the interval |
| 67 | [the-stack-line-where-the-words-say](67-the-stack-line-where-the-words-say.md) | — | `status` prints the loop task's `stack` line, and the words name the command each line is in |

## Models

- **Opus** — 66, 67.

## Afterwards (Thomas)

- **Merge the notes pull request of v0.7.4 (#73) before the release.** The release refuses
  while it is open; `VERSION` is 0.7.5 once it is merged.
- **The release** is the first a board can be updated *to* from meshmaster.dk.
- **Import it there:** Firmware, *Exit node*, *Import a release*, by its GitHub Release
  address.
- **Risskov, the way the other owners' boards will go:** 0.7.4's plain `.bin` over USB first,
  the slot left alone, then *Update* on its Firmware tab. It adds the slot, restarts, fetches,
  switches and proves, and the page shows the journal. Keep it powered.
- **Aarhus C:** *Update* on its Firmware tab. Its slot is there since 20 November.
- **After each:** `status`. The `radio` line says *every 4 s* and the `stack` line's least
  ever free is the first reading that has a fetch and a signature in it. Tell me the number.
- **The four seconds are my choice** and yours to change: `set agc.reset.interval <seconds>`
  on a board, or the number in task 66 before the run.

## Follow-ups

Review findings go into [FOLLOWUPS.md](FOLLOWUPS.md).
