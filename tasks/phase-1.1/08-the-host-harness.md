# 08 — The host harness

**Depends on:** 07.
**Spec:** meshmaster.dk `spec/EXIT-NODES.md` §5–§7 and `spec/API.md` §3; its `src/MeshMaster.MeshCore/Fake/FakeCompanion.cs` (the behaviour to match, not the code); `docs/upstream.md`; `tasks/phase-1/reports/02–05.md` (what each handler sends and expects).
**Size:** one session, and the largest of the firmware so far.
**Model:** claude-opus-5

## Goal

Run the firmware's agent on Linux, against a real meshmaster.dk and a
fake mesh, and watch every job type go claim → air → result → settled
without a radio. The Pi agent has had this since phase 2 (its
`FakeCompanion`, the compose stack, `AgentEndToEndTests`); the firmware's
handlers were written against the same firmware source and have only
ever been exercised one function at a time. The board is in the post.
This is what the wait is for.

## Deliverables

- **Two seams, both already implied by the code.** `JobRunner` talks to
  the mesh through `AgentMesh` (`mesh::Mesh`) and to the server through
  `AgentClient` (Arduino HTTPS). Put an interface in front of each — the
  four primitives task 03 wrote out (`createAnonDatagram`, `createDatagram`,
  `createTrace`, `sendDirect`/`sendFlood`) plus the three callbacks, and
  the client's request/response — so the board build binds the real ones
  and the host build binds these two:
  - **`HostMesh`**: a fake repeater behind the mesh interface that answers
    as stock firmware at `d9296435` does — login (blank from a key in its
    ACL at the entry's level; a password rewrites the entry's role, task
    60's finding; a key not in the ACL gets silence), the four binary
    requests (status, telemetry with AES padding, neighbours paged by nine,
    the access list at 23 with padding), the CLI (`get`/`set` with the
    firmware's ranges and reply spellings, `setperm`, the region loader's
    load mode with its blank-line terminator and the *equal timestamp is a
    retry* rule, `password`, `reboot` as silence), traces with per-hop
    SNR, and knobs the tests need (drop a reply, refuse a value, an ACL of
    24, a region map over 160 characters). It is a **second reading of the
    firmware source**, written from `simple_repeater/MyMesh.cpp` and
    `CommonCLI.cpp` at the pin — not a port of `FakeCompanion.cs` — so the
    two fakes can disagree with each other and one of them be wrong.
  - **`HostClient`**: plain HTTP for the host (libcurl, or a minimal
    client), with `http://` allowed **only** in the host build and only
    for a loopback or RFC 1918 host, so the board's `https://`-only rule
    and its verified chain are untouched and a grep still finds no
    insecure setting in the tree.
- **`[env:host]`** in `platformio.ini`: the whole agent loop (`AgentLoop`,
  `JobRunner`, the handlers, the ring, the result buffer, the sessions,
  the console parser driven from stdin) compiled for Linux with the two
  seams bound to the fakes; `pio run -e host` produces
  `.pio/build/host/program`, which takes `--url`, `--token`, `--board`
  and a scenario file for `HostMesh` (which repeaters exist, their
  passwords, their ACLs, their configurations).
- **Against the compose stack, on the workstation.** `tools/host-run.ps1`:
  checks out meshmaster.dk at a named commit beside this repository (or
  uses the clone that is there), brings its compose stack up, registers
  an exit node through the API with a `config`-scoped token (the stack's
  seed admin), associates the scenario's repeater through the wizard's
  API, runs the host program for one cycle of every job type — Read now,
  the configuration read, a derive from the `dk` profile and an apply of
  one key, the console, a rotation, a reboot — and asserts on the
  server's side (the jobs page JSON, the snapshots, the change rows, the
  association still `active`, `aclRestored: true`) and on the fake's side
  (what was sent, once each, timestamps increasing). The script is the
  bench walk's desk twin and says so.
- **Against a recorded stub, in CI.** CI cannot run meshmaster.dk (no
  image is published, and its tests need MySQL). So `tools/host-stub.py`
  replays the server: the claim/result exchanges recorded from one
  workstation run (`tests/fixtures/server/*.json`, redacted — no real
  token, the scenario's passwords only), and CI runs the host program
  against it for the same cycle and asserts the same things on the
  fake's side. The recording is refreshed by the workstation script with
  `-Record`; a stale recording fails loudly with the field that differed.
- **What it must find or prove.** The report lists, per job type, the
  transaction count the fake saw against EXIT-NODES §7's row, the result
  bytes against the contract fixtures, and every place the firmware and
  `FakeCompanion.cs` would have answered differently (there will be
  some; each is either a firmware bug, a .NET bug, or a difference in
  the two readings of the source — say which, and file the .NET ones in
  `tasks/phase-1.1/FOLLOWUPS.md` for meshmaster.dk).
- **Docs.** `docs/host.md`: how to run it, what the scenario file holds,
  how to record, and the sentence that this proves the protocol and not
  the radio.

## Acceptance criteria

- [ ] `pio run -e host` builds on Linux (CI) and in the workstation's Docker container; `pio run -e heltec_v3` still builds with the real bindings and the image's size moved by nothing but the interface.
- [ ] Against the stub in CI: all thirteen job types run one cycle, every result equals its contract fixture's shape, every command was sent once, timestamps strictly increase, the restore is the last command of every admin session, the password buffer is zero after each job.
- [ ] Against the compose stack (the report's log): the association reaches `active` through the host program, Read now succeeds, the config read ends with `aclRestored: true`, an apply of one key writes its `device` row, a rotation swaps the password, and the exit node's page shows `onboard`.
- [ ] `grep` for insecure TLS and for `http://` outside the host binding finds nothing.
- [ ] The report's table of differences between `HostMesh` and `FakeCompanion.cs`.

## Out of scope

Anything the radio decides (timing on the air, SNR, the real loader's
behaviour); the board build's architecture (one thread, 06).
