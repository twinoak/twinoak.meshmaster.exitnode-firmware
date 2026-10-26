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
  *Picked up by meshmaster.dk pile 4.15, task 131 (18 October); this end's tests follow in 1.6, task 24.*
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
  *Picked up by meshmaster.dk pile 4.15, task 131 (`AgentHeartbeatNoWake.json`), and 1.6, task 24.*

## Left by 19 (14 October)

All seven are for Thomas and the next piles rather than for a task of this
pile; the pile is finished when `v0.2.0` is tagged, and none of them blocks that.

- [ ] **Tag `v0.2.0`, then walk `docs/console.md` on it** (Thomas). The release
  workflow is unchanged: a tag `vX.Y.Z` on a commit CI has passed is what makes
  the images. The walk is short because nothing the board prints changed in this
  pile — the four reports (`jobs`, `sessions`, `server`, `errors`) now go through
  `TextOut`, the log's `[+123s]` stamp is the library's, and `uniqueTime()` is
  called through no vtable — and the one thing three reports have now asked for
  and no report has produced is **one `frame tx` line pasted into a report**
  (15, 16 and 17 each asked; a session cannot produce one). Note also that
  **AarhusC and Grindsted are still on `v0.1.3`** and Risskov on `v0.1.4`
  (1.4's follow-ups): flashing them is also the four guest-door checks 1.4 left,
  and `v0.2.0` is the obvious image to do it with.
- [ ] **The ESP-IDF component is still unbuilt** (the Walter's phase 4, task
  01). `lib/agent-core/CMakeLists.txt` and `idf_component.yml` have been parsed
  and held against the files on disk and never compiled. Two things to expect
  there, neither of them a change to the library's code if it is wrong: whether
  `bblanchon/arduinojson` 7.4.3 is on `components.espressif.com` under that
  exact name, and whether an empty `REQUIRES` survives the component manager
  adding the managed dependency. Until it has happened once, *the component is
  unverified* — `docs/library.md` §2 says so in as many words, and so does the
  last section of `docs/upstream.md`.
- [ ] **`MeshReply::viaPath` and `MeshReply::snr` are written by every
  implementation and read by nobody.** Found by writing `docs/library.md` §3.1
  method by method: the runner reads `kind`, `data`, `len`, `text` and the five
  `trace*` fields, and nothing anywhere reads the other two (`AgentMesh` fills
  them, `FakeMesh` mirrors them, no caller looks). They are not free — `viaPath`
  is an extra flag every link client has to carry out of §6.3's record, and
  `snr` is a float a RAK has to be asked for. Either give them a reader (a
  result's `snr` is the repeater's own figure today, and `pathUsed`'s provenance
  could carry `viaPath`) or drop them from the struct, which is a `MeshReply`
  change and therefore a `CHANGELOG.md` line and a minor version. Not this
  task's: 19 may not change an interface it is documenting.
- [ ] **Two shapes a reviewer of this pile may want moved, and both are cheap**
  (noted by their own tasks, repeated here so they are not lost): `Budget` has
  no transaction cap because the cap is the intent's one number, stated to the
  server and enforced locally — "three lines and a `static_assert` away" if it
  should be there too (18); and `test/test_seams`' thirteen named files are a
  subset of what `test/test_library` walks, kept because deleting an overlap is
  a test file's worth of risk for nothing (16).
- [ ] **What the host suite cannot prove about the agent, and nothing here
  will.** Two of them are for a bench and one is for a job type nobody has
  written: the CLI replies' exact spellings (`OK - loaded N regions`, the region
  listing, `password now: …`), which no `.hex` fixture carries because they are
  text and not a frame layout; the repeater's own replay rule for a CLI line,
  which only a real repeater's clock can prove; and `IUplink::extendLease` /
  `postProgress`, which are held to their contract fixtures and called by
  nothing in the library — the first job type that outlives a lease is what
  will drive them for real.
