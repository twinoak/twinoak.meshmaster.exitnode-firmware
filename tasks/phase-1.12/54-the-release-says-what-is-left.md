# 54 — The release says what is left

**Depends on:** nothing in this pile.
**Spec:** `.github/workflows/release.yml` (the last step: `Stop-WithThePullRequestOpen` ~L476–490 with *Two things cause it* ~L486 and its `throw`; the pull request's body ~L510–522 with *No check runs on this pull request* ~L519; `gh pr create` ~L523; `gh pr merge` ~L536), `tools/release.ps1` and its self-test, `docs/release.md` (~L140–150: *the step goes red*), `tasks/phase-1.11/reports/52.md`, `tasks/RUNBOOK.md`; **the Walter firmware, read only, as the model:** its `.github/workflows/release.yml` ~L428–462 (`waiting`: the run stays green and its summary says what is waiting) and its `docs/release.md` (*Expect to merge that pull request by hand*).
**Size:** one session, short.
**Model:** claude-opus-5

## Goal

On 13 November Thomas released v0.7.1 with the button (run 43476713883). The release was right.
The run was red, and its summary named two causes: a setting switched off, or `master` changed
while the run was publishing. Neither had happened. He asked whether it was ok.

What happened, as seen on github.com:

- `master` requires two checks, `build (heltec_v3)` and `native tests`, so the job cannot merge
  its own pull request (#58). That is every release.
- GitHub **does** start a `ci` run for that pull request, and holds it for a maintainer's
  approval (*1 workflow awaiting approval*). Unapproved, it expires after about an hour and is
  listed as failed. The pull request's body says no run starts.
- The step throws, so a release that published is a red run.

## Deliverables

1. **A release that published is a green run.** Where the notes' pull request is open and the
   job cannot merge it, the step ends as the Walter's does: green, with one line at the top of
   the run's summary saying what is left for a person, and which pull request.
   - Still red: the branch could not be pushed, or the pull request could not be opened. Nothing
     is waiting for a person then; something failed.
2. **How the step ends is decided in one small place with a test:** pushed or not, opened or
   not, merged or not, in; the exit and the summary's first line, out.
3. **The summary names the cause that is true:** the checks `master` requires. The two it names
   today stay only where they are what happened: the pull request could not be opened.
4. **The pull request says what is true:** which checks `master` requires; that GitHub holds the
   run for a maintainer's approval; and the two ways to finish, either of which is right: approve
   the run and merge when it is green, or merge without waiting, because the commit touches the
   release's own files and nothing else. A few lines.
5. **The next press still refuses** while that pull request is open, in the sentence it has.
6. **`docs/release.md`** says the same where a person looks after a release.

No line in `release-notes/unreleased/`: nothing a user of the firmware sees changes.

## Acceptance criteria

- [ ] Pushed, opened, not merged: green, and the summary's first line names the pull request (test).
- [ ] Not pushed, and pushed but not opened: red, each with its own sentence (tests).
- [ ] The pull request's body no longer says that no check runs, and the summary no longer offers a switched-off setting as the reason a pull request that exists was not merged; a rail holds both (test).
- [ ] The dry run from this branch is green and published nothing; its run is in the report.
- [ ] Everything `tasks/RUNBOOK.md` §2 names is green. CI green.

## Out of scope

`master`'s rules: a repository setting is Thomas's to change. A release. The library.
