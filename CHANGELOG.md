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
