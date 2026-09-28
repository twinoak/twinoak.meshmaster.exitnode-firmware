# {{NN}} — MeshCore {{TAG}}

**Depends on:** nothing. The branch already exists: `upstream/{{TAG}}`.
**Written by:** `tools/new-sync-task.ps1 -Tag {{TAG}}` on {{DATE}}.
**Size:** one session.
**Model:** claude-opus-5

## What happened

`.github/workflows/upstream.yml` saw MeshCore's `{{TAG}}` and opened
**{{PR}}** moving the pin from `{{OLD_PIN}}` to `{{NEW_PIN}}`. CI on that pull
request is **red**, which is the only case that needs a session: a green bump
is merged by a person (or by auto-merge) and costs nobody anything.

The failing job:

{{LOG}}

## The task

Bring this firmware to `{{TAG}}` on the branch the workflow opened.
`git checkout upstream/{{TAG}}` (it is on `origin`) and work there — do not
branch from `master` and do not move the pin a second time.

1. **Read what upstream changed.** The pull request body lists MeshCore's
   commits between the two pins. In the checkout,
   `git -C .pio/libdeps/heltec_v3/MeshCore log --oneline {{OLD_PIN}}..{{NEW_PIN}}`
   says the same thing with the diff attached.
2. **Re-read every place this firmware cites a MeshCore line at the old pin.**
   `grep -rn "{{OLD_PIN_SHORT}}" --include=*.cpp --include=*.h --include=*.md --include=*.ini .`
   finds the ones that say so out loud; the ones that do not are found by
   reading the failing compiler output and `docs/upstream.md`'s list of the
   places a bump touches.
3. **Bring the code to the new API.** A rename upstream is a rename here; a
   changed field order in a companion frame is a changed reader here. What is
   *not* allowed is to work around a compile error by loosening a rail.
4. **Re-run the fixture and frame tests against the new tag's source.** A test
   that encodes or decodes a MeshCore frame is only as right as the source it
   was written from: check each one against
   `.pio/libdeps/heltec_v3/MeshCore` at `{{NEW_PIN}}`, not against the old
   reading of it.
5. **Update the flag blocks.** `platformio.ini` carries a copy of MeshCore's
   `[arduino_base]`, `[esp32_base]`, `[sensor_base]` and
   `variants/heltec_v3` flags, and `[libs]` a copy of its library versions.
   Diff them against the new tag and bring across what changed, with a line in
   the report for anything deliberately not brought across.
6. **Verify**: `pio run -e heltec_v3` (every board environment), then
   `pio test -e native`. Green, then push to `upstream/{{TAG}}`; the pull
   request is already open, so CI runs on the push.

## The rails that must survive this bump

Every one of these is in the repository README, and a bump is exactly when one
gets quietly lost:

- **TLS is verified.** No `setInsecure()` and no cousin of it, at any pin.
- **Secrets stay where they are.** The token in NVS and printed by nothing; a
  repeater's password in RAM for the job and overwritten after; the serial log
  redacting as the .NET agent does.
- **Nothing the mesh says is logged as text.** Overheard channel and contact
  messages counted and dropped. `MESH_PACKET_LOGGING` and `MESH_DEBUG` stay
  off, whatever upstream's examples do.
- **One send per command.** A `set` sent twice is a change made twice; strictly
  increasing sender timestamps; the region loader's terminator.
- **The contract is the .NET agent's.** The fixtures under
  `tests/fixtures/contracts/` are meshmaster.dk's bytes. A MeshCore change
  that seems to need a different envelope is a meshmaster.dk change first
  (`docs/upstream.md`, the third tier) and never a second dialect here.

## Acceptance criteria

- [ ] `pio run -e heltec_v3` green at `{{NEW_PIN}}`, locally and in CI.
- [ ] `pio test -e native` green, and every test that reads a MeshCore frame re-checked against the new tag's source.
- [ ] The five rails above still hold, each with the test or grep that says so named in the report.
- [ ] The report says what upstream changed that mattered — not "the pin moved", but which of their commits this session had to answer.

## Out of scope

Anything that is not this bump. A bug found on the way gets a line in
`tasks/phase-1/FOLLOWUPS.md`, tagged for the task that owns it.
