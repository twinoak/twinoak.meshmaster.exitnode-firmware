# 73 — What is new is a file of the release

**Depends on:** —
**Spec:** `.github/workflows/release.yml` (the notes' step ~L281–296; the release's files ~L524–532); `tools/release.ps1` (`-Action notes`); `docs/release.md` (the files of a release). Read only, in `meshmaster.dk`: its `tasks/phase-4.49/262-…md`.
**Size:** a small session.
**Model:** claude-opus-5

## Goal

Every task leaves a line in `release-notes/unreleased/`, and a release prints them in its
GitHub page and nowhere a board's owner looks. meshmaster.dk is to show them where an
update is offered (its task 262) and reads them from a file of the release. This task
publishes that file.

## The file (the same text in meshmaster.dk's task 262)

`WHATSNEW.md`, UTF-8, LF, at most 16 384 bytes. One bullet a task: a line that begins `- `,
with `**bold**` and `` `code` `` and nothing else of Markdown. It is **not in the manifest
and not signed**: text about a release, never a part of what a board trusts.

## Deliverables

1. **`WHATSNEW.md` is uploaded with the release**, beside `manifest.json`: the bullets the
   notes' step already writes, and nothing else in the file (no heading, no version, no
   links the tooling adds). If today's file has more than that, the release's page keeps
   what it has and the uploaded file is the bullets alone.
2. **It is held to its shape before the upload:** every line a bullet or a bullet's
   continuation, the bound, LF, no markup but bold and code. A release whose notes break it
   stops with the line that broke it. `tools/release.ps1`'s own tests hold both.
3. **`SHA256SUMS` lists it.** The manifest does not: say so in `docs/release.md`, with why
   (the manifest is what a board trusts and its format is the hardware repository's).
4. **A dry run** uploads nothing, as today, and prints the file.
5. **The words:** `docs/release.md`'s list of a release's files; a line in
   `release-notes/unreleased/`.

## Acceptance criteria

- [ ] A release carries `WHATSNEW.md`, the bullets and nothing else, and `SHA256SUMS` lists it (the workflow's step; the tooling's test).
- [ ] A line that is not a bullet, a file over the bound and markup beyond bold and code each stop the release with the line named (the tests' names).
- [ ] The manifest and its signature are byte for byte what they would have been (the test or the rehearsal that shows it).
- [ ] `pio test -e native` green, both boards build, the frame rail and the library's host rail (`tools/check-library-host.py`) green, CI green.

## Out of scope

The manifest. Notes of releases already made. meshmaster.dk.
