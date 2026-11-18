# Pile 1.15 — a listing's count, and updates from the server

Written on 18 November. Task 56 was written as pile 1.14 and moved here, so that everything
this repository has waiting is one launch:

```powershell
.\tasks\run-all.ps1 -Phase '1.15'
```

**Nothing here waits for another repository.** The contract fixtures these tasks are written
against are made by meshmaster.dk's tasks 239 and 244, which may land before or after. So
tasks 56 and 61 write them here from the lines the tasks carry, into
`tests/fixtures/contracts-ahead/`, and the copied tree is not touched. Once both sides are on
their trunks a sync brings the real files, and they must be the same bytes (`FOLLOWUPS.md`).
The drivers of meshmaster.dk, of the Walter and of this repository can run at the same time.

## A listing says how many there were (56)

Thomas, on meshmaster.dk: a repeater's neighbour list often comes back with half its
neighbours, and the ones left out are alerted on as lost and found again as new.

The library does it, and says so where it does: `runNeighbours` reads the list nine to a page,
and *a page that never came back after some already did is still a reading: the server gets
what was heard rather than nothing at all*. The job ends `ok`, the log says *the list stops at
9 of 17*, and the result is nine neighbours with nothing to tell them from a whole list. The
repeater's own count is in every page; the result leaves it out. Task 56 puts it in.
meshmaster.dk's task 239 reads it, and keeps a neighbour on its list through five listings
that leave it out for every exit node that does not write it yet.

## Updated from the server (57 to 61)

**What there is.** Every update of this firmware is a file an owner fetches and flashes over
USB. A board has one place for an application; the second was taken out of the table in
October, and its flash left unallocated. A release is not signed, and an image does not say
inside itself what it is.

**What was asked.** Thomas, 18 November: owners have asked for an update they can start from
meshmaster.dk.

**What these tasks build**, a step a task, each one usable from the console before the next
exists:

- the second slot is back, and the firmware adds it to a board's table itself (57);
- a release carries a manifest, its signature and a descriptor in each image, and the
  firmware can check all three (58);
- a release is fetched into the other slot and judged there (59);
- a judged image is switched to and runs on trial: kept when it has reached the server
  within ten minutes, undone by itself when it has not (60);
- meshmaster.dk asks in the reply to a heartbeat, the board does those steps in order, and
  posts one result (61).

**One last flash over USB, and it is the plain `.bin`.** A board gets its table from a merged
image, and the web flasher erases the whole board before it writes one: the key, the token
and the WiFi. So no board in the field is asked for that. It takes the first release of this
pile as the plain `.bin`, keeping everything, and the firmware writes the one sector of the
table when the first update is asked for. A power cut in that moment leaves a board that
needs a merged image; it is the only moment in this pile that can.

**A person starts every update**: on meshmaster.dk, one board and one release, or at the
console. Nothing here updates a board because a release exists.

**The server's half is meshmaster.dk's tasks 243 to 245**, in its pile 4.42.

**No test here can write a board's table, boot a second slot or pull its power.** The tasks
test their logic against fakes, and each leaves ⚠ items in `docs/field.md` for a V3 and a V4
on the bench. They are the proof, and they come before any owner is told.

## Order and dependencies

| # | Task | Depends on | What it leaves behind |
|---|---|---|---|
| 56 | [a-listing-says-how-many-there-were](56-a-listing-says-how-many-there-were.md) | — | the neighbours result carries the repeater's own count; a list that stopped at a page, or at the cap, is told from a whole one by its bytes |
| 57 | [a-second-slot](57-a-second-slot.md) | — | both tables have `app1` again; the firmware knows a board's table as `one`, `two` or `other` and adds the slot itself, one sector, from one function; `update` and `update slot` on the console |
| 58 | [a-release-is-signed](58-a-release-is-signed.md) | — | a descriptor in each image; `manifest.json` and its signature in a release, refused before publishing when they do not verify; the keys a build trusts; a reader that checks a release, 1 to 5 |
| 59 | [an-image-is-fetched-and-judged](59-an-image-is-fetched-and-judged.md) | 57, 58 | a release fetched from meshmaster.dk into the other slot and judged from what was written; a record of what the slot holds; `update fetch` |
| 60 | [switched-proved-or-back](60-switched-proved-or-back.md) | 59 | a judged image is switched to and runs on trial for ten minutes: kept on the mesh, the WiFi and an answered heartbeat, undone otherwise; a flash over USB still wins; `update apply` |
| 61 | [the-server-asks](61-the-server-asks.md) | 60 | the heartbeat's `update`, the ask in its reply, the steps in order across the restarts, one result posted until it is taken; no job claimed meanwhile |

## Models

- **Opus** — 56, 57, 58, 59, 60, 61.

## Afterwards (Thomas)

- **The fixtures that waited.** When meshmaster.dk's pile 4.42 is merged:
  `.\tools\sync-fixtures.ps1 -Commit master`, and the files in
  `tests/fixtures/contracts-ahead/` must be the copied ones byte for byte. It is the first
  item of `FOLLOWUPS.md`, for the next pile to do and to remove them.
- **The signing key, before the next release.** Task 58 puts into `keys/release-keys.txt`
  the public half the Walter's file has: one release key for the three firmwares, which
  meshmaster.dk already trusts. Give this repository the `RELEASE_SIGNING_KEY` secret with
  its private half. If that half cannot be had, make a key for this firmware alone:
  replace the line in the file, put the private half into the secret and nowhere else, and
  add the public half on meshmaster.dk under Admin › Firmware release keys. A release is
  refused until one of the two is done. If the notes pull request of v0.7.2 is still open,
  merge it first: the release refuses while the tag of `VERSION` exists.
- **Release**, with the button: the first version a board can be updated *from*, and the one
  whose listings say how many neighbours there were.
- **Record the release on meshmaster.dk**: Admin › Settings, the version and the address of
  the release, once its task 241 is deployed. Saving it is what tells the owners of exit
  nodes that are behind.
- **The bench, before anybody is told**: the ⚠ items of 57, 59, 60 and 61 on a V3 and a V4.
  They need a second release to update *to*, imported on meshmaster.dk and canary for the
  board on the bench.
- **The last USB flash of every Heltec**: that first release, as the plain `.bin`, through
  the web flasher. Never the merged one, which erases the key and the token.
- **From then on**: release, import it on meshmaster.dk, canary it for your own, release it
  there, and each owner presses *Update*.
- **The Walter takes the library at that release** (its `upstream` workflow). One bump
  carries task 56, pile 1.13's kept image, which its phase 15's task 04 waits for, and the
  heartbeat's new member, which is optional: the Walter hands the loop no provider, so
  nothing changes for it.

## Follow-ups

Review findings go into [FOLLOWUPS.md](FOLLOWUPS.md).
