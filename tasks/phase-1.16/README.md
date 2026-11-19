# Pile 1.16 — The fixtures arrive, and two things the review found

Written on 19 November.

**Pull first**: `master` moved on GitHub after this clone was last fetched (the notes pull
request of v0.7.2 was merged there). Then one launch, three tasks in the order of their numbers:

```powershell
git pull
.\tasks\run-all.ps1 -Phase '1.16'
```

Nothing here waits for another repository, and nothing of the contract with meshmaster.dk
changes.

## `master` is red (62)

On 19 November the sync brought the six fixtures that tasks 56 and 61 had written ahead. They
are the same bytes, and the test that was put there to say *the sync has brought it* says so:
one of 840 fails, and it fails until the tests read the six from the copied tree and the
waiting files are gone. Task 62 does that. It touches tests and words and no line of the
firmware, and `master` is green when it has merged.

## A table that could not be written (63)

The review of pile 1.15 (its `FOLLOWUPS.md`): adding the second slot erases one sector and
writes one table, once. A write that fails leaves a board that runs on with no partition table
until its next reset, and when the server asked for the update nobody is there to read the
sentence that says so. Task 63 has the firmware try again, put the old table back when the new
one cannot be written, and keep trying for as long as it runs when neither holds.

## The way back (64)

The same review: after an update from A to B, flashing A over USB does nothing and says
nothing, because the firmware cannot tell a slot that was flashed a minute ago from one that
has held the same bytes since the update. Task 64 gives the console `update back`, which
switches to what lies in the other slot, on trial like any other, and makes the boot say why
it did not switch. And a trial that failed ends in a restart where the framework refuses its
rollback, so that nothing is called rolled back before it is.

## What no task here can do

The first update of a board whose table is `one` writes its partition table with nobody there,
and a power cut in that moment needs USB and a merged image. That was decided on 18 November
and cannot be designed away; the order of things below is what there is.

## Order and dependencies

| # | Task | Depends on | What it leaves behind |
|---|---|---|---|
| 62 | [the-waiting-fixtures-have-arrived](62-the-waiting-fixtures-have-arrived.md) | — | the six fixtures are read from the copied tree and the waiting files are gone; `master` is green |
| 63 | [a-table-that-could-not-be-written](63-a-table-that-could-not-be-written.md) | — | a table write is tried three times, the old table is put back when the new one cannot be written, and a board with no table keeps trying and says so |
| 64 | [the-way-back](64-the-way-back.md) | — | `update back` switches to the other slot; the boot says why it did not switch; a failed trial restarts where the framework refuses its rollback |

## Models

- **Opus** — 62, 63, 64.

## Afterwards (Thomas)

- **The signing key, before the release**: the `RELEASE_SIGNING_KEY` secret in this
  repository, 64 hex characters, read as the Walter's workflow reads it.
- **The release.** Its version is `VERSION`'s. It is the first a board can be updated *from*.
- **The bench, in this order, before anybody else is told**, on a V3 and a V4 with that
  release flashed as the plain `.bin` (`docs/field.md` §9):
  1. `update slot`, `reboot`, `update`: the word is `two`, and the settings, the token and the
     identity are as they were.
  2. `update fetch <id>` of a second release, `update apply`, the ten minutes. Read the journal
     line of the trial: it says whether this board's bootloader undoes a reset by itself.
  3. The same again with the power pulled during the trial: the old firmware comes back.
  4. `update back`, and the ten minutes again.
  5. On meshmaster.dk, with one board whose table is still `one` at the desk: *Update* on its
     Firmware tab. It adds the slot, restarts, fetches, switches and proves, and the page shows
     the journal.
- **Then the last USB flash of every Heltec**: that release, as the plain `.bin`, through the
  web flasher. Never the merged image on a board that has an identity.
- **Record the release on meshmaster.dk** (Admin › Settings), import it on `/firmware`, canary
  it for your own exit node first.
- **The Walter's waiting fixtures are not synced yet.** Its repository has the same tripwire,
  and one of its files, `FirmwareApplyProgress.json`, is *not* the same bytes as meshmaster.dk's
  (its pile 17's follow-ups). A sync there turns its `main` red until a task like 62 is written
  for it.

## Follow-ups

Review findings go into [FOLLOWUPS.md](FOLLOWUPS.md).
