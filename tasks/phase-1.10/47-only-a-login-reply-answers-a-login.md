# 47 — Only a login reply answers a login

**Depends on:** nothing in this pile.
**Spec:** meshmaster.dk, read only: `spec/EXIT-NODES.md` §7 (the login) and §4.3; `tasks/phase-4.32/186-a-refusal-with-nothing-in-it.md` there is the server's half. This repository: `lib/agent-core/src/JobRunner.cpp` (`loginOnce` ~L180–230, `probeReachable` ~L364, `exchange` ~L416–460: *no reply on a reused session: logging in again once*, the discover's two logins ~L800–860), `lib/agent-core/include/agent/IMesh.h` (`awaitReply`, `MeshReply`), `lib/agent-core/src/ReplyCodec.cpp` (`parseLoginReply` ~L180), `src/mesh/AgentMesh.cpp` (`sendLogin` ~L372: *a login reply carries no tag of ours*; `takeResponse` ~L536), `test/support/FakeMesh.h`, `test/test_runner`, `docs/library.md`, `docs/console.md`; MeshCore at the pin, `examples/simple_repeater/MyMesh.cpp` (`handleLoginReq`: it returns a reply or 0, never a refusal).
**Size:** one session.
**Model:** claude-opus-5

## Goal

A login reply starts with the **repeater's** clock, not with a tag of ours, so a login is awaited
with tag 0 and the next response from that repeater is taken as its answer. When that response does
not parse as a login reply, `loginOnce` says *refused* and the job ends `unauthorized`.

Stock firmware cannot refuse a login: `handleLoginReq` answers or returns nothing. What arrives
instead is the **late reply to the request before**. On meshmaster.dk, 12 November, every one of the
ten times an active ACL association was parked as *the ACL entry is gone* has this log (jobs 24142,
21783, 8293, 19065):

    session reused (acl, read-write, 147 s old);
    neighbours page at 0 try 1: no reply (direct);
    no reply on a reused session: logging in again once;
    login (acl) try 1: refused

In job 24142 the same key had been answered *logged in as read-write* three minutes before.

A late reply can also parse. A neighbour page of a repeater with no neighbours is four zero bytes
after its tag. `parseLoginReply` takes a first byte of zero for *login ok* and the fourth byte for
the permissions, so that page reads as *logged in as guest*. Check whether a status reply
can do the same: its first field is the battery in millivolts.

## Deliverables

1. **While a login is awaited, only a login reply ends the wait.** A response that does not parse as
   one is dropped with a line in the job's log saying a reply to something else arrived, and the
   wait goes on to its end. Then it is *no reply*, and the second try is made as for any silence.
2. **A late reply is known by its tag.** The runner knows the tags of the requests this job has
   sent. A response whose tag is one of those is that request's reply, whatever it parses as, and is
   dropped the same way. This is what catches the empty neighbour page.
3. **In the library, not only in the Heltec's mesh.** The Walter has its own `IMesh`, and takes this
   at its next sync. `AgentMesh` may drop what it can tell itself, but the rule holds with an
   `IMesh` that hands every response through.
4. **A response that does not parse is never *refused***: in `loginOnce` (~L209), in the
   reachability probe's blank login (~L393) and in the discover's logins (~L808–860), which act on
   `LoginResult::Refused`. If a reply that *is* a refusal exists on some firmware, say in the
   report where you looked and what it would look like; do not keep the word for it unproven.
   **Answered below the class the job needs** (~L240–260) is a login reply and a verdict. It stays
   exactly as it is.
5. **`FakeMesh` can be late**: a reply delivered after its request was given up, during the next
   wait. Each case below fails without the fix.
6. **The words.** `docs/library.md`, `docs/console.md` (the walk gets an item: a slow repeater, a
   neighbour read, and no `unauthorized`), `CHANGELOG.md`.

## Acceptance criteria

- [ ] A status request times out, the re-login is sent, the status reply arrives first and the login reply after it: the login is *logged in as …*, the job ends `ok`, and the log has the dropped line (test).
- [ ] The same with no login reply at all: *no reply*, a second try, and the job's outcome is what a silence gives, never `unauthorized` (test).
- [ ] An empty neighbour page arrives late during a login wait: dropped by its tag, not read as *guest* (test).
- [ ] The reachability probe and the discover: a late reply is not *refused* (tests).
- [ ] The existing login tests pass; the one that expects *refused* from a non-login response is rewritten to this rule and the report says which.
- [ ] `tools\verify.ps1` green: host tests, the Heltec build, the rails. CI green.
