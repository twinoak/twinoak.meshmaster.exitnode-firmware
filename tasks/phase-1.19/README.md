# Pile 1.19 — The observers stand down for a fetch

Written on 21 November.

**One task, one launch:**

```powershell
.\tasks\run-all.ps1 -Phase '1.19'
```

Nothing here waits for another repository, and nothing of the contract with meshmaster.dk
changes.

## What happened

The first two updates asked from meshmaster.dk, on 20 November. Risskov went from 0.7.4 to
0.7.5 in a minute. Aarhus C refused eight seconds into its fetch: three ranges stopped short,
the third at 48 208 of 65 536 bytes, and three failures are all a fetch gets
(`tasks/phase-1.18/FOLLOWUPS.md`). Aarhus C has both observer slots connected, and 48 208 is
three TLS records to the byte. Nothing on the board or the server says why a range stopped.

Thomas, 21 November: *it would be good for the exit nodes to stop all observers before trying
to update.*

## What 68 builds

Every observer slot is closed before a release is fetched and opens again when the fetch has
ended. A range that stops short says why. And a range that brought bytes is carried on from
where it stopped without using up a try: three requests in a row that bring nothing end a
fetch.

## What no task here can do

Show that memory was the cause on Aarhus C. The next update of that board does, either way:
it goes through, or its journal now says why not.

## Order and dependencies

| # | Task | Depends on | What it leaves behind |
|---|---|---|---|
| 68 | [the-observers-stand-down-for-a-fetch](68-the-observers-stand-down-for-a-fetch.md) | — | no observer slot is connected while a release is fetched, and each says so; a range that stops short says why and is carried on; three requests in a row that bring nothing end a fetch |

## Models

- **Opus** — 68.

## Afterwards (Thomas)

- **Merge the notes pull request of v0.7.5 before the release** (branch `release/v0.7.5`): the
  release refuses while it is open.
- **The release** carries task 67 too (`status` has the `stack` line).
- **Aarhus C's next update is still fetched by 0.7.4's rules:** the firmware that fetches is
  the one the board runs, not the one it is given. Press *Update* again as it is; if it keeps
  refusing, `set mqtt2.preset none` at the board for the update, or the plain `.bin` over USB
  once. From this release on a board stands its observers down by itself.
- **Risskov** takes it from its Firmware tab once it is imported and canary there.
- **Tell me how each ended**, and the `stack` line of `status` afterwards.

## Follow-ups

Review findings go into [FOLLOWUPS.md](FOLLOWUPS.md).
