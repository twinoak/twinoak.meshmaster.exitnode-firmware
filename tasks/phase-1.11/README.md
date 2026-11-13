# Pile 1.11 — a release from the button

Written on 13 November.

**What happened.** Thomas pressed *Run workflow* on `release` twice that morning. Both runs went
green and published nothing: here a run by hand is a dry run, and a release is a pushed tag. On
meshmaster.dk the same button deploys. v0.7.0 then needed a task of its own (1.10/51) to rewrite
the notes inside the workflow, move the changelog and the library's version, and push the tag.

**Decided, 13 November (Thomas):** the firmware repositories release the way meshmaster.dk does.

- **The button publishes.** Nothing is typed. A *dry run* box is there for a rehearsal.
- **The notes write themselves.** A task that changes something a person will notice leaves one
  line, as meshmaster.dk's tasks do. A release is those lines.
- **The version is in the repository.** It names the release that comes next. After a release the
  workflow moves it to the next patch, in a small pull request of its own. A bigger step is one
  line edited beforehand.
- **It refuses when there is nothing to say:** no line since the last release, no release.

The repeater firmware and the Walter firmware get the same, each in its own pile.

**13 November, later: 53 added.** Thomas wants meshmaster.dk to track a repeater's clock and warn
when it is too far out (its pile 4.37). 53 is this firmware's half. It follows the contract of
meshmaster.dk's task 210 and stops at its *First* until that has merged; run it then with
`-From 53`.

## Order and dependencies

| # | Task | Depends on | What it leaves behind |
|---|---|---|---|
| 52 | [a-release-from-the-button](52-a-release-from-the-button.md) | — | *Run workflow* publishes the version the repository names, with notes compiled from the tasks' own lines, and then moves the version on; a dry run by a tick; the tag trigger gone |
| 53 | [the-repeaters-clock](53-the-repeaters-clock.md) | meshmaster.dk 210 merged | every answered login of its own puts the repeater's clock and the board's own time in the job's report |

```powershell
.\tasks\run-all.ps1 -Phase '1.11'
```

## Models

- **Opus** — 52, 53.

## Afterwards (Thomas)

- Nothing to press yet: the only line waiting is this task's own. The first real release from the
  button is the next pile's, and it is the first time the publish and the pull request after it
  run for real. If that pull request does not land, the run says what to do; the release itself is
  out and is not undone.

## Follow-ups

Review findings go into [FOLLOWUPS.md](FOLLOWUPS.md), tagged with the task that must pick them up.
