# 03 — The read jobs

**Depends on:** 02.
**Spec:** [EXIT-NODES §3, §6, §7, §8](https://github.com/twinoak/meshmaster.dk/blob/fbf4625a0352663e1c26bd6877e45e6c555d4f7c/spec/EXIT-NODES.md), [API §3](https://github.com/twinoak/meshmaster.dk/blob/fbf4625a0352663e1c26bd6877e45e6c555d4f7c/spec/API.md), [MONITORING §1](https://github.com/twinoak/meshmaster.dk/blob/fbf4625a0352663e1c26bd6877e45e6c555d4f7c/spec/MONITORING.md).
**Size:** one session.
**Model:** claude-opus-5

## Goal

The board claims and answers the four jobs that need no login or an ACL
login: `status`, `telemetry`, `neighbours`, `trace`. In the .NET agent these
go through the companion protocol over a cable; here the board *is* the
companion, so each is a direct call into MeshCore's mesh layer — the same
functions the companion firmware runs when a phone asks — and the reply
arrives in the callbacks `MyMesh` already has. Simpler on the wire, and the
same result shapes on the server.

## Deliverables

- **The claim loop.** `POST /agent/v1/jobs/claim` `{ "max": 2 }` when the
  heartbeat says `jobsWaiting`, honouring `leaseUntil`, `idempotencyKey`,
  `path` (the hops the server knows, set as the contact's out-path before
  sending), `timeouts` (`perHopMs`, `loginMs`) and `groupWith` (jobs of one
  repeater in one claim share a login). One job on the air at a time. The
  budget from the heartbeat paces every transaction (`transactions a
  minute`, the gap), exactly as `PlannedLoad` prices it.
- **The handlers**, each a request into the mesh and a wait for the
  callback with the tag: `status` (`REQ_TYPE_GET_STATUS`; at class `acl`
  the owner-info request too, for `firmware`), `telemetry`
  (`REQ_TYPE_GET_TELEMETRY_DATA`, Cayenne LPP decoded with the channel-0
  padding rule of task 44), `neighbours` (`REQ_TYPE_GET_NEIGHBOURS`, nine a
  page, eight-byte prefixes, paged until the count is reached), `trace`
  (`sendTracePath`, the hop list with SNR per hop; `unsupported` at no
  transaction when the path hash size is three bytes). Result JSON equal to
  the .NET agent's for the same bytes — the transcribed fixtures under
  `tests/fixtures/companion/` are the inputs, and the expected results are
  what `MeshMaster.Agent.Tests` asserts for them.
- **The ACL login** for class `acl`: a blank-password login as ourselves
  (`sendLogin` with an empty password), the reply's `acl_perms` read as
  task 41 reads it (byte 1 is `is_admin`, the level is `acl_perms`), a
  session cached per repeater for the firmware's session lifetime, and a
  login answered below the class refused as `unauthorized` with the tier
  in the result (`LoginBelowClassResult`, task 60). A blank login answered
  *above* the grant sends `setperm <own key> <granted>` at once, as task 60
  built it.
- **Outcomes** as the server reads them: `ok`, `unreachable` (silence
  twice), `timeout` (the per-hop budget), `unauthorized`, `unsupported`,
  `failed`; `transactions`, `pathUsed`, `hops`, `rttMs`, `startedAt`,
  `finishedAt`, and a `log` bounded at 4 000 characters with passwords
  redacted (none here, the rule anyway). A result posted once with its
  `clientId`; a late result still posted.
- **`jobTypes`** in the heartbeat becomes the four; routing then sends
  them (task 53: routing requires the type advertised).
- **The console**: `jobs` (the last ten with outcome and transactions),
  `log.frames on` prints every mesh frame as hex on the console — with
  the same redaction the .NET agent's `--log-frames` has, and never a
  message's text.

## Acceptance criteria

- [ ] Native tests: each handler's result JSON for the transcribed fixture bytes equals the .NET expectation; the pacing arithmetic matches `PlannedLoad`; the login-success parse reads `is_admin` and `acl_perms` at the right bytes; the padded telemetry decodes to two readings.
- [ ] `pio run -e heltec_v3` green; the heartbeat advertises the four types.
- [ ] The claim honours `max`, `leaseUntil` and `groupWith` in a host test against a recorded claim reply.

## Not verified here (Thomas, with the board)

Associate Risskov with the board's key (the wizard, as with the Pi); *Read
now*; the four jobs `ok` on the jobs page with sane values; the transaction
counts against EXIT-NODES §7; the SNR the board hears against the Pi's.

## Out of scope

Anything with a password (04, 05).
