# 68 — The observers stand down for a fetch

**Depends on:** —
**Spec:** `src/update/Fetch.h` (`kRangeBytes` ~L61; `kFileTries` ~L63–66: *three in all, not three per range*; the result's `failures` ~L189; `running()` ~L222) and `Fetch.cpp` (`start` ~L206; `stepFile` ~L367–462: a range that stops short ~L449–454; `countFailure` ~L540–556; `end` ~L559), `src/agent/AgentClient.cpp` (`failedForMemory` ~L45; the streamed body ~L245–266: what `writeToStream()` returns is not read; ~L405–409: a call that failed for want of memory; `getStreamed` ~L420–442), `src/update/FetchSeams.cpp`, `src/update/Ask.cpp` (~L462: what stands in an ask's way; ~L536–537: the fetch an ask starts; the journal), `src/main.cpp` (the ask's seam to the fetch ~L269–270; the console's `update fetch` ~L793; the loop's step ~L1344–1346), `src/observer/Connection.h` (the heap gate and its numbers ~L160–200: `kHeapFloorBytes` ~L193, `kHeapYieldMs` ~L199; `onUplinkMemoryFailure` ~L291, `onUplinkCall` ~L297; a slot's `holdUntil` ~L338; `yieldHighest` ~L360) and `Connection.cpp` (the states' words ~L59; ~L294–331), `src/observer/Observer.h` (~L95–98), `src/observer/Board.h` (~L29), `sdkconfig.defaults` (~L1630–1633: `CONFIG_MBEDTLS_DYNAMIC_BUFFER=y`, records of 16 384 bytes); `test/test_fetch`, `test/test_observer`, `test/test_update_ask`; `docs/observer.md`, `docs/field.md` §9, `docs/console.md` (`get mqtt.status`); `tasks/phase-1.18/FOLLOWUPS.md` (*A fetch that three short ranges ended*).
**Size:** one session.
**Model:** claude-opus-5

## Goal

On 20 November two boards on 0.7.4 were asked from meshmaster.dk to update to 0.7.5. Risskov
did, in a minute. Aarhus C refused eight seconds into its fetch:

```
check: table two, 0.7.4+a3bc485 running
fetching: 0.7.5+ac412e7, 1515024 bytes
refused: range 145570-211105 stopped at 48208 of 65536 bytes (3 failure(s), which is all this fetch gets)
```

Risskov's fetch had short ranges too: it was sent 1.6 MB for a file of 1.5 MB. It had fewer
than three.

Three things are in the way of knowing more, and of the next board.

**Nothing says why a range stopped.** The body is streamed by the HTTP client and what the
client returns is not read. The fetch sees fewer bytes than it asked for.

**The likely reason is memory.** 48 208 is three TLS records to the byte, if the answer's head
took 944 bytes of the first. This build takes a buffer for every incoming record as it comes,
and Aarhus C has both observer slots connected, each a TLS session of its own.

**Three failures end a fetch of twenty-three ranges**, whatever each of them brought.

Thomas, 21 November: an exit node should stop all its observers before it tries to update.

## Deliverables

1. **No observer slot is connected while a release is fetched.** Before a fetch's first
   request, whether an ask started it or `update fetch` at the console, every slot that is up or
   opening is closed, and none opens until the fetch has ended. The first request waits until
   the slots' sessions are gone: say in the report how that is known, and what bounds the wait.
2. **A slot says so.** Its state has a word of its own for this, `get mqtt.status` prints it,
   and it is not counted as *gave way*: nothing went wrong.
3. **The slots come back by themselves** when a fetch ended without a switch, at once and by
   their ordinary rule, with no ten-minute hold. After a fetch that is followed by a switch
   nothing reopens: the board restarts.
4. **A range that stops short says why:** what the client returned, the TLS layer's own error
   where it has one, and the largest free block at that moment, in the fetch's line and so in
   an ask's journal. A stop for want of memory is counted where the uplink's memory failures
   are.
5. **A failure is a request that brought nothing.** A range that stopped short and brought
   bytes that were kept is asked on from where it stopped and uses up no try. Three requests in
   a row that bring nothing end the fetch, in today's sentence. And a bound on the requests of
   one file, so that no fetch goes on for ever: say the number and where it comes from.
6. **The words.** `docs/observer.md` and `docs/console.md` (the state, and that a fetch closes
   the slots). `docs/field.md` §9: what a refused fetch's line says and what to do about each
   reason. One line in `release-notes/unreleased/`.

## Acceptance criteria

- [ ] With two slots connected, a fetch is started by an ask and by `update fetch`: both slots are closed before the first request leaves, their state is the new word, and no slot opens while the fetch runs (tests, with the scripted clients of `test/test_observer` and the scripted server of `test/test_fetch`).
- [ ] The fetch ends refused: both slots open again without waiting out a hold, and neither's *gave way* count moved (test). The fetch ends fetched and a switch follows: no slot opens (test).
- [ ] A slot whose preset is `none` stays off through all of it (test).
- [ ] A range that stops at 48 208 of 65 536 bytes with an error from the client: the line names the reason and the largest free block, the next request asks from the byte after the last one kept, and the fetch's tries are where they were (test).
- [ ] Ten ranges in a row that each stop short and bring bytes: the file is fetched and judged (test). Three requests in a row that bring nothing: refused, and the sentence says three (test). The bound of deliverable 5: refused at it (test).
- [ ] An ask whose fetch was refused carries the reason in its journal's last line (test in `test/test_update_ask`).
- [ ] `pio test -e native` green, both boards build, the frame rail green, CI green.

## What only a board can show

- ⚠ Aarhus C, with both observer slots set: *Update* on its Firmware tab. `get mqtt.status`
  while it fetches shows both slots standing down, and the journal says how it ended. If a
  range still stops short, its line now says why: that line is what to send.
- ⚠ The `stack` line of `status` after the update: the first reading with a fetch in it.

## Out of scope

The observers during the ten minutes of a trial: they open after the restart as after any.
A smaller TLS record, which is the server's to send. Fetching on a task of its own. Anything
in the contract with meshmaster.dk.
