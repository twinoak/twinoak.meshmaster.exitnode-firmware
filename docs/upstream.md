# Keeping up with MeshCore

This firmware is built **against** MeshCore, not forked from it. The pin is two
lines of `platformio.ini`:

```ini
[meshcore]
pin = d92964352441e53b93e8667b802e04f6e072b39e
tag = companion-v1.17.1
```

`d9296435` is `companion-v1.17.1`, the commit the field repeater runs. MeshCore
moves — a companion release every few weeks — and this page is how this
repository moves with it. Three tiers, and the first one is the common case.

## Why a dependency and not a fork

Because of this page. A fork has to be *merged*, and a merge of an upstream
release touches files that are not ours — and every one of those conflicts is a
question nobody can answer from here. A pin is a line: `pin = <sha>`, and the
only thing that can go wrong is that our own code no longer compiles against
theirs, which is exactly the thing CI can tell us by itself.

The price is that a consumer of the package does not get everything a checkout
of MeshCore's own tree gets, and three things had to be arranged by hand. They
are the places a bump re-reads:

| What | Where | Why |
|---|---|---|
| the flag block | `platformio.ini`, `[esp32_base]` and `[env:heltec_v3]` | MeshCore's `[arduino_base]`, `[esp32_base]`, `[sensor_base]` and `variants/heltec_v3/platformio.ini` use paths relative to its own root, so they cannot be included from here; they are copied |
| the library versions | `platformio.ini`, `[libs]` | pinned exactly, where upstream uses ranges |
| `lib/ed25519` | `tools/pio/meshcore_private_libs.py` | MeshCore keeps it as a *private* library of its project, which a package consumer does not get; the sources are compiled straight out of the pinned checkout and the include path is a `-I` in `platformio.ini` |

Everything else — `src/`, `variants/`, `examples/` — the package gives us, and
`build_as_lib.py` in MeshCore's own tree is what selects from it (our
`MC_VARIANT`, `BUILD_EXAMPLE` and `MC_UI_FLAVOR` defines drive it).

## Tier one — the weekly pull request (automatic)

[`.github/workflows/upstream.yml`](../.github/workflows/upstream.yml), Monday
mornings and on demand:

1. `tools/upstream-check.ps1` reads `git ls-remote --tags` of
   `meshcore-dev/MeshCore` and compares the newest `companion-vX.Y.Z` with the
   pin. Nothing newer: it says so and stops.
2. Something newer: a branch `upstream/<tag>` that moves the pin, pushed, and a
   pull request **MeshCore \<tag\>** whose body is `git log --oneline
   <old>..<new>` of upstream's tree — what changed, in their words.
3. CI runs on it like any pull request. **Green means the bump compiled and
   every native test still passes.** A person merges it, or nobody does if
   auto-merge is ever turned on for the `meshcore` label.

**One click before CI runs.** The pull request is opened by
`github-actions[bot]` with the workflow's own `GITHUB_TOKEN`, and GitHub holds
workflows on such a pull request as *action required* rather than starting them
— its guard against a workflow that sets itself off. So the run waits for
**Approve and run** on the Actions tab (or
`gh api -X POST repos/twinoak/meshmaster-agent-firmware/actions/runs/<id>/approve`),
and that click is the same click as reading the bump. What would remove it is a
personal access token in a repository secret for `gh pr create` to use instead
of the built-in one; that is Thomas's to decide, and one button a month is
cheaper than a credential to rotate.

Only one bump is open at a time: if `upstream/<tag>` is already on `origin`,
the workflow leaves it alone.

You can ask the same question yourself, at any time:

```powershell
.\tools\upstream-check.ps1                          # against the pin
.\tools\upstream-check.ps1 -Tag companion-v1.16.0   # as if the pin were two tags back
```

And you can rehearse the whole workflow without waiting for upstream: *Run
workflow* with `pin_tag = companion-v1.17.0` pushes a throwaway base branch
pinned at that tag and opens the real pull request on top of it. Close it and
delete both branches afterwards.

## Tier two — a red bump is one command

CI red on such a pull request is the case that needs a session, and there is one
command for it:

```powershell
.\tools\new-sync-task.ps1 -Tag companion-v1.18.0 -Pr 12 -LogUrl <the failing job>
.\tasks\run-all.ps1 -Phase sync
```

It writes `tasks/sync/NN-meshcore-<tag>.md` from
[`tasks/sync/TEMPLATE.md`](../tasks/sync/TEMPLATE.md) — the tag, the pull
request, the failing log, and the standing instructions: read what upstream
changed, re-read every place this firmware cites a MeshCore line at the old pin
(a grep for the old sha finds the ones that say so out loud), bring the code to
the new API, re-run the fixture and frame tests **against the new tag's
source**, refresh the three copied things in the table above, and say in the
report what upstream changed that mattered. The session works on the branch the
workflow already pushed.

The template also lists the rails that must survive a bump, because a bump is
exactly when one gets quietly lost: verified TLS, secrets where they are,
nothing overheard logged as text, one send per command, and the contract
fixtures unchanged.

## Tier three — a change to the wire protocol

Rare, and not a firmware change first. If MeshCore changes a **frame** — a
field's width, a member's order, a new reply code — then the server's reading of
it changes too, and the server is the reference:

1. **meshmaster.dk first.** `MeshMaster.MeshCore` and the captures and fixtures
   under `tests/fixtures/companion/` are where the new reading is established
   and tested, and `tests/fixtures/contracts/` is regenerated if an envelope
   moved.
2. **Here second.** `tools/sync-fixtures.ps1 -Commit <that commit>` brings the
   fixtures across, and the firmware is brought to them.

The order is not negotiable: there is one dialect, it is the .NET agent's, and a
firmware that invents a second one is a bug however well it works. See
[`tests/fixtures/contracts/README.md`](../tests/fixtures/contracts/README.md).

## Reading the firmware you build against

After a build, the pinned MeshCore is on disk:

```bash
.pio/libdeps/heltec_v3/MeshCore/
```

That checkout — not a memory of it, not a transcription in a task file — is what
a claim about the companion firmware is checked against. `git log`, `git show`
and `grep` all work in it; it is a real clone at the pinned commit.
