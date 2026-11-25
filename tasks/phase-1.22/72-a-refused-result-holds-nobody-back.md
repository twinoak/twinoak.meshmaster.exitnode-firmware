# 72 — A refused result holds nobody back

**Depends on:** —
**Spec:** `tasks/phase-1.21/FOLLOWUPS.md` (*A result the server will not take*); `lib/agent-core/src/JobRunner.cpp` (`flushResults` ~L2242–2275); `lib/agent-core/include/agent/IUplink.h` (`postJobResult` ~L108–119); `src/agent/AgentClient.cpp` (`postJobResult` ~L692, `postToJob`); API §3 as `lib/agent-core/include/agent/IUplink.h` cites it and, read only, meshmaster.dk's `spec/API.md` §3 (a result: taken, `late`, `duplicate`; what is answered when the body cannot be accepted). Read only, in `twinoak.meshcore.walter-firmware`: its `tasks/phase-18/reports/06.md` (`ModemUplink::resultIsTheServersVerdict`).
**Size:** one session.
**Model:** claude-opus-5

## Goal

On 24 November a unit posted a result the server turned away for what it said (a refused
NoiseScope session without its word). The library keeps results until they are taken,
oldest first, and `flushResults` **returns at the first post that fails**; a post that
fails is kept and sent again. `IUplink::postJobResult` answers yes or no, so the server's
verdict on a body reads the same as a call that never arrived. That one result was the
oldest for three hours and every result behind it waited: three hours of a repeater's
readings were lost, until the kept image filled and dropped it.

The Walter's firmware has since answered *taken* at its own uplink for such a status (its
phase 18, task 06). That is the right rule in the wrong place: a Heltec has the same queue.

## Deliverables

1. **A post has three answers:** taken; not reached (the call did not come back whole:
   keep it and try again); **refused** (the server answered, and its answer is a verdict on
   this result: never again). How `IUplink` says it is yours (the return type, or one more
   out-parameter), chosen so that the Walter's sync is as small as it can be; say in the
   report exactly what its `ModemUplink` has to change and that its own
   `resultIsTheServersVerdict` can go.
2. **Which statuses are a verdict is one function of the library**, beside the envelopes,
   with a test a status: 400, 404, 409, 410, 413 and 422 are; 401, 403, 408, 429 and every
   5xx are not (a token, a clock or a busy server says nothing about the body). Read
   meshmaster.dk's `spec/API.md` for what the server answers a result with and say where the list and the
   specification differ.
3. **A refused result is dropped and said:** removed from the kept results, counted, logged
   with the job and the status, marked in what `jobs` remembers (*refused by the server:
   http 400*), and the flush **goes on** to the next. A result not reached stops the flush
   as today.
4. **The Heltec's uplink** (`AgentClient::postJobResult`) tells the three apart from the
   status it already has.
5. **The heartbeat's counters are a contract and gain nothing.** The count is in `status`
   and `jobs` at the console. If you find it should be in the heartbeat, write that as
   a follow-up for meshmaster.dk and add nothing.
6. **Host tests:** three kept results with the oldest refused (the other two go, in order);
   the oldest not reached (nothing goes); refused, then not reached, then taken; a wake's
   byte and second budgets still stop a flush where they did; each status of deliverable 2.
7. **The words:** `docs/` where the kept results are described; a line in
   `release-notes/unreleased/`.

## Acceptance criteria

- [ ] A result the server refuses is dropped, counted and said, and the results behind it are posted in the same flush (the test's name).
- [ ] A result that was not reached is kept and stops the flush, as today (the test that holds it, unedited).
- [ ] The statuses that are a verdict are decided in one function, with a test each.
- [ ] The report says what the Walter's uplink changes at its next sync.
- [ ] `pio test -e native` green, both boards build, the frame rail and the library's host rail (`tools/check-library-host.py`) green, CI green.

## Out of scope

The Walter's repository. The server. A result retried with another body. The heartbeat's
contract.
