# Pile 1.2 — the display stays on

The first pile written with a board on the bench. `v0.1.1` is flashed on
Thomas's test exit node and has been online against meshmaster.dk for a day;
everything here is what that day showed, taken from the boxes in
[`../phase-1/FOLLOWUPS.md`](../phase-1/FOLLOWUPS.md) in the words they were
made in. Three tasks, all on master; 08 (the host harness, stopped on its
branch for Thomas's four clicks) is not a prerequisite and is not touched -
it rebases over 09's `main.cpp` when it resumes.

It runs **beside** meshmaster.dk's pile 4.8: nothing here changes the agent's
contract or the contract fixtures, and nothing there changes the firmware.

**Prerequisite:** master at `0d6da15` or later (`v0.1.1`).

## Order and dependencies

| # | Task | Depends on | What it leaves behind |
|---|---|---|---|
| 09 | [the-display](09-the-display.md) | — | an OLED that stays on unless told otherwise, toggled by the PRG button, showing four lines a person at the window can read: who, the clock, WiFi and Online, the last heartbeat, and what it is doing |
| 10 | [the-small-ones](10-the-small-ones.md) | — | a backoff that honours `Retry-After` and caps at two minutes once the link is proven; `neighbours` answered at a guest login; the release notes and README naming the merged image as the first flash |
| 11 | [the-partition-table](11-the-partition-table.md) | 10 | NVS above the application on both boards, so that the merged image can no longer erase a provisioned board's settings; one re-provisioning on the day it lands, said in the release notes |

11 runs after 10 because 10 writes the sentence that 11 then changes.

```powershell
.\tasks\run-all.ps1 -Phase 1.2
```

## Models

- **Fable** — 11 (one wrong offset loses an identity; the rails re-read).
- **Opus** — 09, 10.

## The findings

### From Thomas, 4 October, with the board on the bench

1. **The display.** *"For the next pile, I'd like that the display on the
   Heltec doesn't turn off."* Then the lines: *"1: left side ID: {first 4
   chars of public key} and right side the clock hh:mm:ss if available. 2:
   WiFi: yes/no, Online: yes/no, where WiFi indicates if WiFi is connected and
   Online indicates if there is connection to MeshMaster. 3: Last heartbeat:
   hh:mm:ss. 4: Current job: {jobid}? Not sure about this one, but something
   that makes sense if I look at the display to determine if it is currently
   active or not."* And: *"Would it be possible to toggle the display with
   the PRG button?"* Task 09.
2. **The backoff after a server deploy** (bench, 3 October): a release on
   meshmaster.dk cost five failed calls and the doubling wait then outlasted
   the outage by a quarter of an hour, so the exit node read *quiet* long
   after the server was back. Task 10.
3. **The merged image erases the settings** (bench, 4 October): *"I just
   reflashed 0.1.1 without selecting 'erase first' on meshcore.io. But I
   seemed to lose the WiFi, token and everything? I took the 'merged' file,
   was that the mistake or do we have a bug?"* Neither: `default_8MB.csv`
   puts NVS at `0x9000`, inside the span the merged image fills. Then:
   *"Should we skip building the merged versions?"* No - it is the only way
   onto a board that is not already running this firmware. Task 10 says so
   in the README and the release notes; task 11 moves NVS out of the span.
4. **`neighbours` at guest** (meshmaster.dk task p4.7-94): the server now
   runs the neighbour-list job at class `guest` for a guest-only pair, and
   this firmware's `typeAllowsClass` refuses it. Task 10.

### Not in this pile

- The watchdog has never fired: a build with a deliberate hang is a bench
  afternoon, not a headless task.
- The host display shim: it wants 08's `[env:host]`, which is on its branch.
  09 makes the lines a pure function and tests them natively instead.

### Watch

- The 1.1 review and 08's four clicks: `tasks/phase-1.1/logs/STOPPED-08.md`.
- The OLED after a month lit: an SSD1306 ghosts; the fix if it shows is a
  pixel shift, not a timeout.
