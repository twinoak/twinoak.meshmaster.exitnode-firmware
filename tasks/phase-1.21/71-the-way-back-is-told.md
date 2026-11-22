# 71 — The way back is told

**Depends on:** —
**Spec:** `tasks/phase-1.20/FOLLOWUPS.md` (*A reply does not come back the way the request went*); MeshCore at the pin, as `.pio/libdeps/heltec_v3/MeshCore` has it: `examples/simple_repeater/MyMesh.cpp` (a flooded login forgets the path ~L131–133; `sendFloodReply` ~L413–430; a login's reply ~L596–619; a request's ~L684–697; a text's ack and a console reply ~L725–756; `onPeerPathRecv` ~L766–782), `src/Mesh.cpp` (a PATH received, and the reciprocal one upstream's clients send ~L160–180), `src/Mesh.h` (`getDirectRetransmitDelay` ~L67, `createPathReturn` ~L193–194, `sendDirect` ~L213), `src/helpers/RoutingPolicy.h` (`chooseReplyRoute` ~L24–44); `src/mesh/AgentMesh.cpp` (`beginJob` ~L247–267, `setDirectPath` ~L269, `forgetPath` ~L275, `floodOut` ~L349, `dispatch` ~L363–371, `sendLogin` ~L373, `onPeerDataRecv` ~L573, `onPeerPathRecv` ~L582–599) and `AgentMesh.h` (~L232); `lib/agent-core/include/agent/IMesh.h` (`MeshReply` ~L61–82, `SendResult` ~L85, a job's exchanges ~L226–286), `Sessions.h` (`RepeaterSession` ~L26, `kLifetimeMs` ~L47), `Airtime.h` (the gate ~L1–16, `kTransactionsForLogin` ~L30); `lib/agent-core/src/JobRunner.cpp` (`kLineSettleMs` ~L47, `spend` ~L135–162, `awaitLoginReply` ~L221, `ensureSession` ~L465, `probeReachable` ~L504, `exchange` ~L550–600, `runDiscover` ~L1294–1330, `describeSend` ~L2490); `test/support/FakeMesh.h`; `docs/library.md` §3.5, §3.6, §8; `docs/host.md` (~L46)
**Size:** one session.
**Model:** claude-opus-5

## Goal

On 22 November Thomas traced TwinOak Ebeltoft through the Risskov exit node: out by two hops
and back, 7 to 12 dB at all six receptions (meshmaster.dk job 40632). Two minutes later a
status read by the same pinned path had two logins unanswered (job 40633). That day's reads
of Ebeltoft needed two or three attempts when they worked at all.

A trace goes out and comes back along the hops it names. A request goes out along the path
too, and its reply does not come back along it:

- A repeater answers a direct request along the `out_path` it has stored for that client, and
  floods the reply when it has none (`MyMesh.cpp` ~L596–619, ~L684–697, ~L725–756).
- It stores one only from a PATH packet the client sends (`onPeerPathRecv` ~L766–782), and a
  flooded login forgets it (~L131–133).
- This firmware sends none: `AgentMesh::onPeerPathRecv` returns false on purpose (~L596), and
  with a path from the server nothing is flooded for a repeater to answer with a path anyway.

So every reply to an exit node is a flood: repeated by every repeater that hears it, and home
only if one copy finds its way through an air that TwinOak Risskov spends a quarter of every
hour receiving. In the counters: Ebeltoft answered a login, a status and a telemetry request
in each of seven hours that morning, and its `sentDirect` moved by 5 in all.

After this task the repeater is told the way back before it is asked anything, so a reply
returns by the hops the request went out by, which are the hops a trace measures; and a job's
log says how each reply came.

## Deliverables

1. **The way back, as a send of the mesh's.** `IMesh` gains a member that tells this job's
   repeater the way back to this exit node. Its default sends nothing and says so, so that a
   mesh without it (the Walter's, a host's) compiles and behaves as today. On the board it is
   the PATH packet upstream's clients send as their reciprocal one (`Mesh.cpp` ~L176–177):
   `createPathReturn` to the peer, the way back as its path, nothing extra, sent direct along
   the out path, through `dispatch`'s door so that its frame is logged like any other.
2. **What the way back is.** For a path from the server: its hops in the opposite order, in
   its hash size. For a pair of no hops: the empty path, which a repeater stores as *direct*.
   For a path a flooded reply taught: the path that reply itself travelled. Show from the
   source, for both, that the order is the one `sendDirect` at the repeater needs.
3. **Before every login that goes out direct**, each try of it: the way back, then the time
   it needs to clear the hops, then the login. The time comes from the mesh's own figures (the
   packet's airtime, the delay a hop adds, the hops) and is given in the report for 0, 1, 2
   and 4 hops: no number picked by hand. A login that is flooded is preceded by nothing. Both
   packets are one slot of the airtime gate and one transaction, as a login is today.
4. **A reused session whose path has changed.** A session remembers the path it was opened
   by. A job that reuses it with another path in its claim tells the way back before its
   first send.
5. **A reply says how it came.** `MeshReply` says whether the reply arrived direct, flooded,
   or *not known* (a mesh that cannot say). The job's log says it at every answered exchange,
   beside how the request went.
6. **Told once more when it was not taken.** A reply that arrives flooded after the way back
   was told in this job means the repeater did not know this key when it came (a guest's
   first login) or the packet was lost. It is told once more before the job's next send, once
   a job, and the log says so.
7. **After a path a flood taught** (a discover; a login with no path from the server): the
   way back is told before the job's next direct send. `onPeerPathRecv` keeps returning
   false: nothing is sent from inside MeshCore, past the gate and the frame log.
8. **Not a command.** *One send per command* is the rail for lines that change a repeater. A
   way back told twice is the same way back. Correct the comment at `AgentMesh.cpp` ~L596 and
   say in `docs/library.md` §8 where the rail ends.
9. **The fake repeater** (`test/support/FakeMesh.h`) does what the pin does: an out path per
   key it knows, set by a way back from such a key and by nothing else, forgotten by a
   flooded login; a reply direct along it when it has one, flooded when it has none; a way
   back from a key it does not know dropped. A test can declare a way back dead, and a reply
   sent along it is then lost.
10. **Tests**, on the host:
    - **22 November:** two hops from the server, a repeater that knows the key. Frames in
      order: way back, login, request, request. Every reply direct, none flooded. Before the
      change every reply of the same job is flooded; show both logs in the report;
    - a guest's first login: the first way back dropped, the login's reply flooded, told once
      more, the request's reply direct; the next job all direct;
    - a pair of no hops; a path of four hops in three-byte hashes;
    - a reused session with a new path in the claim;
    - a flooded login that taught a path, then a direct request;
    - a way back lost on the air: replies flooded as today, told once more, then direct;
    - a dead way back: both tries of the login unanswered, the job `unreachable` as today,
      and nothing new is tried;
    - a mesh with the default member: every send and byte as today.
11. **What the gate and the lease see.** Transaction counts and `transactionsFor()` do not
    move. The report says how much longer a login's slot is, in airtime and in time, and
    whether any job is then tight in the lease the server gives it.
12. **The words.** `docs/library.md`: a section beside §3.5 on the way back, and §8;
    `docs/host.md` for the fake. A line in `release-notes/unreleased/`.

## Acceptance criteria

- [ ] The test of 22 November: way back, login, request, request; every reply direct (the test's name, and its log before and after).
- [ ] A way back is the claim's hops reversed in their hash size, empty for no hops, and the flooded reply's own path after a flood; the report quotes the lines of MeshCore that make this the order a repeater sends by.
- [ ] A guest's first login ends with the request's reply direct and exactly two ways back told (the test's name).
- [ ] A reused session tells the way back when the claim's path differs and not when it is the same (two tests' names).
- [ ] A flooded login is preceded by nothing, and nothing is sent from inside `onPeerPathRecv` (the tests' names).
- [ ] A dead way back ends the job as silence ends it today; no send is added (the test's name).
- [ ] With the default member the existing tests pass unedited; name any that were edited and why.
- [ ] Transaction counts are what they were (the tests that hold them pass unedited).
- [ ] `pio test -e native` green, both boards build, the frame rail and the library's host rail (`tools/check-library-host.py`) green, CI green.

## What only a board can show

That a repeater in the field takes the packet. After the release: a read of TwinOak Ebeltoft
through Risskov whose log says the replies came direct, and Ebeltoft's `sentDirect` moving by
three or more an hour. Thomas's, from meshmaster.dk; I read the counters.

## Out of scope

meshmaster.dk: no contract changes, and how a reply came is in the job's log only. The
Walter's mesh: its replies are its radio's, and the repeater firmware and the link's
specification would have to carry a way back. The .NET agent. What a silence means: the login
sequence and `forgetPath` stay. The scope a flooded reply is sent with. A pin.
