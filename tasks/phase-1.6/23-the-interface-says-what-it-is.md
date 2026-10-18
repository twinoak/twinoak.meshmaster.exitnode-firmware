# 23 — The interface says what it is

**Depends on:** 22.
**Spec:** the 1.5 review ([`../phase-1.5/FOLLOWUPS.md`](../phase-1.5/FOLLOWUPS.md): *The heartbeat's `transport` is hard-coded*, and 19's items it gathers — *no path from `IMesh` to `unsupported`*, *all thirteen types always advertised*, *`MeshReply::viaPath` and `snr` are written by every implementation and read by nobody*); meshmaster.dk at today's `master` (read-only): `spec/EXIT-NODES.md` §4 (routing requires the type advertised), D-45 (the unit is a `mesh` agent with transport `native`), `src/MeshMaster.Contracts/Envelopes.cs` (`AgentHeartbeat`, `JobResultReport`); this repository's `lib/agent-core/include/agent/AgentLoop.h` (`HeartbeatIdentity` ~L232), `AgentLoop.cpp` (`buildHeartbeat`, `hb.transport = "onboard"` ~L177, the advertised list ~L185–194), `IMesh.h` (`SendResult`, `MeshReply`), `JobRunner.cpp` (`Outcome::Unsupported`); `CHANGELOG.md` (the four questions), `docs/library.md` §3, §7.1, §10.
**Size:** one session.
**Model:** claude-fable-5-1

## Goal

A consumer of the library cannot say what it is (the heartbeat always says
`onboard`), cannot say what it cannot do (all thirteen job types are always
advertised, and a mesh that cannot serve one has no way to answer
`unsupported`), and must fill two `MeshReply` fields nobody reads. One
interface change, all four at once, one CHANGELOG entry, `v0.3.0`.

## Deliverables

1. **`transport`.** A defaulted member of `HeartbeatIdentity`
   (`"onboard"`), read by `buildHeartbeat`. Validate it against the
   transports meshmaster.dk accepts (EXIT-NODES / `AgentTransports`) in a
   test against the contract fixtures, not a hand-written list.
2. **What the mesh cannot do.** Pick one design and say why in the report:
   `IMesh` answers whether it can serve a job type (asked at every
   heartbeat for the advertised list, and before a job is run, so a job the
   mesh refuses is reported `unsupported` without a packet), or a send result
   that means *this mesh does not do that*, mapped to `unsupported`, plus an
   advertised-types filter in `HeartbeatIdentity`. The first is what the
   Walter's p4-02 *local door* wanted (a `trace` its UART link cannot carry);
   either must leave the Heltec advertising exactly what it does today and
   every existing runner test untouched. An extension's types are advertised
   as before.
3. **`MeshReply::viaPath` and `snr`.** Give each a reader or drop it. Read
   what `JobResultReport` and the result payloads carry: if a result field
   exists that one of them is the honest source of (a result's `snr`, the
   provenance of `pathUsed`), wire it and test it against the fixture;
   otherwise remove the field from the struct and from `AgentMesh` and
   `FakeMesh`. Do not add a contract field: that is meshmaster.dk's to add.
4. **The version.** `agent-core` `0.3.0` in `library.json`, `Version.h`
   and `idf_component.yml`; `CHANGELOG.md`'s `v0.3.0` section answers the
   four questions (which methods moved, for whom, what a consumer changes;
   22's seam if it landed; the fixtures' manifest commit is 24's line);
   `docs/library.md` §3 and §7.1 rewritten for what is now possible, and §7.1
   keeps only what still is not (the ring's own drops being the only
   `observationsDropped`, if that stands).

## Acceptance criteria

- [ ] A consumer can send `transport: "native"`; the Heltec's heartbeat bytes are unchanged (`test_envelopes`).
- [ ] A mesh that cannot serve a type is not advertised that type, and a job of it is answered `unsupported` with nothing sent (tests with `FakeMesh`).
- [ ] `viaPath` and `snr` are each read by something tested, or gone.
- [ ] `CHANGELOG.md` `v0.3.0`; version `0.3.0` in all three places; `pio test -e native`, 20's matrix and both board builds green.

## Not verified here

The Walter's p4-02 against the new interface (its next sync).

## Out of scope

Letting a consumer change what one of the thirteen core types does (19's
third limitation: a different job type, through the extension, is the way).
Tagging — Thomas tags `v0.3.0` after 24.