- [ ] **The Walter's phase 4, task 02 expects a word the library cannot say**
  (Thomas, in `twinoak.meshcore.walter-firmware`). Read while checking
  `docs/library.md` against the reader it is for: that task's *local door*
  deliverable says that for a core job type the unit's own repeater cannot serve
  over the UART, `LinkMesh` should "return `Failed` so the runner answers
  `unsupported`". It will not. `SendResult::Failed` is reported **`failed`**
  (*the packet could not be built*), and `unsupported` is the runner's own word
  for a type nobody handles — there is no path from inside `IMesh` to it. Two
  smaller things in the same breath, both now in `docs/library.md` §7.1: the
  heartbeat always advertises **all thirteen** types plus the extension's, with
  no way to advertise fewer, so the server may route a `trace` to a unit whose
  mesh cannot trace; and a consumer cannot change what one of the thirteen does.
  Either that task's wording changes (`failed` with a log line is the honest
  outcome, or silence and `unreachable`), or the library grows a way for a
  consumer to advertise fewer types — which is a `CHANGELOG.md` line, a minor
  version and not this pile's.
- [ ] **The heartbeat's `transport` is a constant, and the next consumer needs
  another value.** `AgentLoop::buildHeartbeat` sets `hb.transport = "onboard"`
  unconditionally and `HeartbeatIdentity` has no field for it, so a consumer
  cannot say what it is. The Walter's phase 4, task 04 needs **`native`**
  (meshmaster.dk's `AgentTransports`, and SYSTEM S-1: that unit *is* its
  repeater), which means its exit node would register as onboard and the page
  would say so. The fix is one defaulted member of `HeartbeatIdentity` —
  `const char* transport = "onboard";` — read by `buildHeartbeat`, which changes
  no byte of the Heltec's heartbeat and no test of this repository, plus a
  `CHANGELOG.md` line. **19 did not make it**: the pile's whole claim is that
  the board's bytes did not move, and the task that writes the hand-over
  document is the wrong one to change the interface it is documenting on the
  commit being tagged. `docs/library.md` §7.1 says the limitation plainly so
  that nobody writes a consumer against a transport it cannot send. Beside it,
  smaller: the heartbeat's `observationsDropped` can only be the
  `ObservationRing`'s own drops, so a consumer whose radio or link lost adverts
  *before* the ring has nowhere to report them.

## Review of 15–19 (15 October) — the pile, after it landed

Read against the five reports, the items above and the code at `v0.2.0` (`bbe7ae0`). 14 October,
13:17 → 18:56, 5 h 39 min, no stop (15 91 min, 16 58, 17 93, 18 60, 19 36); 356 native cases in
22 suites. The Heltec held: of the pre-existing suites only `test_envelopes` changed in substance.
The one wake behaves as reported within one boot; across a deep sleep it does not, and its byte cap
leaks — those two matter. The Walter's p4-01 then built the library as an ESP-IDF component with
no edit and found two upstream issues, listed here. The items *left by* 15 and 17 above stand.

- [x] **[1.6] A wake's byte cap and uplink seconds do not bound the job step.** `JobRunner::post()`
  starts with the unbounded `flushResults()` (`JobRunner.cpp:1374–1377`, `1405–1409`), and
  `runClaimed` and `reportNotRun` post through it: a result `wakeResults` kept back as `NoBudget` is
  flushed with the whole offline buffer by the claim's first job, counted in neither
  `report.bytesPosted` nor `uplinkSpentMs_`. `AByteCapPostsAsManyBatchesAsItFits…` exercises only
  observations. Pass the wake's `bytesLeft` into `post()`, time the job-step posts into the uplink
  seconds, and test a kept result plus a claimed job under a cap.
- [x] **[1.6] [walter] The offline result buffer and the observation ring do not survive a deep
  sleep.** `WakeState` (`AgentLoop.h:213–227`) carries backoff, hold, interval, budget, `halted` and
  counters, not `results_` or `ring_`. A result whose post failed is lost on a unit that sleeps, and
  its job stays leased until the server's lease lapses — "no job left leased and silent" holds only
  within one boot. `AWakeFlushesWhatTheLastWakeCouldNotPost…` and the hour-later half of `AByteCap…`
  reuse one `AgentLoop` without `restore()`. Expose both buffers for retained storage or a journal,
  or say in `docs/library.md` §6 that they are RAM and the consumer keeps them (or loses them) on
  purpose. The Walter's RTC memory is nearly full (its phase-3 review): decide with that in view.
- [x] **[1.6] A parameter named `unix` in `ConfigReply.cpp:41,48`.** GCC predefines `unix` as 1 in
  every `gnu++` dialect on a Linux host: fifteen errors (Walter p4-01 report, *Reported upstream* §1,
  with the diff). The host check compiles only `-std=c++17` and xtensa has no such predefine, so
  nothing here sees it. Rename to `epoch`; add a `gnu++17` pass to `tools/check-library-host.py`.
