# Runbook — one task, unattended

You are Claude Code running headless (`claude -p`): nobody is watching, nobody
answers questions. `tasks/run-all.ps1` started you with "Run task NN of phase
P". Everything below uses `tasks/phase-P/` for that phase. Do exactly this,
in order. Everything you decide goes into files in the repo, never into the
console.

This is twinoak/meshmaster.dk's runbook, in PlatformIO. **The spec lives in
that repository**, not this one: the task files link into it at a commit, and
those links are what "read the spec section in full" means here.

## 0. Orient

- You are on `master`, clean and up to date (the driver checked). If not:
  `git checkout master && git pull --ff-only`.
- Read `tasks/README.md`, `tasks/phase-P/README.md`, then
  `tasks/phase-P/NN-<name>.md`, then every spec section that task references,
  in full — they are links into `twinoak/meshmaster.dk`; fetch them. The spec
  wins over the task file; note any disagreement in the report.
- Read `tasks/phase-P/FOLLOWUPS.md`. Every open item tagged `[NN]` for your
  task is part of your task. Items tagged for other tasks are not yours; do
  not tick anything — the reviewer does.
- Read `tasks/phase-P/reports/*.md` for the tasks yours depends on: their
  "deviations" are what you inherit. The bootstrap's own report is
  `tasks/phase-4.2/reports/65.md` in twinoak/meshmaster.dk — read it before
  touching `platformio.ini`, CI or the release.
- Read `docs/upstream.md` before changing anything that cites a line of
  MeshCore. The pin is in `platformio.ini`'s `[meshcore]`, and the source it
  names is on disk under `.pio/libdeps/<env>/MeshCore` after a build — read
  the firmware you build against rather than trusting a transcription of it.

## 1. Branch

`git checkout -b task/pP-NN-<name>` from `master`. One task, one branch.

**If that branch already exists** (locally, or on `origin` — check both), a
previous session of this task was cut off: a usage limit, a crash, a closed
lid. Do not delete or reset it and do not start over. `git checkout` it,
`git pull` if it has a remote, read `git log master..HEAD` and any draft of
`reports/NN.md` — the reviewer may have moved one to
`tasks/phase-P/logs/NN-report-draft.md`, which is git-ignored so that an
unfinished report cannot reach `master` and make the driver think the task
landed — run the build and the tests to see where it stands, and continue
from there. The commits already made are the task's; yours are added after
them. Say in the report that the task was done in more than one session.

## 2. Implement and verify

- The conventions in `tasks/README.md` apply: spec wins, no scope creep,
  libraries pinned to an exact version with a one-line justification, secrets
  never in the repo, docs move with code, leave the evidence.
- Run the task's verification commands, plus **`pio run -e heltec_v3`** (every
  board environment if the task added one) and **`pio test -e native`**. Fix
  until green. `-Werror` is on for our own sources (`build_src_flags`), which
  is this repository's `-warnaserror`: a warning in `src/` is a red build.
- **No native compiler on Windows.** `pio test -e native` is a host build and
  this machine has no `g++`. Run it in a container instead — the image pulls
  once and the whole suite is seconds after that:

  ```bash
  docker run --rm -v "C:/Users/<you>/path/to/meshmaster-agent-firmware:/w" -w /w python:3.11-slim \
    bash -c "apt-get update -qq && apt-get install -y -qq g++ git && pip install -q platformio && pio test -e native"
  ```

  In Git Bash, prefix that with `MSYS_NO_PATHCONV=1` or the `-w /w` is
  mangled into a drive letter. CI runs the same command on Linux with the
  compiler already there, so a container run is a rehearsal of CI and not a
  substitute for it.
- **Run the long commands in the foreground and let them finish inside the
  same tool call.** A clean `pio run` fetches a toolchain and every library
  and takes minutes; raise that call's timeout to suit. Never start one in the
  background and then end your turn to wait for a completion notification: a
  headless session with nothing left to do *ends*, and the task dies there —
  the run unfinished, the report unwritten, the branch unpushed. Block on it
  inside the turn, or poll it inside the turn.
- **A session cannot flash a board or hear a radio.** It can build, run the
  native tests, and read the MeshCore source it builds against. Everything
  else is Thomas's to walk with the board on the bench, so every task ends
  with a *Not verified here* list precise enough to be walked, and the
  firmware prints enough on its serial console that the walk is a matter of
  reading.
- Nobody will answer a question. Where the spec and the task are silent, take
  the smallest decision consistent with the spec and record it in the report
  under "Decisions taken without asking".
- Never: `git push --force`, delete a branch other than your own, write a
  secret anywhere (including this report and the logs), or add `setInsecure()`
  and its cousins to the tree — TLS is verified, and a test greps for them.

## 3. Report

Write `tasks/phase-P/reports/NN.md` and commit it on the branch. It is the
PR description and what the reviewer reads:

```
# pP-NN — <name>

## Acceptance checklist
- [x] <criterion> — <one line: how it was verified>
...
## Deviations from the spec or the task
## Decisions taken without asking
## New libraries (with the one-line justification)
## Not verified here (and why)      <- the bench list: what Thomas must do with the board
```

## 4. PR, CI, merge

**With `gh` (installed and logged in):**

1. `git push -u origin task/pP-NN-<name>`
2. `gh pr create --title "pP-NN: <name>" --body-file tasks/phase-P/reports/NN.md`
3. `gh pr checks --watch --fail-fast`. Red: `gh run view --log-failed` on the
   failing run, fix, commit, push, watch again. At most three rounds; still
   red is a stop condition (§5).
4. Green: `gh pr merge --merge --delete-branch`, then
   `git checkout master && git pull --ff-only`.

**Without `gh`:** after the local verification is green,
`git checkout master && git pull --ff-only && git merge --no-ff
task/pP-NN-<name> -m "Merge task/pP-NN-<name>" && git push && git branch -d
task/pP-NN-<name>`. CI runs on `master`; the reviewer watches it.

The driver considers the task done when `tasks/phase-P/reports/NN.md` exists
on `origin/master`. Nothing else counts. Merging never releases: a release is
a person pushing a tag `vX.Y.Z` (or pressing *Run workflow* for a dry run),
and what it publishes is in `docs/upstream.md`'s neighbour,
`.github/workflows/release.yml`.

## 5. Stop conditions

Stop — do not merge, do not continue — when: CI is still red after three
rounds; an acceptance criterion cannot be met; a step needs a secret or a
human (a board on a bench, a GitHub secret, a certificate); neither a
compiler nor Docker is there for `pio test -e native`. Then: leave the branch
pushed (PR open if `gh` exists), write `tasks/phase-P/logs/STOPPED-NN.md`
(git-ignored) saying exactly what is missing and what was completed, and end.
The driver stops the run and the human reads that file.

## Phase-specific

**Phase 1.** Task 01 is the foundation: where the token lives, how the
server's certificate is checked and what the serial console will and will not
print are decided there and inherited by everything after it. The rails in
[`README.md`](../README.md) hold in every task — verified TLS, secrets in NVS
and RAM only, nothing overheard logged as text, one send per command, the
contract is the .NET agent's — and each of them has a test or a grep, not a
promise.

**The sync phase** (`-Phase sync`). A task under `tasks/sync/` is a MeshCore
bump that CI could not swallow. `docs/upstream.md` is the page; the task file
names the tag, the pull request and the failing job, and its standing
instructions are the template's. The pin lives in `platformio.ini`; the
branch is `upstream/<tag>` and it already exists — check it out rather than
branching from master.
