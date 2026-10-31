# 29 — Built at home

**Depends on:** nothing. **First in this pile:** until it lands, every pull request's CI runs on GitHub's runners.
**Spec:** this repository's `.github/workflows/ci.yml` (2 jobs: the board matrix, the native tests), `upstream.yml` (the weekly MeshCore check), `release.yml` (3 jobs: the gate, the board builds, the release); the pattern, meshmaster.dk's move of 29 October (read-only, its `master` on GitHub: commits `7d84fc9` *Added unraid job runner*, `b7c9bd9` *Moved all runners to unraid*, `35a5d2a`, `65ce46a`, `70b73ad`), and its `.github/workflows/ci.yml` as it stands there; `tasks/RUNBOOK.md` (what *CI green* means for a task); wherever `README.md` or `docs/` describe CI and the release.
**Size:** one session, short.
**Model:** claude-opus-5

## Goal

Thomas, 31 October: *all CI is to run on our new Unraid runners.* GitHub-hosted
minutes got out of hand. The runners are self-hosted at the organisation level
(`myoung34/github-runner`, label **`unraid`**), and meshmaster.dk moved its CI
to them on 29 October. Every job here still says `runs-on: ubuntu-latest`, so
every pull request a task opens costs hosted minutes.

What meshmaster.dk learned on the way, which applies here too:

- `runs-on: [self-hosted, unraid]`.
- **A self-hosted runner keeps its home between jobs**, so `actions/cache`
  would download a tarball over the WAN every job for something already on
  disk. Its cache steps run only `if: runner.environment == 'github-hosted'`.
- **The runner shares the Unraid host**. Anything a job starts there is
  cleared at the start of the job and taken down `if: always()`, and a fixed
  port the host already uses is not available.
- **pwsh, gh, jq, node, curl and docker are on the runners**, because its CI
  uses them there.

**What ships is built away from home.** The runner is shared with every branch
a task session pushes, and keeps its home, including `~/.platformio` and its
toolchains, from one job to the next. A release built there is only as clean
as the last branch that ran there. So `release.yml`, which builds the images
people flash and publishes them, stays on GitHub's runners. It runs at a tag,
a few times a month, so its minutes are few. Everything that **checks** goes
home; everything that **ships** stays.

## Deliverables

1. **Home:** both jobs of `ci.yml`, and `upstream.yml`'s job, run on
   `[self-hosted, unraid]`.
2. **Away:** all three jobs of `release.yml` stay on `ubuntu-latest`, with a
   two-line comment at the top of the file saying why.
3. **Caches as meshmaster.dk has them:** in `ci.yml`, the `~/.platformio` /
   `~/.cache/pip` cache steps run only on GitHub's runners, with a comment
   saying why. `release.yml` keeps its caches: it runs on GitHub's runners.
   `setup-python` and the pinned `pip install platformio==${PIO_VERSION}`
   stay.
4. **Nothing left behind, nothing shared:** the board matrix and the native
   tests now run at once on one host, on two runners. Check that no job uses a
   host port or writes outside its workspace and the runner's home, and that
   checkout's clean (the default) leaves no `.pio/` from an earlier job. Where
   something does, fix it in the job.
5. **The guard.** A step early in `ci.yml` (pwsh or python, as the workflows
   already use) checks every job in `.github/workflows/`. It fails, naming the
   file and the job, when a job outside `release.yml` runs anywhere other than
   `[self-hosted, unraid]`, or when a job of `release.yml` runs anywhere other
   than GitHub's runners. The rule and its reason sit in one place, so neither
   change happens by accident.
6. **Words:** where the README or `docs/` describe CI and the release: where
   each workflow runs and why, why the caches are skipped at home, and what a
   runner must have (python through `setup-python`, pwsh, gh, jq, node, git).

## Acceptance criteria

- [ ] This task's own pull request runs both `ci.yml` jobs on a runner labelled `unraid`; the report quotes the runner names from the runs' *Set up job* logs.
- [ ] Both boards build and the native tests pass there. The guard passes; it fails on a scratch commit that puts a `ci.yml` job on `ubuntu-latest`, and on one that puts a `release.yml` job on `[self-hosted, unraid]` (both reverted; the report quotes the failures).
- [ ] After merge, `upstream.yml` runs once by `workflow_dispatch` at home (this task if the token allows, else the report asks Thomas to).

## Out of scope

The runners themselves (Thomas's stack on Unraid). The build itself: flags,
boards, MeshCore's pin. What the release does.
