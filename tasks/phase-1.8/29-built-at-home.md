# 29 — Built away, because it is public

**Depends on:** nothing. **First in this pile:** every later task's CI follows its rule. The slug stays `built-at-home` so that the branch `task/p1.8-29-built-at-home` and its pull request [#32](https://github.com/twinoak/twinoak.meshmaster.exitnode-firmware/pull/32) carry on: **rework that branch, do not start over.**
**Spec:** the branch as the first session left it (`dc0861b` the change, `234c629` the report) and its `tasks/phase-1.8/reports/29.md`; `.github/workflows/ci.yml`, `release.yml`, `upstream.yml`; `tools/check-runners.py` (the guard, and its `--self-test`); `README.md` (*Where CI runs*), `docs/upstream.md`; GitHub's [billing for Actions](https://docs.github.com/en/actions/concepts/billing-and-usage) (*free for standard GitHub-hosted runners in public repositories*) and its [self-hosted runner security](https://docs.github.com/en/actions/security-for-github-actions/security-guides/security-hardening-for-github-actions#hardening-for-self-hosted-runners) (*self-hosted runners should almost never be used for public repositories*).
**Size:** one session, short.
**Model:** claude-opus-5

## Goal

Thomas, 31 October: *all CI is to run on our new Unraid runners*, because the
tests were using up the GitHub credits. The first session of this task moved
`ci.yml` and `upstream.yml` to `[self-hosted, unraid]`, and its jobs then sat
queued with no runner, while the same runners took meshmaster.dk's jobs within
seconds. The runner group is set to *All repositories*, but **this repository
is public**: meshmaster.dk and the repeater firmware are private. A runner
group serves public repositories only when *Allow public repositories* is
ticked. It is not ticked, and it should stay that way, for two reasons:

- **Running here costs nothing.** Actions on standard GitHub-hosted runners
  are free in a public repository, so this repository's CI never used any of
  the credits.
- **On a public repository, a self-hosted runner runs a stranger's code.** A
  fork's pull request can change a workflow, and it would run on the Unraid
  host, with its docker socket, its LAN and its Tailscale. GitHub's own advice
  is not to put self-hosted runners on public repositories.

So every job in this repository stays on GitHub's runners, and the guard the
first session wrote is turned around to say so.

## Deliverables

On the existing branch and pull request #32:

1. **Every job of `ci.yml`, `release.yml` and `upstream.yml` on
   `ubuntu-latest`.** `ci.yml`'s two jobs and `upstream.yml`'s go back.
2. **The caches as they were:** remove the `runner.environment ==
   'github-hosted'` conditions from `ci.yml`'s cache steps (they always run
   hosted now). `release.yml`'s comment at the top gives this reason (a public
   repository) instead of *what ships stays away*.
3. **Keep the one finding:** `upstream.yml`'s clone and pull-request body in
   `$RUNNER_TEMP` rather than `/tmp` is right on any runner.
4. **The guard, turned around.** `tools/check-runners.py` fails, naming the
   file and the job, when any job runs on a `self-hosted` label (any label,
   not only `unraid`). It gives the reason in one sentence: *a public
   repository; a fork's pull request would run on the host*. `--self-test` is
   updated to match. It stays the first real step of the `native` job.
5. **Words:** README's *Where CI runs* says that CI runs on GitHub's runners,
   which are free for a public repository, and why not at home. It also says
   that if this repository ever goes private, the move is in `dc0861b`
   (*CI runs at home*), ready to take back. Change `docs/upstream.md` to match.
6. **Tidy:** cancel the two runs still queued from the first session
   (`41187241749`, `41189315421`). The report replaces the first session's
   account of the stop with this one.

## Acceptance criteria

- [ ] Pull request #32's CI is green on GitHub's runners; the report quotes a *Set up job* log (*Hosted Compute Agent*, the image name).
- [ ] The guard fails on a scratch commit that puts a `ci.yml` job on `[self-hosted, unraid]`. The guard runs in a hosted job, so it can still say so while the moved job sits queued. The scratch commit is reverted, and the report quotes the failure.
- [ ] After merge, `upstream.yml` runs once by `workflow_dispatch` on a hosted runner (this task if the token allows, else the report asks Thomas to).

## Out of scope

The runner group's settings: public repositories stay not allowed. The
private repositories, whose CI is at home already.
