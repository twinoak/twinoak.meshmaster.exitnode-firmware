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
