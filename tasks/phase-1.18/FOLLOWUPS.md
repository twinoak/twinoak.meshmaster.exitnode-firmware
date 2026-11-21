# Pile 1.18 — follow-ups

Review findings, tagged with the task that must pick them up. Only the reviewer ticks an item.

- [ ] **[later, meshmaster.dk] Nobody is told that an exit node has stopped hearing.** On
  19 November it was found the next day, from the holes in two repeaters' charts. The server
  has what would say it: the heartbeat's `overheard` counter, kept with its history, stops
  rising; and every read through one exit node failing while the same repeaters answer another.
  That night's `overheard` history would also show whether the receiver was the cause
  (`GET /api/v1/agents/2/counters`, which wants a token; the page shows the newest fifty
  changes only).
- [ ] **[Thomas to decide] Four seconds.** It is the repeaters' value from the standard
  profile, taken because the fleet runs with it. An exit node asks and waits where a repeater
  only relays; nothing here measured what a reset costs a reply that is on its way.
- [ ] **[later] The two findings this pile is made from** stand in pile 1.17's `FOLLOWUPS.md`
  and are ticked there when 66 and 67 have been reviewed.

## The review of 66–67 (20 November)

Both merged: 66 at 17:45 CET (#74), 67 at 18:36 (#76, and #77 for a line of its report). Eight
of eight and five of five criteria, no deviation of substance. Read: both reports' deviations,
and the change in `src/` of each (`AgentMesh.h`, `mesh/Receiver.cpp`, `settings/Keys.cpp`,
`main.cpp`, `platform/LoopStack.h`). Not read: the tests, the words. Nothing found.

**v0.7.5 was released from ac412e7, between the two:** it has 66 and not 67. The `stack` line in
`status` is in the release after it.

- [ ] **[none] 66 is the one override and nothing beside it.** `AgentMesh::getAGCResetInterval()`
  answers `agcResetIntervalMs()`, seconds times a thousand, 0 to 3600, 4 unless set
  (`mesh/Receiver.cpp`); it is set at boot and when `agc.*` is set. No line of ours resets a
  radio. The `radio` line says the interval, a floor of 0 reads *noise floor being measured*,
  and the observer's message leaves it out.
- [ ] **[none] 66 does not round to four seconds as a repeater does**, and takes up to 3600
  where a repeater takes 1020. Said in its report. The key's NVS name is `agc_reset_int`.
- [ ] **[none] 67:** `printStatus` prints the loop task's line from the one function;
  `get mqtt.status` keeps its three; `docs/observer.md` was corrected too, which the task had
  not named.
- [ ] **[Thomas] Not seen on a board yet:** the `radio` line of `status` on 0.7.5, and a run of
  nights on Risskov.

## A fetch that three short ranges ended (Aarhus C, 20 November)

- [ ] **[next] An update of Aarhus C to 0.7.5 was refused eight seconds into its fetch.** Its
  journal on meshmaster.dk: `check: table two, 0.7.4+a3bc485 running`, `fetching: 0.7.5+ac412e7,
  1515024 bytes`, `refused: range 145570-211105 stopped at 48208 of 65536 bytes (3 failure(s),
  which is all this fetch gets)`; the server counted 262 kB sent in 4 requests. Nothing was
  switched and the board runs 0.7.4. Risskov, an hour before, on the same firmware and the same
  release, was sent 1.6 MB for a file of 1.5 MB and committed: it had short ranges too, fewer
  than three.
  What is known:
  - **A fetch gets three failures for the whole file** (`kFileTries`, `Fetch.h` ~L63–66), 23
    ranges of 64 kB. The comment beside the range's size says the bench's own `update fetch`
    settles it, and there was no bench.
  - **A range that stops short says nothing of why.** `AgentClient` hands the body to
    `HTTPClient::writeToStream()` and does not read what it returns (`AgentClient.cpp` ~L257–260);
    the fetch sees fewer bytes than it asked for and no error (`Fetch.cpp` ~L449–454).
  - **48 208 is three TLS records to the byte**, if the answer's head took 944 bytes of the
    first: 3 x 16 384 - 944. The build takes a buffer for every incoming record as it comes
    (`CONFIG_MBEDTLS_DYNAMIC_BUFFER=y`, 16 384 bytes of content), and Aarhus C has both
    observer slots connected, each with a TLS session of its own; on 20 November it had
    58 764 bytes free and a largest block of 47 092 at rest.
  So a record's buffer that could not be had is the likely end of each range. It is not shown:
  no line says so, on the board or on the server.
  Wanted: the reason a range stopped in its journal line (what the client returned, the TLS
  error, the largest free block); a count that a range which got further does not use up; and
  whether the observer's slots give way while a file is fetched, as they do for a call under
  the floor.
