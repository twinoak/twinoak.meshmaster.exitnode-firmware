# 69 — A wake claims again

**Depends on:** —
**Spec:** `lib/agent-core/include/agent/AgentLoop.h` (`kTransactionsPerJobAtLeast` ~L129, `WakeIntent` ~L134–143, `Budget` ~L145–165, `wake()` ~L417, `claimMax()` ~L566), `lib/agent-core/src/AgentLoop.cpp` (the heartbeat's `jobsWaiting` ~L423; `transactionsLeft`/`claimMax` ~L700–711; `wakeResults` ~L751; `wakeJobs` ~L775–870; `wake` ~L886–946), `lib/agent-core/include/agent/JobEnvelopes.h` (`kClaimMax` ~L50, `writeClaimRequest` ~L213) and `JobEnvelopes.cpp` (~L162), `lib/agent-core/include/agent/IUplink.h` (`claimJobs` ~L86, `postJobResult` ~L90), `lib/agent-core/include/agent/ResultBuffer.h` (~L28–36), `lib/agent-core/src/JobRunner.cpp` (`claim` ~L2167, `endClaim` ~L2190, `runClaimed` ~L2195), `src/agent/AgentClient.cpp` (`postJobResult` ~L670); `test/test_loop`, `test/test_claim`, `test/test_contract_fixtures`, `tests/fixtures/contracts-ahead/README.md`; meshmaster.dk's task p4.47-253
**Size:** one session.
**Model:** claude-opus-5

## Goal

`AgentLoop::wake()` is one pass: a heartbeat, one claim of at most two jobs, the jobs, the
observations. A board that sleeps between wakes therefore does two jobs an hour at best,
whatever is queued for it. On 21 November a NoiseScope sample from the Walter at Bellevue took
five wakes for six reports, a download and a session; and four times a day its `neighbours`
listing takes one of the two places and that hour's `telemetry` expires on the server.

Two is the room for a claim (`kClaimMax`, the runner's scratch, the result buffer) and stays.
After this task a wake that has finished what it was handed asks again, while the server says
there is more and the wake has the time, the transactions and the claims for it. How many
claims a wake may make is the caller's to say, and a caller that says nothing gets one: what it
gets today.

**The contract** (API §3; the same text is in meshmaster.dk's task p4.47-253 and in both tasks
of this pile, and the four lines below are the fixtures both sides hold):

- `POST /agent/v1/jobs/claim`, the request, gains two optional members after `max`, in this
  order:
  - `transactionsLeft` — an integer of 0 or more: what the wake has left of the
    `maxTransactionsThisWake` its heartbeat stated. An agent writes it in every claim of a wake
    after the first, and never in the first.
  - `holding` — a list of job ids: the jobs this exit node is running or keeps a result for.
    An agent that writes it writes it in every claim, empty when it holds nothing.
- The answer to a result, `JobResultReply`, gains one optional member, last: `jobsWaiting` — a
  boolean: whether a claim by this exit node now would be handed a job. It is written to an
  exit node whose last heartbeat stated an interval of its own (`nextCheckInS`) and to no other.
- Every member is left out when it has nothing to say, so the four fixtures that exist are
  what they were, to the byte.

| File | Line |
|---|---|
| `ClaimRequestLeft.json` | `{"max":2,"transactionsLeft":17}` |
| `ClaimRequestHolding.json` | `{"max":2,"holding":[12345]}` |
| `ClaimRequestHoldingNothing.json` | `{"max":2,"holding":[]}` |
| `JobResultReplyWaiting.json` | `{"jobId":12345,"state":"succeeded","duplicate":true,"late":true,"jobsWaiting":true}` |

## Deliverables

1. **`WakeIntent::maxClaimsThisWake`.** 0 and 1 are one claim a wake, exactly as today; a
   larger number is the most claims this wake makes. It is not sent to the server: it is this
   end's own bound, set by the board that knows its battery.
2. **When a wake claims again.** After the jobs of a claim have run, `wakeJobs` claims again
   only when all of these hold: the answer to the last result this wake posted said
   `jobsWaiting: true`; no result of that claim was kept back for the budget; the last claim
   handed over at least one job; `claimMax()` is at least 1; there is uplink time left; and
   fewer than `maxClaimsThisWake` claims have been made. Anything else ends the wake's jobs,
   and the report says which.
3. **What the server said, carried up.** `postJobResult` learns `jobsWaiting` from the answer:
   present and `true`, present and `false`, or not there. Not there is `false` here. The
   uplink's interface carries it without a second parse of the body where there is one already;
   the Heltec's client reads it too and `loop()` does nothing with it.
4. **`transactionsLeft` in the later claims.** The first claim of a wake is `{"max":N}` and
   nothing else, to the byte. Every later one states what is left of
   `maxTransactionsThisWake` when the intent stated one, and leaves the member out when it
   stated none.
5. **Every claim is priced as the first is.** The jobs of a later claim meet the same checks
   before they run (`wakeJobs` ~L804–870): the transactions left, the time left, a job the mesh
   declines. The wake's report counts claims made and jobs run over the whole wake.
6. **`loop()` is what it was**: its bytes, its cadence, its counters. A board that never sleeps
   claims at its poll.
7. **The fixtures.** `ClaimRequestLeft.json` and `JobResultReplyWaiting.json` in
   `tests/fixtures/contracts-ahead/`, written from the lines above, read by the tests that hold
   the writer and the reader to them, with the directory's own tripwires. (`ClaimRequestHolding`
   and `ClaimRequestHoldingNothing` are task 70's.) `tests/fixtures/contracts-ahead/README.md`
   says what waits again.
8. **Tests**, on the host: a wake with `maxClaimsThisWake` 4 and a server that says
   `jobsWaiting` after each result makes four claims and no fifth; with 0 it makes one; a
   server that never says it gets one claim; an empty claim ends it; a kept result ends it; a
   wake whose transactions run out ends it and the later claim's bytes carry what was left; the
   first claim's bytes are `{"max":2}`; a wake's seconds running out between two claims ends it
   with *no time* and no claim half made.
9. **The words.** `docs/library.md` where it describes a wake, and a line in
   `release-notes/unreleased/`.

## Acceptance criteria

- [ ] With `maxClaimsThisWake` unset a wake's calls and bytes are what they are today (the existing tests of `wake()` pass unedited; name any that had to change and why).
- [ ] A wake allowed four claims, told `jobsWaiting` each time, makes exactly four (test's name).
- [ ] No later claim is made after a result was kept, after an empty claim, without `jobsWaiting: true`, or with less than a job's transactions left (four tests' names).
- [ ] The second claim's body is `ClaimRequestLeft.json`'s shape with the wake's own number, and the first is `{"max":2}` (tests' names).
- [ ] `loop()`'s claim and result bytes are unchanged (test's name).
- [ ] The two waiting fixtures are the two lines above, byte for byte (the test that holds them).
- [ ] `pio test -e native` green, both boards build, the frame rail and the library's host rail (`tools/check-library-host.py`) green, CI green.

## What only a board can show

Nothing on a Heltec changes. The Walter turns this on in its own pile 18, after its sync.

## Out of scope

More than two jobs in one claim. Acting on `jobsWaiting` in `loop()`. What a battery band
allows: the Walter's. The server: meshmaster.dk's pile 4.47, deployed before this is released.
