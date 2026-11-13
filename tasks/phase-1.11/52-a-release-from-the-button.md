# 52 — A release from the button

**Depends on:** nothing.
**First:** read only. `gh api repos/twinoak/twinoak.meshmaster.exitnode-firmware/actions/permissions/workflow` must say the workflow's own token may create pull requests. If it may not, write `logs/STOPPED-52.md` naming the setting (*Settings → Actions → General → Allow GitHub Actions to create and approve pull requests*), change nothing else, and end: a setting is Thomas's to change.
**Spec:** `.github/workflows/release.yml` (the two triggers; the version from the tag ~L85–95; *What's new* written into the file ~L228; `gh release create` ~L380), `.github/workflows/ci.yml`, `CHANGELOG.md` (*Unreleased*, the four questions), `lib/agent-core/include/agent/Version.h` and `library.json` and whatever else 51 moved to 0.7.0, `tasks/RUNBOOK.md` (~L125: *Merging never releases*), `tools/new-sync-task.ps1` and `docs/library.md` (what a consumer that pins a tag reads), `tasks/phase-1.10/51-v0-7-0.md` and its report (everything a release needed by hand); **meshmaster.dk, read only, as the model**: its `.github/workflows/release.yml` (*Compile the release notes* ~L168; the pull request that files them, opened and merged by the job's own token, ~L341–410), `tools/ci/Compile-ReleaseNotes.ps1`, `release-notes/unreleased/`, and its `tasks/RUNBOOK.md` on a task's release note.
**Size:** one session, a long one.
**Model:** claude-opus-5

## Goal

See the README. A release of this firmware has needed four things done by hand or by a task: the
*What's new* text edited inside the workflow, the changelog's heading, the library's version in
its manifests, and a tag pushed. After this task it needs one press.

## Deliverables

1. **The version is in the repository, in one place, and it names the next release.** Today that
   is `0.7.1`. The manifests that carry the library's version agree with it, and a rail in CI
   fails when one does not. The workflow reads the version from there and from nowhere else; the
   board reports it with the commit as now.
2. **A task leaves a line.** `release-notes/unreleased/<task>.md`: what a person who runs an exit
   node will notice, in a sentence or two. `tasks/RUNBOOK.md` gets the rule in meshmaster.dk's
   words: a change somebody will notice has a line, a change nobody will has none.
3. **`workflow_dispatch` publishes**, from `master`'s head and no other commit:
   - CI green for that commit, as now;
   - *What's new* is the unreleased lines, in the order the tasks merged. The standing parts of
     the notes (the flasher, the table of files, the console lines) stay where they are;
   - the release is created with its tag on that commit, as **Latest**;
   - a tick box **dry run** does what a run by hand does today: the same build, nothing
     published, `0.0.0-dryrun`. Only a dry run may name another `ref`.
4. **It refuses, before anything is built, and says why in one sentence:**
   - no unreleased line: there is nothing to release;
   - the tag of the version already exists: the version was not moved on after the last release,
     and the sentence says how to finish that;
   - a release asked for from a ref that is not `master`.
5. **After the release is out, one pull request, opened and merged by the workflow:**
   - the lines move from `unreleased/` into `release-notes/vX.Y.Z.md`, as published;
   - the version moves to the next patch, in every place deliverable 1 names;
   - the changelog: decide how its top section is headed so that the file reads right **at the
     tag** for a consumer that pins it (today it would say *Unreleased* there), and so that this
     pull request opens the next one. Say what `tools/new-sync-task.ps1` and the Walter's sync
     read from it, and keep that working.

   Where `master`'s rules do not let the workflow merge it, the pull request is left open and the
   run's summary says so. The release stands either way, and the next one refuses until the
   version has moved (deliverable 4).
6. **The commit that pull request makes** touches only the release's own files. Say how the
   *ci is green* gate reads it at the release after: it has no CI run of its own.
7. **The tag trigger goes.** One way to release. A tag pushed by hand starts nothing.
8. **The parts that can be tested are.** Compiling the lines, the refusals and the move after a
   release are a script with tests of its own, run by CI, against a scratch repository. A dry run
   from this task's branch is in the report.
9. **The words.** `tasks/RUNBOOK.md`, `docs/library.md` where it says how a tag is made, README;
   and this task's own line in `release-notes/unreleased/`.

This task publishes nothing and pushes no tag.

## Acceptance criteria

- [ ] The version is read from one place; a manifest that disagrees fails CI (test).
- [ ] Two unreleased lines compile into *What's new* in merge order; none is a refusal with its sentence (tests).
- [ ] An existing tag of the version, and a ref that is not `master`, are each refused before the build (tests).
- [ ] After a release in a scratch repository: the lines are in `release-notes/vX.Y.Z.md`, `unreleased/` is empty, every place that carries the version says the next patch, and the changelog reads right at the tag and after (test).
- [ ] The dry run from this branch is green and published nothing; its run is in the report.
- [ ] `release.yml` has no tag trigger, and `tools/check-runners.py` still passes.
- [ ] The runbook's verification passes on this machine (`tasks/RUNBOOK.md` §2). CI green.

## Out of scope

A release. The boards. The Walter's and the repeater's repositories, which get the same in their
own piles.
