# 70 — A claim that was lost

**Depends on:** 69
**Spec:** `lib/agent-core/src/AgentLoop.cpp` (`wakeJobs` ~L789–795: a failed claim ends the wake's jobs; `loop()`'s own claim), `lib/agent-core/src/JobRunner.cpp` (`claim` ~L2167–2190), `lib/agent-core/include/agent/IUplink.h` (`claimJobs` ~L86, `lastCall()`), `lib/agent-core/include/agent/ResultBuffer.h`, `lib/agent-core/src/JobEnvelopes.cpp` (`writeClaimRequest` ~L162), `src/agent/AgentClient.cpp` (the claim ~L600–620); `tests/fixtures/contracts-ahead/`; meshmaster.dk's tasks p4.47-253 and p4.47-254, and its `src/MeshMaster.Core/Transport/LostLease.cs` for the story
**Size:** one session.
**Model:** claude-opus-5

## Goal

The server leases the jobs of a claim in the moment it answers. When the answer does not
arrive, it holds leases this end knows nothing of. On 21 November the Walter at Bellevue
claimed at 19:11Z, the answer came back *http 200* without its body, and the server had leased
it a download and a status read. The download has one attempt and was failed as *the exit node
took this job and never reported a result*. That unit counts 28 failed claims in 1 628
heartbeats.

After this task every claim says what this exit node holds, and a claim that did not come back
is made once more. meshmaster.dk's task 254 answers such a claim with the jobs the first one
lost, first, with no attempt spent.

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

1. **`holding` in every claim**, `loop()`'s and `wake()`'s alike: the ids of the results this
   end keeps and has not had taken, oldest first, and `[]` when there are none. The runner
   claims between jobs, so a job in hand is never among them; say in the report that this is
   so and where the code makes it so. The writer's bound: as many ids as the result buffer
   holds results.
2. **One more claim.** A claim whose request went out and whose answer did not come back whole
   (no reply after the request was sent, a 2xx without its body, a body that does not parse) is
   made once more in the same wake when there is uplink time for it, with the same `max`. One,
   not a loop: a second failure ends the wake's jobs as a failure does today, and counts once in
   `claimFailures`. A claim that was refused (a 4xx, a 5xx, a 429) is not made again: the
   server spoke.
3. **In `loop()`** the next poll's claim is that second claim already; nothing is added there
   but `holding`.
4. **A job handed over twice is run once.** A job whose id this end keeps a result for is not
   run when it is handed over again: its kept result is posted. With `holding` the server does
   not hand it over; this is the rail for the server that does.
5. **The fixtures.** `ClaimRequestHolding.json` and `ClaimRequestHoldingNothing.json` in
   `tests/fixtures/contracts-ahead/`, with the tests that hold the writer to them.
6. **Tests**, on the host: the evening of 21 November (a claim answered 200 with no body, the
   second claim made at once with `holding: []`, its two jobs run); a second failure ends it; a
   429 is not asked again; a kept result's id is in `holding` and its job, handed over again, is
   not run; no uplink time left means no second claim.
7. **The words.** `docs/library.md`; a line in `release-notes/unreleased/`.

## Acceptance criteria

- [ ] Every claim this library writes carries `holding`, and with nothing kept its bytes are `ClaimRequestHoldingNothing.json` (test's name).
- [ ] After a claim answered without its body, exactly one more claim is made in the same wake, and none after a refusal (tests' names).
- [ ] A job handed over again whose result is kept is not run and its result is posted (test's name).
- [ ] `claimFailures` rises by one for a claim that failed twice and by none for one that came back the second time (tests' names).
- [ ] The two waiting fixtures are the two lines above, byte for byte.
- [ ] `pio test -e native` green, both boards build, the frame rail and the library's host rail (`tools/check-library-host.py`) green, CI green.

## What only a board can show

- ⚠ On the Walter, after its sync and its pile 18: the unit's `agent calls` list with a claim in
  it that did not come back, and the job of that wake run all the same.

## Out of scope

Why the answer is lost on the Walter's modem: parked until its bench. A lease that has run out
before the board comes back: the server's sweep. The server's half: meshmaster.dk's task 254,
deployed before this is released, because an older server has never seen `holding`.
