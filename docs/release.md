# The release: one press of a button

*Thomas pressed Run workflow on `release` twice on the morning of 13 November
2026. Both runs went green and published nothing — here a run by hand was a dry
run, and a release was a pushed tag. On meshmaster.dk the same button deploys.*
(Pile 1.11's README, task p1.11-52.)

So the firmware repositories release the way meshmaster.dk does. **The button
publishes.** Nothing is typed into it. A *dry run* box is there for a rehearsal,
and a tag pushed by hand starts nothing at all.

| | Where | Who moves it |
|---|---|---|
| the version | [`VERSION`](../VERSION), one line, at the root | a task, or the release's own pull request |
| a change somebody will notice | `release-notes/unreleased/<task>.md`, one or two sentences | the task that makes the change |
| what a release did | `release-notes/vX.Y.Z.md`, and the GitHub release | the release workflow |
| what a *library consumer* must know | [`CHANGELOG.md`](../CHANGELOG.md) | the task that moves an interface |
| the decisions, with their tests | [`tools/release.ps1`](../tools/release.ps1) | — |
| the build, the images, the publish | [`.github/workflows/release.yml`](../.github/workflows/release.yml) | — |

## 1. The version is in the repository, and it names the next release

`VERSION` holds one line, `X.Y.Z`, and it is the **release that comes next** —
not the one that was last made. Today it says `0.7.1`, and the next press
publishes `v0.7.1`.

Three manifests carry a copy of that number, because a consumer of
`lib/agent-core/` reads them rather than this file:

- `lib/agent-core/include/agent/Version.h` (`AGENT_CORE_VERSION`, which the
  firmware can print),
- `lib/agent-core/library.json`,
- `lib/agent-core/idf_component.yml`.

**A copy that disagrees is a red build**, twice over: `test/test_library`'s
`TheVersionIsTheOneTheRepositoryNames` holds the number the library was
*compiled* with against the file on disk, and `tools/release.ps1 -Action check`
— a step of CI's native job — holds all three plus `CHANGELOG.md`'s top heading.
The release workflow reads the version from `VERSION` and from nowhere else.

**A bigger step than a patch** — `0.8.0`, `1.0.0` — is one line edited
beforehand, in a pull request of its own, moving all four together. After a
release the workflow moves them to the next **patch** by itself.

The version the *board* reports is still the release's and comes from the build
(`-D MESHMASTER_AGENT_VERSION="X.Y.Z+<sha7>"`, `src/version.cpp`), with the
commit beside it, as it always has.

## 2. A task leaves a line

**A change somebody will notice has a line. A change nobody will has none.**

```
release-notes/unreleased/52.md
```

`<id>` is the task number, or any short name for a change made by hand. The file
holds **one paragraph** — one or two sentences, at most 400 characters:

- about **what a person who runs an exit node will notice**, not about what was
  done. *A repeater that answers slowly is no longer reported as having lost its
  access-list entry.* — not *`LoginSequence` drops a non-login response*;
- plain words. No heading, no list marker, no quote: the release makes the
  bullet. Emphasis and a `code span` are fine, and the lines of a release read
  better when the first few words are bold;
- it **stands on its own**. It is read in a GitHub release by somebody who will
  never see the task.

A test, a refactoring, a workflow, a document, a dependency bump that changes no
behaviour: **no file**. If you are not sure, ask what a person with a board would
do differently after flashing it. If the answer is *nothing*, there is no line.

`release-notes/unreleased/.gitkeep` keeps the directory through a release that
empties it; leave it alone. `tools/release.ps1 -Action check` holds every waiting
file to the format on every pull request, so a line that breaks it is a red check
and not a surprise during a release.

This is **not** `CHANGELOG.md`. That file is for the *other* consumer of this
repository — a second firmware that pins `lib/agent-core/` at a tag — and it
answers four questions about interfaces, constants and fixtures. Most tasks write
one of the two, and some write neither.

## 3. The press, and what it refuses

*Actions → release → Run workflow.* Two boxes, and neither needs filling:

| | |
|---|---|
| **dry run** | the same build, published nowhere, `0.0.0-dryrun+<sha7>`. The images are the run's artifacts |
| **ref** | a dry run's commit or branch. A real release is always `master`'s head and refuses anything else |

Before anything is built, the run refuses in one sentence when:

- **no line is waiting.** *There is nothing to release.* A release with nothing
  to say is not made;
- **the tag of the version already exists.** That means the version was not moved
  on after the last release — the pull request below is still open — and the
  sentence says how to finish it;
- **a release is asked for from a ref that is not `master`.**

A dry run refuses none of them and prints what a real release *would* have
refused, so that a rehearsal rehearses that too.

Then the gate it has always had: **nothing is released whose `ci` is red or
unknown**, waited for with the same twenty minutes as before.

## 4. What it publishes

Per board, the application image and the merged image, with `SHA256SUMS` — see
[`README.md`](../README.md)'s *Which of the two*. The notes are two halves:

- ***What's new in vX.Y.Z*** — the waiting lines, as bullets, **in the order the
  tasks merged**. That order is read off git: the commit that added each file
  along `master`'s first parent, which for a `--no-ff` merge of a task branch is
  the merge itself;
- **everything else** — the flashing table, the first-install and upgrade
  paragraphs, the console lines that set up WiFi, the token, the observer and the
  flood scope. That is *standing text*, it lives in `release.yml`, and a task
  that changes what a person types edits it there.

The release is created with its tag on `master`'s head, as **Latest**.

## 5. Afterwards: one pull request, opened and merged by the workflow

With the release out, the run opens `release/vX.Y.Z` and merges it:

- the lines move from `release-notes/unreleased/` into `release-notes/vX.Y.Z.md`,
  as published;
- the version moves to the next patch in all four places of §1;
- `CHANGELOG.md`'s next section opens above the released one.

**The commit touches the release's own files and nothing else.** That is not
tidiness: it is what lets the *next* release trust it. No check runs on that pull
request — GitHub starts no workflow from an event raised with the job's own
token — so the commit it puts on `master` has no `ci` run of its own, and the
next press would wait twenty minutes for a run that is never coming. The gate
therefore asks `tools/release.ps1 -Action ci-commit` which commit to read `ci`'s
verdict from: it walks `master`'s head back over commits that are **the
`github-actions` bot's** *and* touch **nothing but** `VERSION`, `CHANGELOG.md`,
`release-notes/` and the three manifests, and stops at the first commit that is
anything else. Nothing that can change what is built is ever walked over; the
build and the tag still use `master`'s head.

**Where `master`'s rules do not let the workflow merge it**, the pull request is
left open, the run's summary says so, and the step goes red. The release stands
and nothing needs undoing — and the next press refuses, *the tag of the version
already exists*, until somebody merges it by hand or moves the version
themselves. The one repository setting it depends on is **Settings → Actions →
General → Allow GitHub Actions to create and approve pull requests**.

## 6. The changelog at the tag

`CHANGELOG.md`'s top section is headed with the version `VERSION` names, so that
the file **reads right at the tag**: at `v0.7.1` the first heading says `v0.7.1`.
It used to say *Unreleased* there, which is what a consumer pinning the tag would
have read.

Two things read that file by machine, and both keep working:

| Reader | What it reads | After this |
|---|---|---|
| the Walter's upstream watch (`twinoak.meshcore.walter-firmware`, `.github/workflows/upstream.yml`) | `git show <tag>:CHANGELOG.md`, the **first** `## ` section, into the bump's pull request body | it quotes `## v0.7.1` and that release's four answers, where it used to quote *Unreleased* |
| the Walter's `tools/upstream-check.ps1` | not this file at all — `AGENT_CORE_VERSION` in its copy, against this repository's `v*` tags | unchanged |

`tools/new-sync-task.ps1` in **this** repository reads nothing from
`CHANGELOG.md`: it reads `[meshcore].pin` out of `platformio.ini` and the tag it
is given. Nothing there changed either.

## 7. The script, and its self-test

Every decision above is in [`tools/release.ps1`](../tools/release.ps1) and not in
shell inside the workflow, so that it can be run twice and tested:

```powershell
.\tools\release.ps1 -Action check        # the rails; CI runs this on every pull request
.\tools\release.ps1 -SelfTest            # the whole of a release, against a scratch repository
.\tools\release.ps1 -Action notes        # what this release's What's new would say, now
```

`-SelfTest` builds throwaway repositories in the temporary directory and walks
every case: the rails, the format of a line, each of the three refusals, a dry
run that refuses nothing, two lines compiling in merge order, the move after a
release, and the commit the `ci is green` gate reads. CI runs it in the native
job.