- [x] **[1.6] ArduinoJson 7.4.3 raises `-Wmaybe-uninitialized` under GCC 13 at `-O2`**
  (`ObjectImpl.hpp:36`, via `ConfigReply.cpp` and `JobEnvelopes.cpp`; Walter report §2). The host
  check never builds at `-O2`, and `library.json`'s `-Werror` reaches every PlatformIO consumer. Add
  an `-O2` pass; exempt the warning per file or move ArduinoJson.
- [x] **[1.6] The heartbeat's `transport` is hard-coded** (`hb.transport = "onboard"`,
  `AgentLoop.cpp:177`; `HeartbeatIdentity` has no field); the Walter needs `native`. One interface
  task with 19's other limitations — no path from `IMesh` to `unsupported`, all thirteen types
  always advertised, `MeshReply::viaPath`/`snr` never read — one CHANGELOG entry, `v0.3.0`.
- [ ] **[walter] p4-02's local-door wording:** `SendResult::Failed` from `LinkMesh` is reported as
  `failed`, not `unsupported`, until the task above lands.
- [ ] **[Thomas] `docs/library.md` §2 says to pull by `git:` URL at `v0.2.0`**, which cannot work for
  a private repository: describe the checked copy the Walter used, or the read token it needs.
- [ ] **[Thomas] Accept the size on the record.** Against `v0.1.4`: V3 +208 B RAM, −8 588 B flash;
  V4 +208 B, −8 496 B — but only because 17 kB moved from `.dram0.data` to `.bss`; code and rodata
  grew about 8.3 kB (`.flash.text` +5 472, `.flash.rodata` +2 836), every byte named in 15–18.
  "Within ~1 kB" was not literally met.
- [ ] **[reviewer]** "No assertion changed" is slightly too strong:
  `TheSameInputMakesTheDotNetFixtureByteForByte` now sets the two new fields because the fixture
  changed in place; the Heltec's bytes are held by `AnAgentThatProposesNothing…`, which cuts the
  fixture at `,"nextCheckInS":` — sound, and the only anchor until meshmaster.dk adds a second
  heartbeat fixture (17's and 18's items for meshmaster.dk: `MaxPasswordLength` 128 → 15, that
  fixture).
- [ ] **[Thomas] Bench walk of `docs/console.md` on `v0.2.0`**, with one `frame tx` line pasted into a
  report (15–17 each asked; none has one). AarhusC and Grindsted are on `v0.1.3`, Risskov on
  `v0.1.4`; 1.4's guest-door checks go on the same flash.
- [ ] **[Thomas] Housekeeping:** commit the working tree's `tasks/run-all.ps1` (the limit pattern now
  knows *session limit* and *resets …*); confirm `origin/task/p1.5-15-the-seams` is gone on GitHub.
- [ ] **[reviewer] 19's "the ESP-IDF component is still unbuilt" is answered:** the Walter's p4-01
  registered `lib/agent-core/CMakeLists.txt` unedited, ArduinoJson from the registry, empty
  `REQUIRES`, 45 runner and 31 loop cases green there under ASan/UBSan.

### Fine as it is

- **Credentials** wiped before and after a failed claim, before posting, on not-run, and
  `endClaim()` on every wake path.
- **Reboot guard** in a wake sets `rebootWanted` and does not reboot.
- **Backoff across a sleep** re-based by `restore()`; a 401 survives.
- **Claim sizing:** never more than the cap holds; not-run answered `failed` with nothing sent.
- **TLS scan** walks `lib/`; `TlsRoots.h` stayed; rails paths green; `AgentMesh`'s only diff is `sendCli`.
- **Library boundary:** an include allow list, `-Werror` carried by `library.json`, proved by a probe.
- **Heltec image:** `wake()` is garbage-collected out.
- **Fixtures:** contracts at meshmaster.dk `0595dc2`, companion at `f518ebd`.

### Picked up (18 October)

The five `[1.6]` items are tasks 20–23 of pile 1.6
([`../phase-1.6/README.md`](../phase-1.6/README.md)): the `unix` name and the
`-O2` warning in 20, the job step's budget in 21, what a sleep keeps in 22, the
transport and 19's other interface limits in 23. The `[Thomas]` item about
`docs/library.md` §2 is in 24. `v0.2.0` is tagged.
