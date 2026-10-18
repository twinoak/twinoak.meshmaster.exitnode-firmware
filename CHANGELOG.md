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

## v0.3.0 (unreleased)

Opened by task p1.6-22; tasks 23 and 24 fill and close it. `agent-core` is
still `0.2.0` in the three manifests until 23 moves it, and the fixtures'
manifest commit is still `0595dc2` until 24 moves it.

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
