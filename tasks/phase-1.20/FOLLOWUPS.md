# Pile 1.20 — follow-ups

Review findings, tagged with the task that must pick them up. Only the reviewer ticks an item.

- [ ] **[next] The four waiting fixtures are reconciled** once meshmaster.dk's master has them
  (its pile 4.47): sync, compare byte for byte, read them from the copied tree, remove the
  waiting files.
- [ ] **[later] A board that never sleeps could claim at once on `jobsWaiting`.** It claims at
  its next poll, thirty seconds on; 69 leaves `loop()` alone.
- [ ] **[later] The claim's room is two.** `kClaimMax`, the runner's scratch, the result
  buffer's eight results and the Walter's 6 144 bytes for a claim's answer are all sized for
  it. A wake of several claims does not need more; a board on mains might.

## A reply does not come back the way the request went (22 November)

Read in production (jobs 40632 and 40633, and the day's jobs of repeaters 5, 42 and 2) and in the
code. No task written.

- [ ] **[pile 1.21, task 71] Every reply to an exit node is flooded.** A request goes out
  direct along the server's path. MeshCore's repeater answers a direct request along the
  `out_path` it has stored for that client and floods when it has none
  (`examples/simple_repeater/MyMesh.cpp` ~L596-618 for a login, ~L692-696 for a request). It
  stores one only from a PATH packet the client sends (`onPeerPathRecv` ~L766-776), and a
  flooded login forgets it (~L132). `AgentMesh::onPeerPathRecv` (`src/mesh/AgentMesh.cpp`
  ~L582-599) sends none on purpose (*one send per command*), and with a path from the server
  nothing is flooded for a repeater to answer with a path in the first place. In the counters:
  TwinOak Ebeltoft answered a login, a status and a telemetry request in each of seven hours on
  22 November (04Z to 10Z) and its `sentDirect` moved by 5 in all. So a trace, which goes out
  and back along the named hops, says nothing about the way a reply takes: at 15:23Z the trace
  Risskov, ef15, f77b, Ebeltoft read 7 to 12 dB on all six hops, and two minutes later two
  logins went unanswered. What it would take: after a login answered on a direct route the exit
  node tells the repeater the way back (the path reversed, in a PATH packet), once a session.
  One more send a session, against the rail.
- [ ] **[none] What it was not.** Not the wait: 24 s for two hops (`perHopMs` 8 000 times
  three), and the answers that came took 3 to 7 s. Not Ebeltoft alone: AAR_LH's own reads
  through Risskov failed in the same hours (11Z to 14Z) while TwinOak Risskov's never did.
- [ ] **[later] The air a flood has to cross.** TwinOak Risskov spends 20 to 26 % of an hour
  receiving and 15 % of what it receives ends as a receive error; at Ebeltoft it is over half,
  with a noise floor of -105 dBm in the hours around the failures and -118 dBm in the clean
  ones.
- [ ] **[Thomas] TwinOak Risskov and AAR_LH heard each other about 15 dB worse from the evening
  of 21 November to midday on the 22nd.** AAR_LH read -119 dBm and -5 dB at every status from
  21T20Z to 22T10Z, and -100 dBm and +8 dB at 14:45Z; the traces read 11.5 dB on 21T03Z,
  -5.75 dB on 22T03Z and 7.25 dB at 15:23Z, and the other direction moved with it. TwinOak
  Risskov did not restart (79 days up) and no setting was applied to it. Not explained; the
  reads through it failed for three hours as the link came back.

## The review of 69–70 (22 November)

Merged 22 November (#80, #81; released in v0.7.7). Read: both reports whole, the claim loop's
ends in `AgentLoop.cpp` (~L781–876). Not read line by line: the envelopes, `JobRunner::claim`,
the tests. Seven of seven criteria in each, no deviation of substance. Nothing that holds a
rollout back.

- [ ] **[none] A wake's claims are bounded four ways** and say which ended them
  (`claimsEndedBy`): the wake's own number, time, transactions, and the server's word.
  `maxClaimsThisWake` unset is one claim, so a Heltec's `loop()` is what it was.
- [ ] **[none] An older server is safe.** `holding` is a member it skips; said in 70's report and
  read in meshmaster.dk (`ContractsJson.cs` ~L64: unmapped members are skipped).
- [ ] **[later] `IUplink`'s two methods changed their signatures** where a defaulted sibling
  would have kept a consumer compiling. The Walter's sync of v0.7.7 was red on exactly that
  (its pull request #92); meant, by 69's own decision, and it is one line each there.
- [ ] **[the Walter's sync] The second claim after one that did not come back acts on a Walter
  at once**, with nothing there asking for it. Its sync task of v0.7.7 is told to show the wake
  holds it.
