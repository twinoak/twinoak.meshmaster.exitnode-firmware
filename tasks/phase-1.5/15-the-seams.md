# 15 — The seams

**Depends on:** nothing (pile 1.4 merged).
**Spec:** the hardware repository `twinoak.meshcore.standard-repeater-design-v2`, `software/spec/proposals/03-agent-core-as-a-library.md` (all of it — this pile is that proposal; read with `git -C ../twinoak.meshcore.standard-repeater-design-v2 show main:software/spec/proposals/03-agent-core-as-a-library.md` and record the commit) and `software/spec/LINK-PROTOCOL.md` §6 (what the Walter's end of `IMesh` will be able to do — the interface must be implementable with those requests); this repository's `src/mesh/AgentMesh.h` (the public surface), `src/agent/JobRunner.{h,cpp}` (`begin`, `claimAndRun`, `LoginOnTheAir`, `ApplyOnTheAir`, `idle`, `spend`, the file-static scratch), `src/agent/AgentLoop.{h,cpp}` (`begin`, `loop`, `buildHeartbeat`, `noteFailure`), `src/agent/AgentClient.{h,cpp}` (`Call`, the five calls, `TlsRoots.h`), `src/agent/LoginSequence.h` (`LoginExchange`), `src/agent/ConfigApply.h` (`ApplyExchange`), `src/Health.h`, `src/Log.h`, `src/settings/Store.h`; `tasks/phase-1.1/08-the-host-harness.md` (what it wanted; this pile supersedes it) and the FOLLOWUPS of 1.2–1.4 about it.
**Size:** one session.
**Model:** claude-fable-5-1

## Goal

Everything hard about being a MeshMaster agent is in `src/agent/` and works
on the air. What keeps another firmware from running it is three assumptions
baked into `JobRunner`, `AgentLoop` and their callers: the mesh is in the same
process (`AgentMesh&`), the uplink is `AgentClient` over WiFi, and the platform
is Arduino on an ESP32. This task puts an interface in front of each, moves the
callers onto the interfaces, and changes **nothing** the Heltec does — the
proof is that every native test passes untouched and the board image is the
same to within three vtables.

## Deliverables

- **`IMesh`** (`src/agent/IMesh.h`): `AgentMesh`'s public surface as the
  runner uses it and nothing more — `beginJob`, `endJob`, `setDirectPath`,
  `forgetPath`, `hasPath`, `pathHopCount`, `describePath`, `sendLogin`,
  `sendRequest`, `sendCli`, `sendDiscovery`, `sendTrace`, `awaitReply`,
  `awaitText`, `reply`, `uniqueTime`, `counters`, `overheard`, `loop` (the
  pump the runner calls while idling) and `setRing`. Two changes the Walter's
  end needs, made here so the in-process end loses nothing:
  - `sendCli(text, sensitive) -> SendResult` **returns** the timestamp it
    used through an out parameter instead of taking one; the runner's retry
    draws a fresh one by calling again. `ApplyExchange::nextTimestamp` goes
    the same way or disappears — read `ConfigApply` and decide.
  - the `MeshReply` the runner reads (`kind`, `data`, `len`, `text`, the
    `trace*` fields) is a struct of the library's own, sized by named
    constants that `AgentMesh` asserts against MeshCore's at compile time,
    so that `IMesh.h` includes nothing of MeshCore.
  `AgentMesh : IMesh`, with the adapter code inside `AgentMesh` itself.
- **`IUplink`** (`src/agent/IUplink.h`): `fetchConfig`, `postHeartbeat`,
  `postObservations`, `claimJobs`, `postJobResult`, `hasConfig`, `config`,
  `last`, `lastCall` — the `Call`/`Outcome` records moved into the header,
  free of `Root` (which is WiFi's business; keep it on `AgentClient` and give
  `Call` a small `const char* via` instead) — **and two calls the reference
  agent never makes but the protocol has** (`POST /agent/v1/jobs/{id}/extend`,
  `POST /agent/v1/jobs/{id}/progress`; meshmaster.dk `spec/API.md` §3):
  `extendLease(jobId, seconds)` and `postProgress(jobId, body, len)`.
  `AgentClient : IUplink` implements them in the same shape as the five,
  with a test against `FakeServer`'s pattern; nothing on the Heltec calls
  them yet.
- **`IJobExtension`** (`src/agent/IJobExtension.h`): the seam PLUGINS §3
  describes as *handlers compiled into the agent* — `int typeCount()`,
  `const char* type(i)`, `bool run(const ClaimedJob&, JobContext&,
  ResultWriter&)`. `JobRunner::setExtension(IJobExtension*)`: a claimed job
  whose type is none of the thirteen is offered to the extension **before**
  it is answered `unsupported`; `AgentLoop::buildHeartbeat` appends the
  extension's types to `jobTypes`. The runner still owns pacing, the lease
  check, the result envelope and the log; the extension writes the `data`
  object and the outcome. The Heltec sets no extension, and its heartbeat is
  unchanged.
- **`IPlatform`** (`src/agent/IPlatform.h`): `millis()`, `delayMs()`, a
  32-bit `random()`, the log sink (`mm::logf`'s target — `Log.h` already
  separates the two; the sink becomes a pointer the platform sets), `now()`
  and `hasTime()` (what `AgentClock` provides; `AgentClock : IPlatform`'s
  clock half, or an adapter), `freeHeap()` and `minFreeHeap()` for the
  heartbeat, and `rebootForHealth(why)` (what `healthRebootNow` does). The
  Arduino implementation is the current calls, in `src/platform/Esp32Platform.cpp`.
- **Settings out of the agent.** `AgentLoop` and `AgentClient` read NVS
  through `SettingsStore` for the radio block, the URL and the token. The
  library side takes **values**: a `HeartbeatIdentity` struct (version,
  board, companion firmware, radio parameters) filled by `main.cpp` and
  refreshed when a `radio.*` setting changes; the URL and the token stay
  `AgentClient`'s (it is the WiFi uplink, board side). `findKey` leaves the
  two files.
- **The scratch becomes state.** `JobRunner`'s file-static buffers
  (`g_jobs`, `g_data`, `g_body`, `g_log`, …) move into one `JobRunner::Scratch`
  the runner owns — still one instance, still statically allocated by the
  board (`main.cpp` holds it), but a second runner in a test is now
  possible, and the host build is not three kilobytes of globals in a
  translation unit it does not control.
- **The callers.** `JobRunner::begin(IMesh&, IUplink&, IPlatform&, pump)`;
  `AgentLoop::begin(IUplink&, IPlatform&, IMesh&, JobRunner&, const
  HeartbeatIdentity&)`; `main.cpp` wires the real things. No `Arduino.h`,
  `esp_system.h`, `WiFi`, `Preferences` or MeshCore include remains in
  `JobRunner.*`, `AgentLoop.*`, `LoginSequence.*`, `ConfigApply.*` or the
  three new headers — a native test greps for it (the shape of
  `test_tls_policy`'s walk), and 16 moves the files once that is true.
- **08 is superseded.** The report says so in one paragraph, and the pile's
  FOLLOWUPS asks Thomas to delete `origin/task/p1.1-08-the-host-harness`.
  Nothing from that branch is merged.

## Acceptance criteria

- [ ] Every existing native test passes **without a change to a test file** (`pio test -e native`), and `pio run -e heltec_v3 -e heltec_v4` builds; the report gives the flash and RAM sizes before and after, and they differ by less than 1 kB.
- [ ] The three interfaces are implementable from `LINK-PROTOCOL.md` §6 alone: the report walks `IMesh` method by method and names the link request each would become, and raises as a specification gap anything that has none.
- [ ] The grep test for platform includes in the portable files is in the tree and green.
- [ ] `sendCli` returns its timestamp; the apply engine's tests still pass; `log.frames on` output is byte-identical for a scripted login (compare against a captured line from a 1.4 report or the bench, cited).
- [ ] The report lists every method of `AgentMesh` that is **not** on `IMesh`, and why.

## Not verified here

The Heltec on the air with the new wiring — the bench walk of `docs/console.md`
on the next release. Nothing should differ; that is the point.

## Out of scope

Moving files (16). Fakes and host tests of the runner (17). The Walter's
implementations of any seam (its phase 4).
