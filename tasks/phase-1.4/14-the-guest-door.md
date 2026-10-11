# 14 — The guest door

**Depends on:** nothing.
**Spec:** meshmaster.dk `spec/EXIT-NODES.md` §4.1 (the guest door; step 1: *empty, which is the answer for a repeater whose owner never set one*; its `discover` step) and §7 (the classes a job may run at; *the binary neighbours request answers any login*), `spec/DECISIONS.md` A-4 (`handleLoginReq`: a blank login from a key the ACL does not hold is answered *guest* on a repeater with no guest password, silence on one with); meshmaster.dk `src/MeshMaster.Contracts/JobTypes.cs` (`CredentialClasses`: `Discover => [Guest, Admin]` since task p4.7-94, with the paragraph saying why), `src/MeshMaster.Core/Transport/AgentProtocol.cs` (the claim for a guest pair with no password carries **no credential**) and `src/MeshMaster.Agent/Handlers.cs` (`LoginMaterial`, `DiscoverHandler`: how the .NET agent does both); this repository's `src/agent/JobEnvelopes.cpp` (`typeAllowsClass`, `canRun`), `src/agent/JobRunner.cpp` (`loginMaterial`, `ensureSession`, `runDiscover`, `learned`), `src/agent/LoginSequence.*` (task 13), `src/agent/Credentials.*` (`hasGuestPassword`); tasks p1-04, p1.2-10 (`neighbours` at guest, the same table), p1.3-12, p1.3-13; the README.
**Size:** one session, short.
**Model:** claude-opus-5

## Goal

The server's guest door (meshmaster.dk tasks p4.7-94 and p4.11-111) pairs a
repeater through an exit node with the guest password its owner gave - or
with none, for a repeater whose owner never set one, which answers a blank
login *at guest*. The pair is made by a `discover` job **at class guest**,
and its probes are guest-class jobs that arrive **with no credential** when
the password is empty. The .NET agent does both. This firmware does neither:
`typeAllowsClass` admits `discover` at admin only, so `canRun` answers the
pairing step `unsupported` and the association ends `failed` before anything
goes on the air; and `loginMaterial` at guest with no password returns
`nullptr`, so a guest probe ends `unauthorized`, which the server reads as
the repeater refusing the key. Seen on 11 October on meshmaster.dk: repeater
10 (foreign) through exit node 4 (this firmware, v0.1.3) - association 9
*discover at guest: unsupported*, then *read now* at guest `unauthorized`,
then the Neighbours tab's read at `acl` (the pair not being active) silent
through two exit nodes. Make the door work through a Heltec: `discover` at
guest, and a blank guest login where the claim carries no password - and
keep the one case that *is* missing material, an admin job with no admin
password, exactly as it is.

## Deliverables

- **`typeAllowsClass`**: `discover` at `Guest` or `Admin`, the server's
  table (`JobTypes.CredentialClasses`), with the comment saying why in the
  words of task p1.2-10's `neighbours` paragraph above it: the server sends
  the guest door's `discover` at class `guest` since p4.7-94, and this board
  answered `unsupported` where the .NET agent logged in. `acl.set`,
  `config.read` and everything that writes stay admin-only.
- **`loginMaterial` at `CredentialClass::Guest`** with `!hasGuestPassword`
  returns `""` - the blank login, as the `default` branch does for `acl`
  and `none` - with a log line saying so: *no guest password in the claim:
  the blank login, which a repeater with no guest password answers at
  guest*. The admin branch is unchanged: no admin password is still
  `nullptr` and `Outcome::Unauthorized`, because there is no admin login
  without one. Read `Handlers.cs` for the .NET agent's exact behaviour on
  the reply and match it: answered at guest → the session, the job runs;
  answered at a higher level (the key had an entry) → the session at that
  level, as the acl login does (`satisfies` already says so); silence → the
  two tries of `ensureSession` and `unreachable`, which is what the server
  expects for a repeater that does have a guest password (EXIT-NODES §4.1
  step 2's sentence).
- **`runDiscover` at guest** needs no new branch: with the material no
  longer `nullptr` it logs in along the server's path or floods, and
  `learned` reports the path and the level the reply granted, which is what
  the server's association step reads. Two words to fix in `learned`: its
  *the admin session is open* line names the session's role
  (`permissionName(ctx.sessionRole)`), since the session is a guest one
  here; and the "which login" paragraph at the top of `runDiscover` (*the
  admin password, because a login with none is the ACL login*) gets one
  sentence more: at class guest, the guest password or the blank login of
  the empty door.
- **The rotation probe** (13) is untouched by this: it probes only for a
  `new` candidate with material, and a blank guest login has neither.
- **Tests.** `test_rotation_probe`'s scripted repeater (or `test_claim`,
  wherever the login sequence is walked): a `discover` job at guest with
  no credential sends one blank login and, answered at guest, reports the
  path and `loggedInAs: guest`, `ok`; the same with a guest password sends
  it; a guest-class `status` job with no credential logs in blank and runs
  when the repeater answers at guest; the same ends `unreachable` after two
  silent tries when it does not; an admin-class job with no admin password
  still ends `unauthorized` without a send; a `neighbours` job at guest
  with no credential runs; `canRun` says yes to `discover` at guest and
  still no to `discover` at `acl` and `none`.
- **`docs/console.md`**: the job log's login line for this case, one
  sentence; the job-type table, if it lists classes, says `discover` at
  guest or admin.

## Acceptance criteria

- [ ] A `discover` job at class guest is run, not answered `unsupported`: with a guest password it sends it, with none it sends the blank login; answered, it reports the path and the granted level as an admin discover does.
- [ ] A guest-class job with no credential in the claim sends one blank login and, answered at guest, runs; a claim with a guest password still sends it.
- [ ] A silent blank login ends `unreachable` after two tries; an admin job with no admin password still ends `unauthorized` with nothing sent.
- [ ] `pio test -e native` green with the new cases; `pio run -e heltec_v3`, `-e heltec_v4` green; `tools/sync-fixtures.ps1 -Verify` unchanged; CI green.

## Not verified here

A real repeater with no guest password answering a blank login from the
Heltec: the first guest pair Thomas makes at the empty door through it -
repeater 10 through exit node 4 on meshmaster.dk is waiting - and its
`neighbours` read, are the test. A repeater that *has* a guest password
answers the blank login with silence, and the pair ends `unreachable`
there, by design on both sides.

## Out of scope

The host harness (08); any change to what the server sends.
