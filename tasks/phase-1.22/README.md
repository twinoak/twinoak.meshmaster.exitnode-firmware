# Phase 1.22 — three things left over

Written on 25 November.

**What happened.**

- **24 November:** a result the server refused sat first among a unit's kept results and
  held every other result back for three hours (`tasks/phase-1.21/FOLLOWUPS.md`, *A result
  the server will not take*). The library's flush stops at the first post that fails and
  cannot tell *refused* from *not reached*.
- **23 November:** Thomas asked for the firmware's release notes to be readable inside
  meshmaster.dk, where an update is offered.
- **22 November:** an owner of two boards saw one take another address after an update.
  The firmware sets no MAC; it does give every board the same name on the network.

**What this pile builds.** 72: a refused result is dropped, said, and holds nobody back.
73: a release carries its notes as a file, `WHATSNEW.md`. 74: a board's own name on the
network, and its MAC in `status`.

**Chosen by me, his to change before the run:**

- **The rule for a refused result lives in the library**, where both the Heltec and the
  Walter get it; the Walter's own answer to it (its phase 18, task 06) can go at its next
  sync.
- **The notes are not in the manifest and not signed.** The manifest's format is the
  hardware repository's.
- **The name follows the chip** (`meshmaster-` and four hex digits) and cannot be set.

## Order and dependencies

| # | Task | Depends on | What it leaves behind |
|---|---|---|---|
| 72 | [a-refused-result-holds-nobody-back](72-a-refused-result-holds-nobody-back.md) | — | a post is taken, not reached or refused; a refused result is dropped, counted and said, and the flush goes on; one function says which statuses are a verdict |
| 73 | [what-is-new-is-a-file-of-the-release](73-what-is-new-is-a-file-of-the-release.md) | — | `WHATSNEW.md` uploaded with every release, held to its shape, in `SHA256SUMS` and not in the manifest |
| 74 | [a-name-of-its-own-on-the-network](74-a-name-of-its-own-on-the-network.md) | — | `meshmaster-xxxx` announced to the network; the MAC and the name in `status`; a rail that nothing sets a MAC |

```powershell
.\tasks\run-all.ps1 -Phase '1.22'
```

## Models

- **Opus**: 72, 73, 74.

## Afterwards (Thomas)

- **Release**, import on meshmaster.dk, update Risskov and Aarhus C from their pages.
  With meshmaster.dk's pile 4.49 deployed, the Firmware tab of a board behind the release
  says what is new.
- **The Walter:** its `upstream` workflow, then the sync, which takes 72's change to
  `IUplink` (the report says what its uplink changes).
- **Two boards on one network** under two names in the router's list (74).

## Follow-ups

Review findings go into [FOLLOWUPS.md](FOLLOWUPS.md).
