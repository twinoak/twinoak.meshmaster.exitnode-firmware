# Pile 1.4 — follow-ups

## Review of 14 (13 October) — the pile, after it landed

Read after PR #16 merged and `v0.1.4` was tagged, against the report and
the code it names. One task, no stop. Since then the repository was
renamed `twinoak/twinoak.meshmaster.exitnode-firmware` (11 October,
`5d0158f`); the release assets, the user agent and `firmwareString()`
keep their `meshmaster-agent` names on purpose - they name the protocol
role, the .NET agent sends the same shape, and the 0.1.x flasher links
keep working.

**14 — the guest door** opened both halves. `typeAllowsClass` admits
`discover` at `Guest` or `Admin`, as the server's
`JobTypes.CredentialClasses` does, and still refuses `acl` and `none`
(the ACL login, which a repeater that has not granted us an entry answers
with silence). The material of class `guest` with no password in the
claim is `""`, the blank login, with one log line saying so; an admin job
with no admin password is `nullptr` and `unauthorized` with nothing on the
air, as before. The session took the decision out of `JobRunner` into
`LoginSequence.cpp` as `loginMaterialFor`, so every branch is a native
test instead of a grep on a file the native build cannot compile - the
same split task 13 made for the sequence, and the right call; the
credential rail (`test_credentials`' grep for stores and file systems)
now covers that module too. Two log-line differences from the .NET
agent are deliberate and only visible off the guest door: `learned` names
the session's *role* (which agrees with the `loggedInAs` in the same
result) where the Pi names its class. The one stale line it found was on
the other side - EXIT-NODES §7's table said `discover | admin` - and
meshmaster.dk's review commit of 13 October corrects it.

### What is open

- [ ] **The bench walk, `docs/console.md` item 19**, on `v0.1.4`: Risskov
  already runs it; **AarhusC and Grindsted are still on `v0.1.3`**
  (meshmaster.dk's exit-node list will say so once it has an agent column,
  its 4.12 follow-ups). Flash AarhusC, then the four checks the report
  lists: the first empty-door pair (repeater 10, BS-Aarhus1) runs its
  `discover` at guest and opens a guest session; the verify `status`
  reuses it and the pair goes `active`; a repeater that *has* a guest
  password ends `unreachable` after two silent tries with no probe line
  between them; a guest pair *with* a password shows no *blank login*
  line at all.
- [ ] **08 is still stopped on `origin/task/p1.1-08-the-host-harness`**,
  awaiting Thomas's four clicks; its rebase now crosses 09's `main.cpp`,
  12's constants, 13's `LoginSequence`, and 14's `typeAllowsClass` and
  `loginMaterialFor`. The longer it waits the more the rebase costs; if
  the harness is no longer wanted, close it and delete the branch.
- [ ] **What 1.3 left open is still open** (`../phase-1.3/FOLLOWUPS.md`):
  the bench items on the display and the rotation, and `online()` as a
  native seam for `AgentLoop`.

### Watch

- meshmaster.dk's jobs list on repeater 10 after AarhusC is flashed: D-43
  keeps the one-off on AarhusC alone (the pin), so its log is this
  firmware's and nobody else's.
