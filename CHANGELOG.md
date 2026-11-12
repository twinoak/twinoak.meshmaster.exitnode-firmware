# Changelog

This file is for a **consumer of `lib/agent-core/`** — a second firmware that
pins this repository at a tag ([`docs/library.md`](docs/library.md)). One section
per tag, and each one answers the four questions a pin has to ask:

- did any method of `IMesh`, `IUplink`, `IPlatform`, `IClock`, `IJobExtension`,
  `JobContext`, `ResultWriter` or `TextOut` move? (a compile error in your tree)
- did `kMeshReplyDataCap` or `kMeshPathCap` move? (your `static_assert`)
- did the contract fixtures' manifest commit move? (the wire dialect moved, and
  **nothing of yours stops compiling** — this is the dangerous one)
- what is the library's version, and the MeshCore pin it reads a repeater's
  replies against?

It is not a changelog of the Heltec firmware. What a release flashes, and what
flashing it does to a provisioned board, is in the GitHub release notes that
[`.github/workflows/release.yml`](.github/workflows/release.yml) writes; what
this repository changed and why is in `tasks/phase-*/reports/`.

A task that moves one of the four things above adds its line here in the same
pull request, as `docs/` moves with code.

## Unreleased

- **A line that cannot be asked twice** (task p1.10-48; meshmaster.dk's
  p4.32-188 and p4.32-189 are the server's half). **Nothing of a pin moved** -
  not a method of `IMesh`, `IUplink`, `IPlatform`, `IClock`, `IJobExtension`,
  `JobContext`, `ResultWriter` or `TextOut`, not `kMeshReplyDataCap` or
  `kMeshPathCap`, not the fixture manifest (still meshmaster.dk `cd872a7`) - and
  nothing of yours stops compiling. **One constant of `agent/Airtime.h` changed
  its value**, which a consumer that prices a wake will see in its arithmetic:

  - `mm::kTransactionsForAclRestore` is **2**, not 1, and
    `mm::transactionsFor("acl.set", ...)` answers 2 rather than 1. The new
    `mm::kTransactionsForOwnEntryWrite` is where both come from: the line, and
    the blank login that checks it when the line is not answered. So
    `transactionsOf()` prices any job that may send that line - every job
    through a pair with a grant, which is where the restore is counted - **one**
    transaction higher than before. If your wake refuses jobs by
    `transactionsOf()`, give it one more. A repeater that answers the line still
    spends the one transaction EXIT-NODES §7 prices, and the second round a
    check answered at admin leads to is beyond the estimate exactly as the
    login's second try always has been.

  **What moved behind it**: `setperm <own key> <level>` is the session's own
  level on stock firmware (the login table *is* the access list), so once the
  line has run this key is no longer admin and `onPeerDataRecv` drops every
  further console line from it without a word - the retry cannot be answered. The
  library now sends that line **once**, in the grant (`acl.set` on our own key)
  and in the restore alike, and reads back what it did with a **blank login**:
  answered at the level asked for is *confirmed, by the login and not by an
  "OK"*; answered at admin means the line never arrived, so it goes once more and
  is checked once more (two rounds at most); a level-0 line answered at guest is
  the confirmation that the key is off; and a silence confirms nothing. A
  confirmed grant of our own key now **keeps no session**, so the verify that
  follows logs in by key - the restore keeps its session as before. meshmaster.dk
  lost 20 of 52 grants and found one association *elevated* seven times in 39
  days for want of this. [`docs/library.md` §3.7](docs/library.md) is the page.

- **Only a login reply answers a login** (task p1.10-47; meshmaster.dk's
  p4.32-186 is the server's half). **Nothing of a pin moved** - not a method of
  `IMesh`, `IUplink`, `IPlatform`, `IClock`, `IJobExtension`, `JobContext`,
  `ResultWriter` or `TextOut`, not `kMeshReplyDataCap` or `kMeshPathCap`, not the
  fixture manifest (still meshmaster.dk `cd872a7`). **Two things of
  `agent/LoginSequence.h` are gone**, and both are a compile error in your tree
  only if you drive `loginSequence()` yourself:

  - `mm::ProbeAnswer::Refused` and `mm::LoginVerdict::probeRefused`. The blank
    login of the rotation path is **answered or silent**, because nothing of
    MeshCore at the pin refuses a login (`handleLoginReq` answers
    `RESP_SERVER_LOGIN_OK` or `return 0`, and so do `simple_room_server` and
    `simple_sensor`); what used to produce the third value was a response that is
    not a login reply, which is now dropped.
  - `mm::kProbeAnsweredRefused`. `mm::probeAnsweredAs(verdict)` is the role's
    `permissionName` and nothing else. The **wire** is unchanged:
    `LoginSilentResult.probeAnsweredAs` still admits `refused` for an agent whose
    companion emits `PUSH_CODE_LOGIN_FAIL`, and `writeLoginSilentResult` still
    writes whatever string it is given.

  **What moved behind them**: a login is awaited with tag 0 - *the next response
  from this peer* - because `handleLoginReq` stamps its reply with the repeater's
  own clock where `handleRequest` reflects ours, so on that wait your mesh hands
  the runner the late reply to the request that had just timed out. The runner
  now **drops a response that is not a login reply and waits on** - by a tag this
  job sent, which is how an empty neighbour page (four zero bytes, which
  `parseLoginReply` reads as *login ok, permissions 0*) is caught, and by the
  parse otherwise - and the login is then *no reply*, with the second try as for
  any silence. So no login the repeater did not answer is reported
  `unauthorized` any more: `LoginTry::Refused` is a login answered **below the
  class it was for** and nothing else. Nothing is asked of your `IMesh`: the rule
  is in the library precisely so that a mesh which filters nothing is as safe as
  one that does. meshmaster.dk parked ten working ACL associations as *the ACL
  entry is gone* for want of it on 12 November 2026.
  [`docs/library.md` §3.6](docs/library.md) is the page.

- **A guest answer the job can use** (task p1.10-46). **Nothing of a pin moved**
  - not a method of `IMesh`, `IUplink`, `IPlatform`, `IClock`, `IJobExtension`,
  `JobContext`, `ResultWriter` or `TextOut`, not `kMeshReplyDataCap` or
  `kMeshPathCap`, not the fixture manifest (still meshmaster.dk `cd872a7`) - and
  nothing of yours stops compiling. **What a result can now carry is one member
  more**, and two free functions are new:

  - `mm::JobReport::loggedInAs` (`agent/JobEnvelopes.h`), written **last** in the
    envelope after `failedAt`, as `JobResultReport`'s own parameter order has it.
    `JobRunner` fills it; the fixture is `JobResultReportAtGuest.json`. nullptr -
    absent on the wire - is every result that ran at the class it asked for,
    which is what an exit node built before this sends and what the server reads
    exactly as it did.
  - `mm::servedWholeAtGuest(type)` and
    `mm::guestIsEnough(type, credentialClass, grantedRole)`
    (`agent/JobEnvelopes.h`): `JobTypes.ServedWholeAtGuest` and
    `GuestIsEnough.Covers` of `MeshMaster.Contracts`, so the firmware, the .NET
    agent and the server read one rule.

  **What moved behind them**: a login at class **`acl`** answered **below
  `read-only`** - on stock firmware *guest*, which is what the repeater answers
  when the ACL entry this exit node's key was granted is gone or demoted - no
  longer loses the reading along with the login for the two types a guest login
  serves **whole**. `MyMesh::handleRequest` checks no permission at all for the
  status request ("guests can also access this now") or for the neighbour
  request, so a `status` or a `neighbours` job runs on the session it was given
  and reports `ok` with the data and the tier. The login is still the evidence it
  was: the server moves the association exactly as it moves it for the
  `unauthorized` carrying the same tier. **Everything else is judged as before** -
  `telemetry` is masked to the base channels for a guest sender and stays
  `unauthorized` with `writeLoginBelowClassResult`, a `get` is admin's, and every
  class but `acl` is what it was. The session is still never held above its
  level, so the `telemetry` grouped with such a read logs in afresh and a
  grouped pair costs two logins where one at the class it asked for costs one.
  [`docs/library.md` §6.3](docs/library.md) is the page.

- **The owner-info reply is kept whole, and asked for at every class** (task
  p1.10-45). **The fixture manifest moved** - `tests/fixtures/contracts` is now
  copied from meshmaster.dk `cd872a7` - and **three free functions of the library
  changed shape**, which is a compile error in your tree if you call them
  directly (no method of `IMesh`, `IUplink`, `IPlatform`, `IClock`,
  `IJobExtension`, `JobContext`, `ResultWriter` or `TextOut` moved, and neither
  `kMeshReplyDataCap` nor `kMeshPathCap` did):

  - `mm::parseOwnerInfoFirmware(data, len, char*, cap)` is gone, replaced by
    `mm::parseOwnerInfo(data, len, mm::OwnerInfoReply&)` (`agent/ReplyCodec.h`),
    which splits `REQ_TYPE_GET_OWNER_INFO`'s reply at its **first two** newlines:
    the firmware version, the node name, and the owner info as the rest of the
    reply verbatim - `set owner.info a|b` stores a line break, so the third line
    may carry newlines of its own. `kMaxNodeNameLen` (31) and `kMaxOwnerInfoLen`
    (119) are the firmware's own `node_name[32]` and `owner_info[120]`; a longer
    line is kept, cut, with `nodeNameCut` / `ownerInfoCut` set.
  - `mm::writeStatusResult(…, const char* firmware, …)` now takes
    `const OwnerInfoReply* owner` in that place (`agent/JobResults.h`).
    **nullptr** is a read that made no owner-info request and writes none of the
    three fields; a line the reply carried **empty** is written as `""` rather
    than left out, because at the other end an empty reading is stored and an
    absent one is not.
  - `mm::readsFirmwareVersion(job)` is now `mm::readsOwnerInfo(job)`
    (`agent/JobEnvelopes.h`) and **the credential class is no longer part of the
    answer**: the claim's `StatusPayload.version` is the whole of it.
    `mm::transactionsFor("status", class)` is therefore **2 at every class** and
    not 1 below `acl`, so a guest-class status read is priced one transaction
    higher than it was; `transactionsOf` still takes it back to 1 for a claim
    that said the reply is not wanted.

  **What moved behind them**: a `status` read makes the owner-info request at
  **any** class the job goes at, because `MyMesh::handleRequest` checks no
  permission at all for that request and its reply carries the node name and the
  owner info as well as the version - which meshmaster.dk now shows on every
  repeater page (its task p4.30-181). The result carries `nodeName` and
  `ownerInfo` beside `firmware`. **One repeater is not asked**: a login reply
  that said `FIRMWARE_VER_LEVEL` below 2 (`kOwnerInfoVerLevel`) does not know the
  request and would answer it with silence, so it is skipped and the job's log
  says why; a job that reused an earlier job's session was told no level, and not
  knowing is not a reason to skip. A job your mesh answers locally
  (`answersLocally`) follows the same rule and spends no airtime on it.

- **An empty `get` reply is a value, not a raw line** (task p1.10-44). **Nothing
  of a pin moved** - not a method of `IMesh`, `IUplink`, `IPlatform`, `IClock`,
  `IJobExtension`, `JobContext`, `ResultWriter` or `TextOut`, not
  `kMeshReplyDataCap` or `kMeshPathCap`, not a fixture, not the version - and
  nothing of yours stops compiling. What moved is **what one free function
  reads**: `mm::cliReplyValue` (`agent/ConfigRead.h`) now reads a **bare `>` as
  the empty value** rather than as "not a `get` reply". `CommonCLI::handleGetCmd`
  prints every key it knows with one `sprintf(reply, "> %s", ...)`, so a key
  whose value *is* the empty string - an unset `guest.password`, an unset
  `owner.info` - answers `"> "`, which `JobRunner`'s `trimInPlace` makes `">"`;
  that used to be kept as a `raw` line and the key left out of the snapshot.
  `">"`, `"> "` and `">  "` now all give `""`, a value keeps every space inside
  it, and `">240"` with no space at all is still nullptr. **Where you will see
  it**: a `config.read` or a `config.apply` re-read of a key with an empty value
  carries that key in `values` (or `reRead`) as `""` instead of in `raw` (or
  `reReadRaw`) - the same thing the .NET agent carries since meshmaster.dk's task
  p4.23-165 (`CliReplies.ValueOf`), so the two agents' snapshots of one repeater
  agree. It matters because meshmaster.dk reads an **empty** `guest.password` as
  *this repeater has no guest password* and clears the stored one: through this
  library that answer never arrived as a value, so a guest password taken off a
  repeater stayed stored on the server until somebody cleared it by hand. Nothing
  was ever stored *wrongly* - absent was read as "not known", never as "none" -
  so this closes a gap and does not fix a fault.

- **A job your mesh answers locally is *priced* as local by a wake, not as air**
  (task p1.10-43). **One method was added to `IMesh`**, `virtual` **with a body**
  — `bool answersLocallyFor(const char* publicKeyHex) const`, defaulting to
  **false** — so a consumer's tree compiles untouched and a mesh that overrides
  nothing is priced exactly as it was. It is the door of `answersLocally()` (the
  entry below) asked for a **key**, before any job has begun, because the caller
  that needs the answer is the one which prices a claimed job before it runs it:
  `answersLocally()` is true only from a `beginJob` to its `endJob`, so the wake
  could not ask it. The two must agree — `answersLocallyFor(k)` is true exactly
  when a job begun for `k` would make `answersLocally()` true — and on the first
  consumer both are the one comparison `beginJob` already makes. What changed
  behind it: `AgentLoop::wakeJobs` prices a job the mesh answers locally at **its
  reply waits**, with no airtime gaps and at **0 transactions**, and reports that
  figure (*… needs about 272 s, answered without the air*) when the wake is too
  small even for that; a job that is **not** local is priced exactly as before,
  to the second. `AgentMesh` keeps the default: the Heltec has no local door.
  **For the first consumer this is the fix of a refusal that had not happened
  yet**: a 34-line `config.read` of a repeater-v2 unit's own repeater was priced
  at 34 × (20 s + 8 s) = **952 s** of air it was never going to use, against an
  exchange bound of 1 200 s — 248 s of margin on the dearest job that unit runs,
  so a shorter bound, a longer key list or a larger `minGapSeconds` from the
  server would have refused in seconds, at no transaction, the very job task
  p1.10-40 made fast. The same read is now priced at 272 s, and its transaction
  price is 0, so a wake with no air left in its cap can still run it. **Nothing
  else of a pin moved**: not `IUplink`, `IPlatform`, `IClock`, `IJobExtension`,
  `JobContext`, `ResultWriter` or `TextOut`, not `JobReport`, not
  `kMeshReplyDataCap` or `kMeshPathCap`, not a fixture, not the version. A
  consumer that overrides `answersLocally()` should override this one too and
  from the same state: left at the default, its local jobs keep being *priced* as
  air (and still **run** off it), which is the behaviour it has today.

- **A wait counts the sleep: `restore()` is told how long the board was away**
  (task p1.10-42). **No method of `IMesh`, `IUplink`, `IPlatform`, `IClock`,
  `IJobExtension`, `JobContext`, `ResultWriter` or `TextOut` moved**, and nothing
  of yours stops compiling: what moved is `AgentLoop::restore()`, which took a
  **second argument with a default** - `bool restore(const WakeState&, uint32_t
  awayMs = 0)`. `awayMs` is the milliseconds between that `state()` and this
  `restore()`, on a clock that ran through the sleep; **you** measure it, because
  the only clock this library has is `millis()` and the sleep kills it. Every
  duration of a `WakeState` is now re-based against the new clock *after* that
  time is taken off it: the hold is what is left of `holdForMs` and never less
  than nothing - a backoff or a Retry-After alike, since either way the server
  asked for a number of seconds of **real** time - and `failingForMs` is longer
  by it. `restore(s)` with one argument still means *no time passed* and is
  **exactly** the behaviour it had before, which is what a power-on wants and
  what a consumer that cannot measure its sleep should keep passing. `WakeState`
  itself did not move: same 76 bytes, same `kVersion` 1, so the blob in your NVS
  still restores. **For a consumer that sleeps this is the fix of a silence**: on
  8 November 2026 the first repeater-v2 unit woke every fifteen minutes all night
  and meshmaster.dk heard it on about every sixth wake, because a 30 s hold was
  spent five or six seconds at a time - one failed call bought about ninety
  minutes of quiet at fifteen-minute wakes, six hours at hourly ones, and a whole
  day at the backoff's two-minute ceiling. One consequence to know about: with
  the time away counted, `failingForMs` can now reach the reboot guard's ninety
  minutes on a board that sleeps, so `report.rebootWanted` and `rebootWhy` will
  be set where they never were. **Nothing restarts because of it** - in one-wake
  mode the guard is told and not taken, as it always was - but if you act on that
  flag, this is the release in which it starts arriving. Nothing else of a pin
  moved: not `kMeshReplyDataCap` or `kMeshPathCap`, not a fixture, not the
  version.
- **A job your mesh answers locally logs in with no password, at every class**
  (task p1.10-41). **No method of `IMesh` moved**; what moved is one free
  function of a public header, `loginMaterialFor`
  (`agent/LoginSequence.h`), which took a new second parameter — `bool
  answeredLocally` — and is a **compile error in your tree only if you call it
  yourself**, which a consumer has no reason to: `JobRunner` is its one caller
  and asks `IMesh::answersLocally()` for the argument. The decision it makes is
  now the door's first: for a job your mesh answers without the air the material
  is the **empty string at every class**, `admin` included, where a claim with no
  password used to answer *nothing to log in with* and the job was `unauthorized`
  **unsent**. A password the claim did carry is not sent either — the door would
  ignore it — and the job's log says *answered by the board, no password needed*.
  For a job that is **not** local nothing changes at all, down to the log line:
  `admin` with no admin password still refuses, at no transaction. The ACL
  restore is untouched and still gated on the level the claim carried, so a local
  job that carried none restores nothing. **For the first consumer this is the
  fix of a refusal**: every CLI job of a unit's own repeater (`cli`, `reboot`,
  `config.apply`, `clock.sync`, `advert.request`, `acl.set`, `password.set`) is
  `admin`-class, and on 7 November 2026 meshmaster.dk refused a scheduled
  `config.read` of the first repeater-v2 unit's own radio with *no admin password
  stored* — the radio's settings had been lost, so its admin password was a
  random one nobody knows. A unit now configures its own repeater with no
  password stored anywhere, and meshmaster.dk hands none at any class for such a
  pair (`AgentProtocol.DecideAsync`, its task p4.27-175, keyed to the consumer's
  reported version). **Nothing else of a pin moved**: not `IUplink`,
  `IPlatform`, `IClock`, `IJobExtension`, `JobContext`, `ResultWriter` or
  `TextOut`, not `kMeshReplyDataCap` or `kMeshPathCap`, not a fixture, not the
  version. A consumer whose mesh answers `answersLocally()` true must expect the
  **blank** login at any class, including `admin`.
- **A job your mesh answers locally is not on the air, and is no longer paced
  like it** (task p1.10-40). **One method was added to `IMesh`**, `virtual`
  **with a body** — `bool answersLocally() const`, true from a `beginJob` for a
  repeater your mesh reaches by something other than the mesh until that job's
  `endJob`, defaulting to **false** — so a consumer's tree compiles untouched
  and a mesh that overrides nothing is paced exactly as it was. `AgentMesh`
  keeps the default: the Heltec's radio is its own and it has no local door.
  `JobRunner::spend` now skips the airtime gate for such a job: **no wait, no
  slot spent and no transaction counted**, because a transaction is an exchange
  with a repeater *over the mesh* (EXIT-NODES §7) and the budget exists to share
  one radio channel (§8). The lease is unchanged and still ends the job. **For
  the first consumer this is the fix of a reset**: a 34-line `config.read` of a
  repeater-v2 unit's own repeater, served over the UART through `LinkMesh`'s
  local door, was paced at the server's *2 a minute, gap 20 s* and waited about
  40 s before its third line — which outlived the Walter's 30 s task watchdog on
  7 November 2026 — and would have taken eleven minutes for air it never used.
  **Nothing else of a pin moved**: not `IUplink`, `IPlatform`, `IClock`,
  `IJobExtension`, `JobContext`, `ResultWriter` or `TextOut`, not `JobReport`,
  not `kMeshReplyDataCap` or `kMeshPathCap`, not a fixture, not the version.
  What a consumer sees on the wire is a local job's `transactions` falling to
  **0** where it used to count its exchanges, and a result's log carrying one
  more line, *this mesh answers for this repeater without the air*; a consumer
  whose own tests assert a transaction count for a mesh that overrides the new
  method is the one thing to look at. A consumer that implements it must make it
  true for the **whole** of such a job — the library reads it per `spend`, not
  once per job — and false again between jobs.
- **A trace to a direct neighbour goes at the board's width, not one byte**
  (task p1.10-39). **One method was added to `IMesh`**, `virtual`
  **with a body** — `uint8_t floodPathHashSize() const`, the path-hash width
  every flood of that mesh originates, defaulting to **2** — so a consumer's
  tree compiles untouched and a mesh that overrides nothing traces no narrower
  than the rail allows. `AgentMesh` answers `kFloodPathHashSize`. The library
  chooses a width in exactly one place, a trace to a *direct neighbour* (the
  claim carried a path of zero hops, so no hop was learned and the only hash is
  one the library computes from the repeater's key), and it now asks this
  instead of filling in `1`; every other path still keeps the width the claim
  carried, because the library never widens one. **Nothing else of a pin
  moved**: not `IUplink`, `IPlatform`, `IClock`, `IJobExtension`, `JobContext`,
  `ResultWriter` or `TextOut`, not `JobReport`, not `kMeshReplyDataCap` or
  `kMeshPathCap`, not a fixture, not the version. What a consumer sees on the
  wire is a `TraceResult` whose `prefix` fields are **four hex digits** for a
  direct neighbour where they were two — the same thing MeshMaster's .NET agent
  sends (`TraceHandler`: a direct path takes the companion's own mode, D-33) —
  so a consumer whose mesh floods wider than two bytes should override the
  method, and one whose tests assert a two-character prefix for a direct
  neighbour is the one thing to look at.
- **A result's log is one entry per line** (task p1.10-38). `JobLog::add` joins
  its entries with a `'\n'` instead of a `"; "`, as MeshMaster.Agent's own
  `JobLog.Append` does, and when the kilobyte buffer fills it now drops whole
  entries — the cut is the first line break at or after the half, so the log
  never starts mid-sentence. **Nothing moved that a pin asks about**: not a
  method of `IMesh`, `IUplink`, `IPlatform`, `IClock`, `IJobExtension`,
  `JobContext`, `ResultWriter` or `TextOut`, not `JobReport`, not
  `kMeshReplyDataCap` or `kMeshPathCap`, not a fixture, not the version. What
  changed is the **text** inside `JobReport::log`, which is free text the server
  only displays: *The agent's log* on a job's page is a `<pre>` with
  `whitespace-pre-wrap`, so it reads line by line. A consumer whose own tests
  assert on the joined text is the one thing to look at; API §3's 4 000-character
  bound is unchanged, and so is the width on the wire, because the JSON writer
  escapes the byte as `\n`.

## v0.6.0 — a lighter TLS, and two broker slots on both boards

Pile 1.9, tasks 36 and 37 (`tasks/phase-1.9/`), released by task 37.
**`agent-core` 0.6.0** in the three manifests. Nothing else of a pin moved: the
contract fixtures are still meshmaster.dk **`42e4d45`** (77 files) and the
companion frame fixtures still **`f518ebd`** (46); MeshCore is still
**`companion-v1.18.0`, `81dbae59`**; `kMeshReplyDataCap` 184 and `kMeshPathCap`
64, unmoved. **The build underneath moved and nothing above it did.** The four
questions:

- **Did a method move?** **No.** Not one method of `IMesh`, `IUplink`,
  `IPlatform`, `IClock`, `IJobExtension`, `JobContext`, `ResultWriter` or
  `TextOut`, and nothing of `JobRunner`, `JobReport` or `Backoff`: nothing was
  added, nothing changed shape and nothing was removed. A consumer's tree
  compiles against 0.6.0 exactly as it compiled against 0.5.0.
- **Did a `MeshReply` constant move?** **No.** `kMeshReplyDataCap` is 184 and
  `kMeshPathCap` is 64, at the same MeshCore pin task p1.8-34 read them at.
- **Did the fixtures' manifest commit move?** **No.** `42e4d45`, 77 files, byte
  for byte — no `tools/sync-fixtures.ps1` ran in this pile. The wire this
  library writes and reads is the wire `v0.5.0` wrote and read.
- **The version and the pin:** `agent-core` 0.6.0; MeshCore
  `companion-v1.18.0`, `81dbae59`.

What a consumer changes: **nothing**. What a consumer who builds the *Heltec*
firmware of this repository should know, since it is the one thing this release
is:

- **The ESP-IDF is compiled from source under the same Arduino core** (task
  p1.9-36): `framework = arduino, espidf` in `platformio.ini`, core 2.0.17 on
  ESP-IDF 4.4.7 — the pair `espressif32@6.11.0` picks itself — with a committed
  `sdkconfig.defaults` that is the core's own configuration but for ten mbedTLS
  keys, held there by `tools/check-sdkconfig.py` on every CI build. The TLS
  content buffers are allocated while a record is in flight and freed after it
  (`CONFIG_MBEDTLS_DYNAMIC_BUFFER`, 16 KB in and 4 KB out), and the certificates
  are freed after the handshake. The first `pio run` on a machine compiles the
  ESP-IDF; nothing of `lib/agent-core/` is involved, and it still builds and
  tests on a host with no ESP-IDF at all.
- **What that cost, measured on a Heltec V3 on 2 November 2026** (task p1.9-37,
  `src/observer/Board.h`): a broker connection costs **19 468** bytes of
  internal heap where it cost 63 564, and the MeshMaster uplink's handshake dips
  **26 404** where it dipped 81 116 — so **both boards now open two broker
  slots**, where a V3 opened one. The gate, the floor (16 384) and the margin
  (4 096) are unchanged; `src/observer/Connection.h` is the arithmetic and
  `get mqtt.status`'s `memory` line prints it. Two slots on a V4 are expected,
  not walked: no V4 was at hand, so its figures are the V3's measured ones.

## v0.5.0 — the uplink's one connection, an observer that fits, a flood with a scope, and the neighbours

Pile 1.8, tasks 30 to 34 (`tasks/phase-1.8/`), released by task 35.
**`agent-core` 0.5.0** in the three manifests; the contract fixtures moved to
meshmaster.dk **`42e4d45`** (77 files, from `v0.4.0`'s `67c2a4c`, 75) and the
companion frame fixtures are still at `f518ebd` (46); MeshCore
**`companion-v1.18.0`, `81dbae59`** (from `companion-v1.17.1`, `d9296435`);
`kMeshReplyDataCap` 184 and `kMeshPathCap` 64, unmoved. The four questions:

- **Did a method move?** Two additions to **`IMesh`**, both `virtual` **with a
  body**, so a second implementation compiles untouched and sends the heartbeat
  it sent: `floodScope()` — the region every flood of that mesh goes out in, a
  name, `*` for *no transport code at all*, or **`nullptr` for a mesh that says
  nothing about scope** — and `setFloodMode(FloodMode)`, told per send. A mesh
  that overrides neither is the mesh it was. `JobRunner::gate()` gained a
  **non-const** overload beside the const one (additive; the board's neighbour
  sweep shares the one airtime budget through it), and `JobRunner` gained
  `setFloodRetry`, `floodCounts`, `floodUnscopedRetry` and `describeFlood`.
  `JobReport` and `JobContext` are unchanged. Nothing of `IUplink`,
  `IPlatform`, `IClock`, `IJobExtension`, `ResultWriter` or `TextOut` moved, and
  **nothing was removed**.
- **Did a `MeshReply` constant move?** No. `kMeshReplyDataCap` is 184 and
  `kMeshPathCap` is 64, and they are MeshCore's `MAX_PACKET_PAYLOAD` and
  `MAX_PATH_SIZE` at the **new** pin too — task 34 read them there
  (`tasks/phase-1.8/reports/34.md`), which is the point of holding a
  `static_assert` against them.
- **Did the fixtures' manifest commit move?** **Yes** — `67c2a4c` → `42e4d45`
  (task p1.8-32, `tools/sync-fixtures.ps1 -Commit master -Set contracts`).
  **The wire moved by addition and by addition only**: two files appeared,
  `AgentFlood.json` and `AgentHeartbeatFlood.json`, and **not one existing
  fixture's bytes changed** — the sync's `git diff` touches `MANIFEST.txt` and
  nothing else. So the heartbeat's new `flood` object is the whole of the
  change, it is optional, and a consumer that writes none of it is on the wire
  it was on. The companion frame fixtures did not move with the MeshCore bump:
  task 34 found **no change to any frame this library decodes** (tier three,
  *none found*).
- **The version and the pin:** `agent-core` 0.5.0; MeshCore
  `companion-v1.18.0`, `81dbae59`.

What a consumer changes: **nothing**. The one addition, in a few lines:

- **A flood carries its region's transport code** (meshmaster.dk's D-47, task
  p1.8-32). A flood used to be a plain `ROUTE_TYPE_FLOOD` with no codes, which a
  repeater that follows the Danish standard's region map forwards only as a
  wildcard. `agent/FloodScope.h` is the rule and the counting, and it knows
  nothing of hashes or transport codes: `FloodMode` (`Scoped` | `Unscoped`),
  `UnscopedRetry` (`Retry` | `Never`), `kFloodScopeUnscoped` (`*`, the word the
  contract's `AgentFloodScopes.Unscoped` has), `floodModeFor(flood, retry)` —
  the whole decision — `FloodCounts`, and `FloodPolicy`, which holds one
  transaction at a time and is **nestable**, because a request that lost its
  session logs in again inside its own retry loop. `JobRunner` marks a
  transaction over each retry loop (the login's two tries with the probe
  between them, a request or a CLI line and its retry, and the whole of
  `discover`) and asks the policy before every send, so **the second flood of a
  transaction goes unscoped** while the setting is `retry` and a repeater with
  no region map is still found. The arithmetic — the scope's key and the code —
  is the *consumer's*, not this library's: on this board it is
  `src/mesh/TransportScope.h`, SHA-256 over `#name` for the key and
  HMAC-SHA256 over the payload for the code, read off MeshCore's own
  `TransportKeyStore` and `TransportKey::calcTransportCode`. A mesh that
  overrides neither new method floods exactly as it did.
- **The heartbeat's `flood` object**, written after `observer` as the record has
  it: `scope` (left out when the mesh's is `nullptr` — *the companion's own* on
  the wire), `unscopedRetry`, and the three counts `answeredScoped`,
  `answeredUnscoped` and `unanswered`, which are the record's non-nullable longs
  and are always written. `AgentLoop` fills it from the mesh's scope and the
  runner's counts, and **writes nothing at all for a mesh that reports no
  scope** — so an older consumer's heartbeat is byte for byte the one it sent.

Two things of **this firmware's** moved with it, for a consumer who reads this
repository's sources as a worked example:

- **One TLS connection to the server, kept between calls** (task p1.8-30).
  `src/agent/KeepAlive.h` is the policy — reuse while the client sees the
  connection alive and the root has not changed, remake a kept connection once
  inside the call when the request over it got no reply and no TLS error, never
  retry a fresh connection's failure — and `src/agent/AgentClient.cpp` is the
  wire under it. It is deliberately **not** in this library: it is about
  `HTTPClient`'s codes and `WiFiClientSecure`'s probe, which is a board's
  uplink and not the `IUplink` seam. meshmaster.dk holds an idle connection for
  **128 s**, measured; the idle heartbeat is at 120 s.
- **The clock's plausibility floor is 1 November 2026**, not 1 January 2025
  (`src/net/Clock.h`, task p1.8-34). MeshCore 1.18's `ESP32RTCClock` seeds
  itself with its build date and keeps a backup across a reset in `RTC_NOINIT`
  memory; with the old floor a fresh power-on would have read that seed as a
  time kept by the RTC and dated the first heartbeat and every result by it. A
  consumer with its own `IClock` over a radio whose clock does the same has the
  same floor to raise.

## v0.4.0 — the observer in the heartbeat, and where a failed job failed

Pile 1.7, task 28 (`tasks/phase-1.7/`), which closes the pile. Two optional
fields of the agent protocol, one interface addition and nothing else:
**`agent-core` 0.4.0** in the three manifests; the contract fixtures moved to
meshmaster.dk **`67c2a4c`** (75 files, from `79b1e9c`'s 69) and the companion
frame fixtures still at `f518ebd` (46); MeshCore `companion-v1.17.1`,
`d9296435`, unmoved; `kMeshReplyDataCap` 184 and `kMeshPathCap` 64, unmoved.
The four questions:

- **Did a method move?** `AgentLoop::setHeartbeatObserver(IHeartbeatObserver*)`
  is new and **additive**: a consumer that never calls it sends the heartbeat it
  sent before, byte for byte. `JobReport` gained a member, `failedAt`, which
  defaults to `nullptr` and is left out when it is - so a consumer that fills
  the struct field by field compiles and writes what it wrote. Nothing of
  `IMesh`, `IUplink`, `IPlatform`, `IClock`, `IJobExtension`, `JobContext`,
  `ResultWriter` or `TextOut` moved, and nothing was removed.
- **Did a `MeshReply` constant move?** No.
- **Did the fixtures' manifest commit move?** **Yes** - `79b1e9c` →
  `67c2a4c`. **The wire moved by addition and by addition only**: six files
  appeared and **not one existing fixture's bytes changed**, which is worth
  saying plainly because this is the question that breaks a consumer silently.
  The six are `AgentHeartbeatObserver.json`, `AgentObserver.json` and
  `AgentObserverSlot.json` (the observer, below), `JobResultReportFailed.json`
  (`failedAt`, below), `AgentHeartbeatNoKey.json` (a unit whose radio has no
  identity yet - the Walter's sample, nothing of this firmware's) and
  `AgentTransportList.json` (the server's four transport words as a list). So
  the two new members are the whole of the change, both are optional, and a
  consumer that writes neither is on the wire it was on.
- **The version and the pin:** `agent-core` 0.4.0; MeshCore `d9296435`.

What a consumer changes: **nothing, if it has no observer**. The two additions,
in two lines each:

- **The observer in the heartbeat** (meshmaster.dk's D-46). An exit node may
  also be an *observer*, publishing every packet its radio hears to the
  community's MQTT brokers; MeshMaster shows the **state** of that and
  configures none of it. The heartbeat's new `observer` member carries the
  region and, per slot, the preset, the broker, a state word, since when, how
  many messages were published, how many were dropped and the last error. This
  library **does not know what an observer is** - not a broker, not a topic, not
  a key, not a packet - and is not going to: a consumer that has one implements
  `IHeartbeatObserver` (one method, `describeObserver`) and hands it to
  `AgentLoop::setHeartbeatObserver`, and the loop asks it at every heartbeat and
  writes what it says. Giving none, or answering `false`, leaves the member out
  altogether. The strings and the slot array are yours and are not copied: they
  have to outlive the call that posts the heartbeat, as `HeartbeatIdentity`'s
  do. The server bounds each field (preset 32, host 64, state 32, lastError 120
  characters, six slots) and **refuses an over-long one by name while still
  taking the rest of the heartbeat**, because a bad observer object must not
  take an exit node off the air; this library bounds nothing, so a provider that
  sends more than that loses the object and keeps its exit node. The state words
  are a contract: `off`, `waiting-for-wifi`, `waiting-for-clock`,
  `waiting-for-heap`, `connecting`, `connected`, `backing-off`.
- **`failedAt` on a failed result** (meshmaster.dk's task p4.17-136).
  `unreachable` was two things - a login nobody answered, which on stock
  firmware is also a wrong password, and a request lost twice after a login that
  *was* answered - and the page that says *the stored guest password is probably
  wrong* must say it for the first alone. `JobReport::failedAt` is `login`,
  `request` or `nullptr`, and `JobRunner` fills it for every job it runs: the
  step the job last reached. It is **absent** on an `ok` result, on a
  `CredentialClass::None` job (a trace logs in to nobody) and on a job that put
  nothing on the air at all - a type your mesh declines, a lease that had
  already run out, a wake with no room for it - because naming a login that
  never went out would point an owner at a password on the strength of a packet
  nobody sent. A result without the field is read by the server exactly as
  before, so an older consumer is unaffected. If you write `JobReport` yourself
  rather than letting `JobRunner` do it, this is the field to decide about.

One number of **this firmware's** moved with it, for a consumer who sizes
buffers from this repository's: the heartbeat body buffer in
`src/agent/AgentClient.cpp` is **2 048 bytes**, not 1 024. Two slots at the
bounds the server accepts are about 700 bytes of `observer` on top of the ~720
this board's own fields come to, and a heartbeat that does not fit is an exit
node that is offline.

## v0.3.1 — the strings are UTF-8

Pile 1.7, task 25 (`tasks/phase-1.7/`): three defects the review of pile 1.6
found, each in this library and each with a test that failed first.
**`agent-core` 0.3.1** in the three manifests; the contract fixtures **still** at
meshmaster.dk `79b1e9c` (69 files) and the companion frame fixtures still at
`f518ebd` (46); MeshCore `companion-v1.17.1`, `d9296435`, unmoved;
`kMeshReplyDataCap` 184 and `kMeshPathCap` 64, unmoved. The four questions:

- **Did a method move?** No. `JobRunner::meshDeclines(job)` is new and
  **additive** - a `const` predicate that answers the question `runClaimed()`
  already asked itself - and nothing was removed or changed shape. Nothing of
  `IMesh`, `IUplink`, `IPlatform`, `IClock`, `IJobExtension`, `JobContext`,
  `ResultWriter` or `TextOut` moved.
- **Did a `MeshReply` constant move?** No.
- **Did the fixtures' manifest commit move?** No - **and the wire moved all the
  same**, which is why this section exists. It moved for strings that were
  **ill-formed UTF-8 before, and for those only**: `JsonWriter::string()` is now
  the decoder between a stranger's bytes and the wire. Since `v0.3.0` clause 5
  copies a character's own source bytes through, and the decoder behind it
  accepted an overlong form, a surrogate encoded as a character, a code point
  above U+10FFFF and the lead bytes F5-FF - so a repeater whose name carried
  such bytes (`E0 83 85` for an `Å`) sent a result **System.Text.Json
  refuses**, and a result the server refuses is kept and retried at the head of
  the ordered queue, blocking the results behind it until the buffer evicts it.
  The rule is Table 3-7 of the standard now, and each **maximal subpart** of an
  ill-formed sequence is one `\uFFFD` (Unicode 3.9, and what .NET makes of the
  same bytes): a cut `E2 82 41` is one `\uFFFD` and an `A`. Nothing a
  well-formed string makes of this writer changed, `JsonEscaping.json` still
  matches byte for byte, and `test/test_json_escaping` carries a case per
  ill-formed form plus two seeded fuzz tests - **no output of this writer is
  ill-formed UTF-8, whatever the input was**. If your own writer copies source
  bytes through, this is the paragraph to read twice.
- **The version and the pin:** `agent-core` 0.3.1; MeshCore `d9296435`.

What a consumer changes: **nothing, to compile**. What a consumer that
deep-sleeps should know, in two lines:

- **An advert nothing can date is dropped at the write and counted.** A wake
  that keeps adverts is the wake whose heartbeat failed; on a unit with no RTC
  that is also a wake with no clock, and an advert heard on it has no moment to
  write - `millis()` dies with the sleep and the sleep's length is unknown.
  `keepAcrossSleep()` used to store epoch 0 and `restoreKept()` then stamped the
  advert with the moment of the restore, wrong by the whole outage. Both ends
  now drop such an advert and count it in `observationsDropped` (the read end
  too, for an image a build before this one left behind), and
  `WakeReport::observationsNotKept` counts it beside what the store was too
  small for. The log line says which it was.
- **A type your mesh declines is declined before it is priced.** `wake()` prices
  each claimed job against the wake's transaction cap; it now asks
  `jobs_->meshDeclines(job)` first. A type you answer `false` for costs no
  transaction, so there was no budget for it to be too large for - and a job of
  it **bound** to your agent came back `failed`, *wake budget*, where `loop()`
  answered `unsupported` for the same job. It is `unsupported` at no transaction
  in both now.

**There is no `v0.3.1` tag yet, and there may never be one:** pile 1.7 tags
`v0.4.0` when its task 28 has merged (`tasks/phase-1.7/README.md`), and a tag is
a person pushing it either way. A consumer that wants the UTF-8 fix before that
takes this library at the commit, as `docs/library.md` §2.1 describes.

## v0.3.0 — a wake that keeps its budget, a sleep that keeps its results, and a new wire dialect

Pile 1.6, tasks 20–24 (`tasks/phase-1.6/`): opened by task 22, filled by 23,
closed by 24. **`agent-core` 0.3.0** in the three manifests (p1.6-23); the
contract fixtures at meshmaster.dk **`79b1e9c`** (69 files, p1.6-24, moved from
`0595dc2`) and the companion frame fixtures still at `f518ebd` (46); MeshCore
`companion-v1.17.1`, `d9296435`, unmoved; `kMeshReplyDataCap` 184 and
`kMeshPathCap` 64, unmoved. The four questions, answered for the whole section:

- **Did a method move?** `IMesh` gained `supports(jobType)` with a default of
  `true` — **additive**, a mesh that does not override it compiles and behaves
  as before. `MeshReply` lost `viaPath` and `snr` — a **compile error** in a mesh
  that assigned them, and the fix is to delete the two lines. `JobRunner::
  flushResults(size_t&, int&)` is gone (p1.6-21, below). Nothing of `IUplink`,
  `IPlatform`, `IClock`, `IJobExtension`, `JobContext`, `ResultWriter` or
  `TextOut` moved.
- **Did a `MeshReply` constant move?** No.
- **Did the fixtures' manifest commit move?** **Yes — `0595dc2` → `79b1e9c`, and
  this is the dangerous one** (p1.6-24). Nothing of yours stops compiling and the
  **bytes on the wire changed**: meshmaster.dk writes its envelopes with .NET's
  *relaxed* JSON encoder now, so `+`, `<`, `>`, `&`, `'`, the backtick, `/` and
  every assigned character outside ASCII are written as **themselves** where they
  used to be `\uXXXX` — a version with build metadata is `0.3.0+abc1234` and not
  `0.3.0\u002Babc1234`, and a Danish repeater name is UTF-8. A reader must still
  unescape every JSON escape (a quote, a backslash and a control still take one,
  and the server reads either form); a **writer** held to these fixtures byte for byte
  has to move. `agent/JsonWriter.h` has, its header states the rule clause by
  clause, and `test/test_json_escaping` holds it against the new
  `JsonEscaping.json` — a fixture whose one string carries a character per
  clause, which is the file to write your own writer against. The one place this
  writer differs from the encoder on purpose is an *unassigned* code point,
  which needs Unicode's table: it is written as itself, and the same string is
  read either way.
- **The version and the pin:** `agent-core` 0.3.0; MeshCore `d9296435`.

What a consumer changes, in one paragraph: fill `HeartbeatIdentity::transport`
(`"native"` for a unit that is its repeater; leave it for `onboard`); override
`IMesh::supports()` if your mesh cannot carry one of the thirteen (return `false`
for `"trace"` on a UART link and the unit neither advertises it nor is sent one —
a job of it anyway is `unsupported` at no transaction, nothing sent); delete the
two assignments to `reply.viaPath` and `reply.snr`; sync the contract fixtures to
`79b1e9c` **and move your writer to the relaxed rule in the same change**, because
the two are one fact; and, if you sleep, take the store of p1.6-22 and the budget
rule of p1.6-21 below.

- **The wire dialect is the relaxed encoder's** (p1.6-24), answered in full above
  because it is the question a pin exists to ask. What is in the tree for it:
  `JsonWriter::string()` writes the rule of
  `tests/fixtures/contracts/README.md` (*How a string is escaped*) and of
  meshmaster.dk's `docs/dev/unit-protocol.md` §8; `test/test_json_escaping` is a
  suite of its own, the fixture written byte for byte and then a case per clause,
  so a failure names the clause; `AgentHeartbeatNoWake.json` is the second
  heartbeat fixture and `test_envelopes` reads it whole instead of cutting the
  first; and `JobCredential.json`'s sample passwords are fifteen characters and
  fewer (meshmaster.dk's p4.15-131, the length a repeater can actually keep), so
  the tests of the cut path build their own long one. What it saves on the air:
  five bytes per `+`, five per `>`, four per Danish letter — a CLI reply that
  starts `> ` is 19 bytes where it was 24, and `Grenå` is 8 where it was 12.
  **Nothing of the Heltec's behaviour changed** but those bytes.
- **The interface says what it is** (p1.6-23). `HeartbeatIdentity` gained
  `transport`, defaulted to `"onboard"` and read at every heartbeat: the unit of
  D-45 sends `native`, the Heltec's bytes are unchanged. `IMesh::supports(type)`
  is new, defaulted `true`: asked for each of the thirteen when the heartbeat is
  built — a type answered `false` is **not advertised**, the extension's types
  follow as before — and once more before a job of one of them is run, so a job
  the mesh declines is reported `unsupported` with nothing sent and `beginJob`
  never called. The Heltec's `AgentMesh` overrides nothing and advertises the
  thirteen as before. `MeshReply::viaPath` and `MeshReply::snr` are **gone**:
  every mesh filled them and nothing read them (`hasPath()` is how the discover
  job reads a returned path; the SNR a trace result carries is `traceFinalSnr`).
  `docs/library.md` §3 and §7.1 say what is now possible, and §7.1 keeps the two
  limits that remain.
- **`agent/IRetained.h` is a new, optional interface** (p1.6-22): a store for
  the one image a sleep keeps — `capacity()`, `write(image, len)`,
  `read(out, cap)`. `AgentLoop::setRetained(store, scratch, len)` hands it in
  and `AgentLoop::restoreKept()` reads the image back after a boot; the end of
  every `wake()` writes the next one, only when it would differ. The image's
  layout, version (`kKeptImageVersion` 1) and CRC-32 are `agent/KeptImage.h`'s,
  and a torn, short, foreign or corrupt image is refused whole and counted
  (`keptImagesRefused()`). Nothing of a consumer that passes none changes.
- **`HeardAdvert` gained `heardAtEpoch`** (p1.6-22), 0 unless the advert came
  back from a kept image; the batch prefers it when set. Additive — a mesh that
  fills the struct as before is unchanged — but `sizeof(HeardAdvert)` grew by
  four, so the 64-entry ring is 256 bytes larger.
- **`WakeReport` gained `keptImageWritten`, `resultsNotKept` and
  `observationsNotKept`** (p1.6-22); `ResultBuffer` gained `at()`,
  `dropNewest()` and `addDropped()`, `ObservationRing` gained `dropNewest()`
  and `addDropped()`, and `JobRunner::results()` has a non-const overload for
  the loop's use. All additive.
- **`agent/UplinkMeter.h` is a new public header** and `AgentLoop` owns one
  (p1.6-21): every call to the server goes through it. `JobRunner::flushResults
  (size_t&, int&)` is **gone** and `flushResults()` returns `int`;
  `JobRunner::setMeter` is new and `AgentLoop::begin` calls it, so a consumer
  that drives both objects needs no change. `WakeReport` gained `uplinkMs` and
  `resultsKept`. The behaviour to know: `Budget::postBytes` and
  `uplinkSeconds` now cover the claim and every job result, so a wake with a
  tight cap keeps results it used to post.
- **`agent/Json.h` is a new public header** (p1.6-20): ArduinoJson reached
  through it, with the one warning the dependency raises under a sanitised
  `-O2` turned off for its code alone. And `ConfigReply.cpp`'s parameter named
  `unix` is `epoch`, so the library builds in a `gnu++` dialect on a Linux host.

## v0.2.0 — the agent core as a library

**The library's first release.** `agent-core` 0.2.0; contract fixtures at
meshmaster.dk `0595dc2` (47 files) and companion frame fixtures at `f518ebd`
(46); MeshCore `companion-v1.17.1`, `d9296435`.

The portable half of the agent — the job runner, the agent loop, the envelopes
and results, the reply codec, the config read and apply engines, the login
sequence, the sessions, the airtime budget, the observation ring, the offline
result buffer, the rate limiter, the backoff, the server policy and the log's
formatting — moved out of `src/` into `lib/agent-core/` and now stands on three
interfaces. Pile 1.5, tasks 15–19 (`tasks/phase-1.5/`).

For a consumer, everything is new, so the list is of what exists rather than of
what changed:

| | |
|---|---|
| interfaces | `agent/IMesh.h`, `agent/IUplink.h`, `agent/IPlatform.h` (with `mm::IClock`), `agent/IJobExtension.h` (with `JobContext` and `ResultWriter`), `agent/TextOut.h` |
| `MeshReply` constants | `kMeshReplyDataCap` 184, `kMeshPathCap` 64 — MeshCore's `MAX_PACKET_PAYLOAD` and `MAX_PATH_SIZE` at the pin above |
| driving it | `AgentLoop::loop()` as before, and `AgentLoop::wake(intent, budget)` for a device that sleeps, with `state()` / `restore()` across the sleep (a 76-byte POD, `WakeState::kVersion` 1) |
| the heartbeat | two optional fields, `nextCheckInS` and `maxTransactionsThisWake` (EXIT-NODES §6, *An agent that paces itself*), written only when set. An agent that sets neither writes the bytes it always wrote |
| the ESP-IDF face | `lib/agent-core/CMakeLists.txt` and `lib/agent-core/idf_component.yml`, **never built in this repository** — the first real IDF build is the first consumer's |
| the fakes | `test/support/FakeMesh.h`, `FakeUplink.h`, `FakePlatform.h`, header-only and written to be copied ([`docs/host.md`](docs/host.md)) |

**The Heltec's behaviour did not change** — that is the whole claim of the pile,
and every task asserted it: no existing native test's assertion changed through
any of the five, and the board images moved only for reasons each report names
byte by byte.

Two things a consumer should know that are not in the list:

- **`IMesh::uniqueTime()` was proposed and is not there.** Every send draws its
  own timestamp and hands it back (`sendCli`'s `timestamp&` out-parameter is the
  shape this took), so nothing in the library asks a mesh for one.
- **`overheard()` has nothing behind it on a link client yet.** The repeater-v2
  link protocol's `MESH_COUNTERS` carries no `overheard` figure and no count by
  payload type; until it does, an implementation over that link reports what it
  can. It is an open item for the design repository, not a library change.

## v0.1.4 and earlier

Firmware-only releases of the Heltec exit node, from a board that boots
(`v0.1.0`) to the guest door (`v0.1.4`). **The library did not exist**, so there
is nothing for a consumer to pin at those tags; `v0.2.0` is the first.
