# Pile 1.21 — follow-ups

Items for a later task of this pile are tagged with it. Only the reviewer ticks an item.

- [x] **[Thomas] No repeater in the field has been sent a way back by this firmware.** The
  host's repeater is a fake written from MeshCore's source. The proof is a read of TwinOak
  Ebeltoft after the release: replies direct in the log, and its `sentDirect` moving.
- [ ] **[later, meshmaster.dk's] How a reply came is only in a job's log.** As a field of the
  result the route's page could say *replies come back flooded* where it says what a trace
  measured.
- [ ] **[later] The unit at Bellevue's replies are flooded too.** A Walter's requests are sent
  by its radio. Whether the repeater firmware's client tells a way back was not read; the
  link's specification has no word for it.
- [ ] **[later] The .NET agent.** A companion's own firmware sends its reciprocal path after a
  flood. With a path from the server it is the same question; not read.
- [ ] **[Thomas] EXIT-NODES §7 prices a login as one transaction.** It stays one, and is two
  packets now. A sentence in meshmaster.dk's specification is his.

## The review of 71 (22 November)

Merged 22 November (#82; released in v0.7.7). Read: the report whole,
`AgentMesh::tellTheWayBack`, `JobRunner::loginOnce`'s first lines and where a way back is owed.
Not read line by line: the fake repeater, the tests, the docs. Nine of nine criteria, no
deviation. Nothing that holds a rollout back.

- [ ] **[none] The order is the task's:** one slot of the gate, the way back, the time it needs
  to clear the hops, the login. The path is reversed at its own hash size and sent through
  `dispatch`, direct along the out path.
- [ ] **[none] The clearing time is arithmetic:** the packet's airtime and upstream's default
  delay of a direct hop (three halves of the airtime): 0.2 s for no hops, 0.9 s for one, 1.6 s
  for two, 3.1 s for four.
- [ ] **[later] The report's bench list says no pinned pair is a direct neighbour.** Several
  are: TwinOak Risskov and TwinOak Bellevue from Risskov, at no hops. The empty way back is
  therefore the commonest case on that board, and is held by a test and not yet by a repeater.
- [ ] **[later] After a flood the way back is the request's path reversed**, not the path the
  reply travelled, as the task asked: `onPeerPathRecv` stores what the repeater says the
  request came by. The same hops under the same assumption a pin makes; said because the
  report's words say otherwise.
- [ ] **[later] A repeater whose owner raised `direct.txdelay` holds the way back longer than
  the clearing time**, and the login then overtakes it. The report names the recovery (the
  reply comes flooded, the way back goes once more). Worth a look in the logs of AAR_LH.
- [ ] **[later] `status`'s `tx` counts packets now and not transactions** on a board with
  paths. Nothing reads it; the words in `docs/library.md` §3.2 should say so.

## The first night on the air (22 November, Risskov on 0.7.7 since about 21Z)

Read on meshmaster.dk at 23:35Z: the logs of Risskov's last fourteen reads of four repeaters.
The first item of this file is ticked on it: real repeaters take the packet.

| repeater | hops | firmware it reports | jobs | ways back told (again) | login replies direct / flooded | request replies direct / flooded | tries unanswered |
|---|---|---|---|---|---|---|---|
| TwinOak Risskov | 0 | v1 (the pin's line) | 6 of 6 ok | 3 (0) | 3 / 0 | 6 / 0 | 0 |
| Temp1 | 0 | not read | 6 of 6 ok | 3 (0) | 3 / 0 | 6 / 0 | 1 |
| AAR_LH | 1 | PowerSaving18.1104 | 4 of 4 ok | 4 (1) | 2 / 1 | 4 / 0 | 3 |
| TwinOak Ebeltoft | 2 | v1.16.0-07a3ca9 | 6 of 6 ok | 7 (5) | 0 / 5 | 3 / 3 | 10 |

- [ ] **[none] No hops and one hop: as designed.** The empty way back is taken (the review's
  doubt about the bench list), every reply of TwinOak Risskov and Temp1 came direct, and
  nothing was told twice.
- [ ] **[later, Thomas] Ebeltoft's login is answered by flood every time**, whatever it was
  told first, so every job there tells the way back twice. Its firmware is v1.16.0; the
  repeater code this task was read against is the pin's. Most likely that version floods the
  reply to a direct login always; not read. A newer firmware on Ebeltoft would settle it.
- [ ] **[later] Ebeltoft: ten tries unanswered in six jobs**, all of which succeeded in the end.
  A reply has one way home now. Whether that is better or worse than the floods of the days
  before cannot be said from six jobs; the hourly reads of a few days can.

## A result the server will not take (24 November)

- [ ] **[next, offered] One result the server refuses holds every result behind it.**
  `JobRunner::flushResults` (`lib/agent-core/src/JobRunner.cpp` ~L2242–2275) posts the kept
  results oldest first and returns at the first post that fails, and a result that was *not
  taken* is kept and sent again. `IUplink::postJobResult` answers yes or no, so the server's
  verdict on a body (a 400 of its contract, a 404, a 409) is the same *no* as a call that
  never arrived. On 24 November the Walter at Bellevue held one such result (a session
  `refused` with no word, which meshmaster.dk turned away until its pile 4.48) and reported
  no job for hours. The Walter's pile 18 task 06 answers it in its own uplink (the three
  statuses are answered *taken* to the library). The library is unchanged, and what a
  Heltec's uplink answers for a 4xx was not read.
