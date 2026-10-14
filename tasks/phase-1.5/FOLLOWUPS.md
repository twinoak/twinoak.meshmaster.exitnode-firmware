# Pile 1.5 — follow-ups

Review findings, tagged with the task that must pick them up: `[NN]` for a
task of this pile. A session reads this file before it starts (RUNBOOK.md
section 0) and treats every open item tagged for its task as part of the task.
Only the reviewer ticks an item.

## Left by 15 (14 October)

- [ ] **Delete `origin/task/p1.1-08-the-host-harness`** (Thomas). Task 15's
  report says why in one paragraph: the host harness 08 wanted is this pile,
  15–17, built the other way round — interfaces in front of the mesh, the
  uplink and the platform rather than a second `JobRunner` for the host —
  and nothing from that branch is merged. The three readings it left behind
  that *were* worth keeping already landed on `master` by other routes (the
  login reply that carries no tag of ours, in `AgentMesh::sendLogin`; the
  `agc.reset.interval` key, 12; the rebase notes in the FOLLOWUPS of 1.2–1.4).
  `tasks/phase-1.1/logs/STOPPED-08.md` is git-ignored and goes with the clone.
- [ ] **`LINK-PROTOCOL.md` §6 and `IMesh`** (Thomas, in
  `twinoak.meshcore.standard-repeater-design-v2`): 15's report walks `IMesh`
  method by method against §6 and finds three things the link does not
  carry — `uniqueTime()` (nothing in the library calls it since 15; drop it
  from `IMesh` in 16 or 17 rather than add a request), `overheard()` and the
  per-type receive counters behind it (§6.4 `MESH_COUNTERS` has no
  `overheard` or by-type figure, and the heartbeat's `overheard` counter needs
  one), and the awaited-reply timeout, which the link takes at the send
  (`timeout_ms`) and the interface takes at `awaitReply()` — implementable
  with a ceiling at the send, but worth a sentence in §6.2. `setRing()` is
  §7's `OBS_PULL`, not §6's, and the task's "§6 alone" should read "§6 and
  §7".

## Left by 17 (14 October)

- [ ] **A stored repeater password of sixteen characters or more is a lock-out
  nobody is told about** (Thomas, in `twinoak/meshmaster.dk`). Found by the host
  fakes: `tests/fixtures/contracts/JobCredential.json`'s own sample is
  `"admin-password-sample"`, twenty-one characters, and a login carrying it is
  **silence** — `BaseChatMesh::sendLogin` sends the first fifteen bytes and
  `NodePrefs` holds `char password[16]`, so neither end can carry more. Both
  agents behave correctly and identically: the material is cut, one line says so
  (`loginMaterialFor`'s `noteCut` here, `Handlers.cs:196`'s warning there), and
  the job is reported `unreachable` — which is also what a repeater that is off
  the air reports. What is missing is the refusal at the **form**:
  `CredentialRotation.MaxLength` is 15 with exactly the right reason ("the
  firmware keeps fifteen … which is a lock-out"), and
  `CredentialService.MaxPasswordLength` — the *store* behind the credentials
  page — is still **128**, with a comment saying it is "a bound on the column,
  not a rule of the device (phase 2 learns that one)". Phase 2 learned it. So an
  owner can store a sixteen-character admin password today and every admin job
  on that repeater is `unreachable` for ever, with nothing on the page to say
  why. The fix is one bound and one sentence in `Credentials.Check`, in that
  repository; nothing in this firmware changes, and
  `test/test_runner`'s `APasswordSetLongerThanTheFirmwareKeepsIsNeverSent`
  already holds this end of it.
- [ ] **`IMesh::uniqueTime()` is gone** — this task did what the item above
  asked 16 or 17 to do (`lib/agent-core/include/agent/IMesh.h` says in its place
  why, `src/mesh/AgentMesh.h` keeps the method as the board's own, and
  `test_seams` fails if it comes back). The two remaining §6 gaps of that item -
  `overheard()` and the awaited-reply timeout - are still for the other
  repository.

## Left by 18 (14 October)

- [ ] **A second `AgentHeartbeat` fixture, for an agent that proposes nothing** (Thomas, in
  `twinoak/meshmaster.dk`). Task 120 changed `tests/fixtures/contracts/AgentHeartbeat.json`'s
  sample **in place** to a proposing agent — `nextCheckInS: 3600`,
  `maxTransactionsThisWake: 12` — so the one contract fixture for that envelope no longer
  carries the shape every agent but a repeater-v2 unit sends, and task 120's own promise
  ("an agent that sends neither is byte-for-byte as before") has no fixture of its own on
  either end. 18 held it from this end by cutting the two members off the fixture's bytes
  (`test_envelopes`'s `AnAgentThatProposesNothingWritesTheFixtureWithoutTheTwoWakeFields`),
  which is anchored to the file and fails loudly if the record's order moves — but a second
  sample written by `MeshMaster.Contracts`' own round-trip test, say
  `AgentHeartbeatNoWake.json`, would hold the same thing on the side that owns the record. It
  is one case in that repository's tests and a sync here; 18 did not open it, because a task
  of this repository does not edit the other one's fixtures (`tasks/README.md`: the spec is in
  the other repository, say so and let the reviewer open it there).
