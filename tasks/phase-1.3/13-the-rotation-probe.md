# 13 — The rotation probe

**Depends on:** 12.
**Spec:** meshmaster.dk `spec/CONFIGURATION.md` §7 (credential rotation, step 4 as task p4.7-95 rewrote it), `spec/EXIT-NODES.md` §7 (*One silence is reported as a refusal all the same*, the replay rule, the transaction model), `spec/DECISIONS.md` A-4 (the four behaviours of a login at `d9296435`: a blank login from a key the ACL holds is answered at the entry's level, and at guest with a guest entry written on a repeater with no guest password; a wrong non-blank password is silence); the .NET agent's `src/MeshMaster.Agent/Handlers.cs` (the probe at `LoginAsync(PublicKey, ReadOnlyMemory<byte>.Empty, LoginWait, ct)`) and `src/MeshMaster.Contracts/Results.cs` (`LoginSilentResult(string Reason, string? ProbeAnsweredAs)`); this repository's `src/agent/JobRunner.h/.cpp` (`Context`, `loginOnce`, `ensureSession`), `src/agent/JobResults.h` (`writeLoginBelowClassResult`), `src/agent/JobEnvelopes.h` (`credentialCandidate`), `src/mesh/AgentMesh.h` (`sendLogin`, `awaitReply`, `parseLoginReply`); the finding in meshmaster.dk's `tasks/phase-4.7/reports/95.md` (*A finding for the firmware repository*), which is the change written in this firmware's terms; finding 4 in the README.
**Size:** one session.
**Model:** claude-fable-5-1

## Goal

During an `unverified` credential rotation the server hands a job the
*new* candidate; if the repeater does not know that password, stock firmware
answers nothing, and this agent's `ensureSession` - two `loginOnce`,
`NoReply` twice - reports `unreachable`, which is what a dead repeater
reports too. So the server cannot tell the two apart and CONFIGURATION §7
step 4's fallback to the old candidate never happens for an onboard exit
node. The .NET agent learned the difference in 95: a **blank login** from
the exit node's own key is answered by any repeater whose ACL holds the key,
so a silence to the candidate right after an answered blank login is the
password's silence and not the mesh's. Teach this agent the same probe,
with the same result record, on that path only.

## Rails, before anything is written

- Read `handleLoginReq` at the pinned MeshCore commit, not a summary of it:
  the blank login's first statement answers at the ACL entry's level; its
  second answers *guest* and writes a guest entry when the repeater has no
  guest password; a wrong non-blank password is `return 0`. A-4 has the line
  numbers.
- Read `Handlers.cs`'s probe in the .NET agent and match its decisions, not
  its code: the probe runs only when the material is a candidate (`new`),
  only after the **first** try was silent, costs one transaction through
  the gate, stores no session, and the **second** try is the one judged.
- The result record is the contract's `LoginSilentResult` exactly:
  `{"reason":"silentAfterReachable","probeAnsweredAs":"<tier>"}`, the tier
  being the word the server uses for a login's level (`guest`, `read-only`,
  `read-write`, `admin`, as `writeLoginBelowClassResult` already spells
  them) or `refused` for the legacy `LOGIN_FAIL` frame. Nothing is added to
  the contract and no fixture is edited: `tools/sync-fixtures.ps1 -Verify`
  stays green.

## Deliverables

- **`Context`** gains `bool silentAfterReachable`, `bool probeRefused` and a
  `probeAnsweredRole` (the role byte the probe's reply carried, or none).
- **`JobRunner::probeReachable(const ClaimedJob&, Context&)`**: spends one
  transaction through the gate as every send does (refused by the gate →
  no probe, `false`), sends `sendLogin("", tag)` - the ACL login the status
  probes already make - and `awaitReply(tag, loginWaitMs(job), pump_)`. No
  reply → a log line, `false`. A reply that parses as a login → the role
  into `probeAnsweredRole`, a log line saying the repeater is on the air and
  at what level, `true`. A reply that does not parse → `probeRefused`,
  `true` (a refusal is an answer). `sessions_` untouched either way: the
  next job logs in afresh.
- **`ensureSession`**: when the material is the `new` candidate and the
  first `loginOnce` was `NoReply`, `probeReachable` runs before the second
  try; a second `NoReply` after a probe that answered ends
  `Outcome::Unauthorized` with `silentAfterReachable` set, and the result's
  data is `LoginSilentResult` (a `writeLoginSilentResult(const char* tier,
  …)` beside `writeLoginBelowClassResult`). A second `NoReply` after a
  silent probe is `Unreachable` as before. A candidate that is `old`, or no
  candidate, never probes. The `agentLog` names which it saw, in the .NET
  agent's words.
- **Tests.** `test_claim` (or wherever `ensureSession`'s outcomes are
  tested against the fake mesh): the four paths - silent, silent → probe
  answered → silent (`unauthorized`, the data byte-for-byte), silent →
  probe silent → silent (`unreachable`), silent → probe refused → silent
  (`unauthorized`, `probeAnsweredAs: refused`) - and that an `old`
  candidate never probes, that the probe costs one transaction, and that no
  session is stored by it. `test_job_results` gets the new writer against
  the exact JSON of `LoginSilentResult`.
- **The display.** 12's line four says `Job 1234 status` while this runs,
  as for any job; nothing new to say on the OLED.
- **docs/ops/agent.md** (or wherever this firmware describes its logins):
  one paragraph on the probe, pointing at CONFIGURATION §7 step 4.

## Acceptance criteria

- [ ] A job carrying the `new` candidate whose first login is silent sends exactly one blank login before its second try; a job carrying `old`, or no candidate, sends none.
- [ ] Silent → answered probe → silent ends `unauthorized` with `{"reason":"silentAfterReachable","probeAnsweredAs":"<tier>"}` as its data, the tier the probe's; silent → silent probe → silent ends `unreachable` with no data.
- [ ] The legacy refusal frame to the probe counts as answered, `probeAnsweredAs: refused`.
- [ ] The probe spends one transaction and stores no session.
- [ ] `tools/sync-fixtures.ps1 -Verify` green; `pio test -e native` green; `pio run -e heltec_v3`, `-e heltec_v4` green; CI green.

## Not verified here

A real repeater asked a wrong password and then a blank login - the fake
is the whole evidence, here as in the .NET agent. 08's host harness, once
merged, is where this runs end to end against meshmaster.dk's rotation;
the report says so.

## Out of scope

The display (12); leasing a candidate job alone so that two candidate jobs
in one batch do not both probe (meshmaster.dk's 4.7 follow-ups, left open).
