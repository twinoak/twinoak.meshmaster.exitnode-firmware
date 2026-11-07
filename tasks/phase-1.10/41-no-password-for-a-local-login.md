# 41 — No password for a local login

**Depends on:** 40 (the same virtual says the job is local).
**Spec:** `lib/agent-core/src/LoginSequence.cpp` (`loginMaterialFor` ~L27: at `admin` with no admin password in the claim it answers `nullptr`, *nothing to log in with*), `lib/agent-core/include/agent/LoginSequence.h`, `lib/agent-core/src/JobRunner.cpp` (`loginMaterial` ~L156, `ensureSession` ~L285, `restoreAcl`), `test/test_rotation_probe` (where every branch of the material decision is walked); **read only, from the sibling repositories**: the Walter firmware's `components/agent/link_mesh.cpp` (`localLogin`: a login to the unit's own repeater is answered by the board at admin, whatever material it carries) and `tasks/phase-8/FOLLOWUPS.md` (*An admin job for the unit's own repeater needs a password nobody reads*); meshmaster.dk's `src/MeshMaster.Core/Transport/AgentProtocol.cs` (`DecideAsync`, the comment *Not at `admin`, yet*).
**Size:** one session.
**Model:** claude-opus-5

## Goal

On 7 November, meshmaster.dk refused a unit's scheduled `config.read` of its own repeater with *no
admin password stored* (job 22016). The server hands the unit a stored admin password for its own
repeater only because this library asks for one. `loginMaterialFor` answers `nullptr` at `admin` when
the claim carries none, and `ensureSession` then reports `unauthorized` without sending anything. It
decides that before it knows the job will be answered locally, and the local door answers the login
at admin whatever it is sent.

So every CLI job of a unit's own repeater needed a password that nothing reads, and a radio whose
password nobody knows (a fresh one) could not be configured through its own unit.

## Deliverables

1. **A local job's login needs no material.** When the mesh says the job is local (40's virtual),
   the material is the empty string at every class, with a note in the job's log (*answered by the
   board, no password needed*), and never `nullptr`.
   - A password the claim did carry is still not logged, and is not sent anywhere it was not before.
   - For a job that is not local, nothing changes: `admin` with no password still refuses, as now.
2. **`restoreAcl`** is checked once more: it stays gated on the level the claim carried, and does
   nothing for a local job that carried none.
3. **Tests.**
   - `test_rotation_probe`, or the test that walks the material decision, gains the local branches:
     admin, guest, acl and none, each with and without material.
   - A runner test runs an `admin`-class local job with no password to its end.
4. **`docs/library.md`:** the material decision, with the local branch.

## Acceptance criteria

- [ ] A local job logs in with no material at every class, and a non-local `admin` job without a password still refuses (tests).
- [ ] `restoreAcl` does nothing for a local job without a level (test).
- [ ] Docs updated; the build and the native tests green.

## Out of scope

The Walter's side (its pile 9). meshmaster.dk's side (its pile 4.27): it stops requiring a stored
admin password for a unit's own repeater once the unit's Walter has this.
