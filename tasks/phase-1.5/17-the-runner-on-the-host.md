# 17 — The runner on the host

**Depends on:** 16.
**Spec:** proposal 03 (*What it costs here* — "the host harness of task p1.1-08 becomes the ordinary way the runner is tested"); `tasks/phase-1.1/08-the-host-harness.md` (what a fake stock repeater must answer, read from `simple_repeater/MyMesh.cpp` and `CommonCLI.cpp` at the MeshCore pin — the reading, not the branch); meshmaster.dk's `spec/EXIT-NODES.md` §5 (*Behaviour that matters*), §6, §7 (the transaction model: what each of the thirteen job types costs and sends), at today's `master`; `tests/fixtures/contracts/` (the claim and result envelopes) and `tests/fixtures/companion/` (the repeater's reply bytes, by firmware function); `test/test_config_apply/` (`FakeRepeater : ApplyExchange`) and `test/test_rotation_probe/` (`ScriptedRepeater : LoginExchange`) — the two fakes that exist and the shape to generalise; `lib/agent-core/include/agent/{IMesh,IUplink,IPlatform}.h`.
**Size:** one session.
**Model:** claude-opus-5

## Goal

With the three seams in place, the whole agent — `AgentLoop` driving
`JobRunner` through every one of the thirteen job types, the login
sequence, the ACL restore, the airtime gate, the result buffer, the backoff
— runs on the host against a scripted repeater, a scripted server and a fake
clock, in milliseconds. Every rule that was learned on the bench and is
written in `EXIT-NODES.md` §5–§7 gets a test that would catch its loss. This
is what the stuck host harness wanted, done the way the library allows.

## Deliverables

- **`test/support/`** (compiled into the suites that need it):
  - **`FakeMesh : IMesh`** — a stock repeater at the MeshCore pin, as a
    script: it holds a name, an ACL (keys with levels), a guest password or
    none, an admin password, a config of the keys `CONFIGURATION.md` lists,
    neighbours, and answers `sendLogin` / `sendRequest` / `sendCli` /
    `sendDiscovery` / `sendTrace` the way `MyMesh.cpp` and `CommonCLI.cpp` at
    the pin do — **built from the companion fixtures' bytes where one
    exists** (`tests/fixtures/companion/*.hex`, each cited to its firmware
    function) and from a reading of the pinned source where none does, with
    the citation in a comment. Faults on cue: silence (once, twice, always),
    a reply with the wrong tag, a slow reply past the wait, a session the
    repeater forgot, a login refused, an ACL read-only. It records every
    send with the fake clock's time, so a test can assert *one send per
    command* and the minimum gap.
  - **`FakeUplink : IUplink`** — a scripted server: a config, heartbeat
    replies with `jobsWaiting`, a claim queue of jobs built from the
    contract fixtures (`tests/fixtures/contracts/*.json` → `ClaimedJob`),
    results captured as the JSON bytes the runner posts, observations
    captured; faults: unreachable, 401, 429 with `Retry-After`, 5xx, a slow
    call.
  - **`FakePlatform : IPlatform`** — a clock that only moves when the test
    says (so `idle()` and the gate's waits are asserted, not slept), a
    seeded random, a log capture.
- **`test/test_runner/`** — `JobRunner` end to end, one scenario per job
  type and per rule: each of the thirteen types against a repeater that
  answers (the result bytes compared to the contract fixture's shape);
  `status` at `acl`, `neighbours` at guest, `discover` at guest with and
  without a password (1.4's door); the login sequence and session reuse (a
  status + telemetry pair costs one login); the ACL restore after an admin
  job, and *not* after `acl.set`; a lost reply retried once with a fresh
  login on a reused session; `unreachable` is silence twice; `unauthorized`
  is a refused login; `unsupported` for an unknown type; a password line and
  its echo redacted in the captured log; **the credential bytes are zero
  after the job** (read them back); one send per command, the minimum gap
  and the per-minute cap held by the fake clock's record; the lease running
  out mid-job refusing the next transaction; a `FakeExtension` of two
  invented types: claimed, run, its `data` in the result envelope, its types
  in the heartbeat, and a type nobody handles still `unsupported`;
  `extendLease` and `postProgress` reaching the fake server with the bytes
  the fixtures expect.
- **`test/test_loop/`** — `AgentLoop`: config → heartbeat → claim on
  `jobsWaiting` → run → results → observations batch, in order; the offline
  buffer (server away for an hour, results posted later with their original
  timestamps and client ids); backoff on each failure class per
  `ServerPolicy`; the reboot guard after twelve failures over ninety minutes
  **asking** the platform to reboot (the fake records it, nothing restarts);
  the heartbeat still sent during a long job (`heartbeatIfOverdue`); the
  heartbeat's body against the fixture.
- **The two old fakes** (`FakeRepeater`, `ScriptedRepeater`) are either
  replaced by `FakeMesh` or kept with a sentence on why; no duplicated
  repeater behaviour across three fakes.
- **`docs/host.md`**: how to run the suites, what the fakes are and are not
  (they answer as the pinned firmware does, by citation; a MeshCore bump
  re-reads them — add that to `tasks/sync/TEMPLATE.md`'s list), and how a
  consumer of the library can reuse `test/support/` (the Walter does, over
  its link).

## Acceptance criteria

- [ ] All thirteen job types and every rule named above have a test; the report lists them against `EXIT-NODES.md` §5's *Behaviour that matters* bullets, one line each.
- [ ] Every `FakeMesh` answer cites a fixture or a firmware function at the pin (a test checks the citation header, as `test_contract_fixtures` checks its manifest).
- [ ] The suites run in under a minute on the host (the fake clock, not sleeps).
- [ ] `pio test -e native` passes everything, old and new; the boards build.
- [ ] `docs/host.md` exists; `tasks/sync/TEMPLATE.md` names the fakes as a thing a bump re-reads.

## Not verified here

Nothing on the air changes. The fakes are only as true as their citations;
the first disagreement with a real repeater is a fixture to add.

## Out of scope

The one-wake mode (18). Fixing behaviour the tests expose (write a FOLLOWUPS
item tagged for the task that owns the rule; fix only what blocks a test
from being written).
