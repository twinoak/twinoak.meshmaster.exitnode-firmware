# Pile 1.1 — follow-ups

Open items the reviewer ticks, tagged with the task that must pick them up.
`[meshmaster.dk]` is not a task of this repository at all: it is a finding for
the other end, and whoever opens it there should carry the paragraph with it.

## From 08 (the host harness), 2 October 2026

The harness was asked to say, per job type, "every place the firmware and
`FakeCompanion.cs` would have answered differently … each is either a firmware
bug, a .NET bug, or a difference in the two readings of the source". Two of the
differences were this firmware's and are **fixed in 08** (`reports/08.md` has
them, with the finding written beside the fix in the code). Four are the other
end's, and they are these. The whole table, including the rows that are nobody's
bug, is in `reports/08.md`.

- [ ] **[meshmaster.dk] A blank login to a repeater whose guest password has
  never been set is answered *guest*, not refused.** `FakeCompanion.LoginRequest`
  reads `handleLoginReq` as an `if / else if` chain, so a blank password only ever
  looks the sender up in the ACL and a key the ACL does not hold gets
  `PushLoginFailed` (or silence under `SilentToAKeyItDoesNotKnow`). The firmware's
  control flow is two separate statements
  (`examples/simple_repeater/MyMesh.cpp:89-110` at `d9296435`): the ACL lookup,
  and then — **whether or not it found anything** — `strcmp(data, _prefs.password)`
  and `strcmp(data, _prefs.guest_password)` against the empty string. And
  `NodePrefs()` leaves `guest_password` empty (`src/helpers/CommonCLI.h:191`). So
  on a repeater nobody has set a guest password on, a stranger's blank login — the
  `acl`-class login of every probe — is answered **guest**, and
  `acl.putClient` writes a guest entry for that key. What follows from it on the
  server's side: an `acl`-class probe through a pair whose grant was revoked
  reports `unauthorized` with `loggedInAs: guest` rather than `unreachable`, so
  the association reads *needs-regrant* and not *unreachable*; and the reasoning
  of task p3.5-40 ("a repeater that has not granted that key an entry answers it
  with silence rather than a refusal") holds only for a repeater whose guest
  password **is** set. `spec/EXIT-NODES.md` §7's credential-class note says the
  silence without the condition. The fake, the sentence and
  `DECISIONS.md` A-4 ("verify the exact behaviour against the firmware version in
  use") are the three places to settle it.

- [ ] **[meshmaster.dk] `FakeCompanion.cs` models no replay rule at all, so
  nothing on the .NET side tests the one the apply engine is built around.**
  `MyMesh::onPeerDataRecv` (`:702-762`) keeps a `last_timestamp` per client: a
  CLI line whose sender timestamp **equals** it is a *retry* and the command is
  not run and nothing is answered; one below it is dropped as a replay; and a
  `REQ` needs strictly more. The fake keeps no such state — `Cli()` takes
  `senderTimestamp` and uses it for the clock commands only — so two lines in the
  same second are answered twice there and once on the air. That rule is the
  whole reason `ConfigApplyHandler` takes a fresh second per line, and it is
  currently held by a comment on both ends rather than by a test on either. A
  `LastTimestamp` on `FakeRepeater` and one test that sends two lines in a second
  would close it.

- [ ] **[meshmaster.dk] `FakeCompanion`'s `Ftoa` rounds to six decimals; the
  firmware truncates to seven.** `StrHelper::ftoa`
  (`src/helpers/TxtDataHelpers.cpp:52-141`) walks the float's mantissa, writes
  **seven** digits of fraction, **truncates** rather than rounds, and strips
  trailing zeros. The fake uses `value.ToString("0.######")`. For a repeater at
  latitude 56.41 the firmware answers `> 56.4099998` and the fake answers
  `> 56.41`. A config snapshot stores the string verbatim and the drift
  comparison compares strings, so every `lat`, `lon`, `af`, `rxdelay`,
  `txdelay` and `direct.txdelay` value a real repeater reports is a value the
  .NET fake has never produced. `src/host/HostMesh.cpp`'s `firmwareFtoa` is the
  function ported, if a port is wanted.

- [ ] **[meshmaster.dk] An `onboard` exit node's `transport` and `board` do not
  come back out of `GET /api/v1/agents/{id}`.** Run against the compose stack at
  `bdaf526` with the seeded exit node: the heartbeat carried
  `"transport": "onboard"` and `"board": "heltec_v3"`, the record's `reported`
  block came back with `agentVersion`, `at`, `companionConnected`,
  `companionFirmware`, `companionPublicKey`, `errors`, `jobTypes`,
  `pollIntervalS`, `queueDepth`, `radio` and `sourceIp` — **and neither
  `transport` nor `board`** — while the record's top-level `transport` stayed at
  the registered `tcp`. `spec/API.md` §3 says the heartbeat fills the record
  including the transport ("a transport word this server does not know is not
  stored and the registered one stands; the four it knows are `serial`, `tcp`,
  `native` and `onboard`") and, since `tasks/phase-4.2/66`, the optional `board`;
  and §5 of `spec/EXIT-NODES.md` says the exit node's page shows *onboard ·
  Heltec V3*. From the agent's end both are unobservable, which is the half this
  repository can see. Either the write or the DTO is where they are not. The
  evidence is `out/host-run.log` of a run of `tools/host-run.ps1`, and that
  script asserts on both, so the fix shows up there.

## For this repository

- [ ] **[09 or a reviewer] This firmware's default `config.read` list is fifteen
  keys; the server's is sixteen.** `kDefaultConfigKeys` in
  `src/agent/ConfigRead.cpp` is `name, lat, lon, advert.interval,
  flood.advert.interval, repeat, dutycycle, flood.max, flood.max.unscoped, tx,
  path.hash.mode, multi.acks, allow.read.only, regions, acl` —
  **`agc.reset.interval` is missing**, and `ConfigReadPayload.DefaultKeys` in
  meshmaster.dk's `src/MeshMaster.Contracts/Payloads.cs` has it (sixteen entries
  since its task p4.3-71; `spec/EXIT-NODES.md` §7 says "sixteen keys … so the read
  the buttons make costs 19 + login = 20"). Found by the harness's arithmetic: a
  default read costs this firmware 20 transactions *including the ACL restore*,
  which is the spec's 20 reached by a different route, so the two numbers agreeing
  hid a list that does not. It only bites when a claim carries no `keys` at all —
  the server's claims carry them — but then the snapshot has no
  `agc.reset.interval` and the drift comparison reads it as unread. One line in
  `kDefaultConfigKeys`, `kDefaultConfigKeyCount` to 16, and `test_config_read`'s
  price assertions. Left for a reviewer rather than taken in 08, because what a
  default read costs is the lease arithmetic's and the config read's business and
  not a harness task's.

- [ ] **[09 or a reviewer] The compose-stack half of 08's acceptance criterion 3
  is not met, and cannot be by a script.** `tasks/phase-1.1/logs/STOPPED-08.md`
  is the detail. In short: every door that *queues* a job on meshmaster.dk is
  either gated — and a personal access token can never pass a gate, by
  `spec/API.md` §2 and D-26 — or a Razor page, and session cookies are not
  accepted on `/api/v1` at all. So the association, Read now, the apply and the
  rotation are four clicks in the web UI, and `tools/host-run.ps1` prints them
  with the assertion for each rather than going round them. What a run *does*
  prove against the real stack is eleven assertions, including the chunked
  answers the stub does not exercise. The box to tick is somebody walking those
  four clicks once and pasting the script's output into `reports/08.md`.

- [ ] **[09 or a reviewer] `tools/host-stub.py` correlates the journal to a job
  by the result's `transactions` count.** It is exact for every path the cycle
  takes today — one `spend()` per frame, one journal entry per send — but a send
  that fails *after* the budget was spent would shift the walk by one and
  mis-attribute every job after it. A job label carried through `MeshPort`'s
  `beginJob` would make it exact by construction; it was not worth an interface
  change in 08.
