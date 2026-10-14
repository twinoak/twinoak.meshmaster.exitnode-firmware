# 19 — The hand-over

**Depends on:** 18.
**Spec:** proposal 03 (*What it gives the Walter*); `README.md` (*Rails that hold in every task*), `docs/upstream.md`, `docs/host.md`, `docs/console.md`; the Walter firmware's `tasks/phase-3/05-the-wake-cycle.md` (the `Backend` seam) and `CLAUDE.md` (its rails — the library must let it keep every one); every report of this pile.
**Size:** one session, short.
**Model:** claude-opus-5

## Goal

A second firmware is about to take this library at a commit. Before it does,
one document says what it gets, what it must provide, what it must never do,
and how it keeps up — and this repository ships a release whose tag is the
first pin anyone can take.

## Deliverables

- **`docs/library.md`**, for a consumer who has not read this repository:
  - what `agent-core` is and is not (the job logic, the loop, the envelopes,
    the codecs; not the uplink, not the mesh, not storage);
  - **the three interfaces, method by method**, with the contract of each
    (blocking or not, timing, what a return value means, what the runner
    does on failure), the `MeshReply` constants a mesh implementation must
    honour, and `IJobExtension` — how a consumer adds job types of its own
    and what the runner still does for them;
  - **the lifecycle**: construct, `begin`, `loop()` or `wake()`, `state()` /
    `restore()`, what to do on the platform's reboot request;
  - **the rails the consumer inherits and must keep** — nothing the mesh
    says logged as text (the library never logs a reply body; the mesh
    implementation must not either), a password in RAM for the job only
    (the library wipes its copy; the mesh implementation must not keep one),
    one send per command (the library's pacing assumes the mesh sends
    exactly what it is asked), TLS verified (the uplink's; the library
    cannot check it, say so) — each with the test in this repository that
    holds it here and a sentence on what the consumer's equivalent should
    be;
  - **the fixtures**: `tests/fixtures/contracts/` is the wire contract, with
    its manifest commit; a consumer runs the same bytes through the same
    writers; how to sync them;
  - **versioning and pinning**: a tag `vX.Y.Z` is the library's version;
    what a bump may change (list by interface), where the changelog is
    (`docs/upstream.md`'s *this repository is an upstream* section, or a
    `CHANGELOG.md` — decide); what a consumer's upstream watch should look
    at (the three headers, `MeshReply` constants, `library.json`, the
    fixtures' manifest);
  - **what the Walter will implement**, in one table: each `IMesh` method
    → the `LINK-PROTOCOL.md` §6 request (from 15's report), each `IUplink`
    method → an HTTPS call, each `IPlatform` method → its ESP-IDF source.
- **`README.md`**: a section *The library* pointing at `docs/library.md`,
  and the state of things — the Heltec unchanged, the host suite now covers
  the runner, pile 1.5's version.
- **The release**: the report names the version to tag (`v0.2.0`: the tree
  moved, the behaviour did not) and the tag is Thomas's to push; the
  release workflow is unchanged. The Walter's phase 4 pins the tag's commit.
- **FOLLOWUPS for the next piles**: what the host suite found about the
  agent that is not this pile's to fix; the stuck branch 08 to delete; the
  bench walk of `docs/console.md` on `v0.2.0` (AarhusC and Grindsted, still
  on older builds per 1.4's follow-ups).

## Acceptance criteria

- [ ] `docs/library.md` exists and every method of the three interfaces is in it with its contract; the rails table names a test for each rail held here.
- [ ] A reader of `docs/library.md` alone can write the Walter's three implementations: the report says what a reviewer checked to believe that (read it as the Walter's task 02 would).
- [ ] README updated; `pio test -e native` and both boards green; the report names the version to tag.

## Not verified here

The library consumed by ESP-IDF (the Walter's phase 4, task 01). The Heltec
on the air at `v0.2.0` — the bench walk.

## Out of scope

Anything the Walter needs that `LINK-PROTOCOL.md` §6 cannot carry: a
specification gap for the hardware repository, not a library change.
