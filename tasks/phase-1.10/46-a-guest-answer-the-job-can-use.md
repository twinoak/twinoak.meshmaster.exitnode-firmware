# 46 — A guest answer the job can use

**Depends on:** nothing in this pile.
**First:** in `meshmaster.dk` (read only), check that `master` has `tasks/phase-4.30/reports/180.md`. If it does not, write `logs/STOPPED-46.md` saying so, change nothing else, and end. 180 sets the contract this task follows.
**Spec:** meshmaster.dk, read only, as merged with 180: `spec/API.md` §3 (where `loggedInAs` goes on an `ok`), `spec/EXIT-NODES.md` (which types a guest login serves on 1.18), `src/MeshMaster.Agent/Handlers.cs` and `Sessions.cs` (the .NET agent's half); this repository: `lib/agent-core/src/JobRunner.cpp` (the login judged against the class ~L205–240, `ctx.grantedBelowClass`, the result ~L1670–1685), `include/agent/Sessions.h` (`satisfies`), `src/ReplyCodec.cpp` (`roleNeededFor`), `include/agent/JobResults.h` (`writeLoginBelowClassResult`: the `{needed, loggedInAs}` of an `unauthorized`); `test/test_runner`, `test/test_job_results`; MeshCore 1.18 `examples/simple_repeater/MyMesh.cpp` (`handleRequest`: nothing checked for `REQ_TYPE_GET_NEIGHBOURS`; status *guests can also access this now*; telemetry masked to the base channels for a guest).
**Size:** one session, short.
**Model:** claude-opus-5

## Goal

On 10 November two neighbour reads of a repeater failed `unauthorized`,
`{"needed": "read-only", "loggedInAs": "guest"}`. The login at class `acl` was answered at guest,
and agent-core refused before sending a request the firmware answers to a guest. meshmaster.dk's
180 made the server stop creating such jobs where it can. It also said what an agent does with
one that still happens, for example an `acl` pair whose entry the owner removed:

- run `status` and `neighbours` anyway;
- return `ok` with the data and `loggedInAs`;
- leave everything else as it is.

The server reads `loggedInAs` as the same evidence the `unauthorized` carries, so nothing about
the pair is hidden. This task is agent-core's half.

## Deliverables

1. **The rule.** A login at class `acl` answered below `read-only`, for a `status` or `neighbours`
   job: the session is kept at the tier granted, and the job runs and reports `ok` with
   `loggedInAs` where 180 put it.
   - `telemetry` is still refused: a guest gets the base channels only.
   - So is everything else, and every class other than `acl`, as today, with
     `writeLoginBelowClassResult`.
   - A session kept this way never satisfies a later job of a class it does not satisfy
     (`Sessions.h`).
2. **Tests.**
   - `status` and `neighbours` after an `acl` login answered at guest: `ok`, data, `loggedInAs`.
   - `telemetry` and `config.read` after the same login: `unauthorized` as before.
   - A grouped claim (two jobs, one login), where the second job is one that must still be refused.
   - The session store not handing the guest session to an `acl`-class job that needs more.

## Acceptance criteria

- [ ] The tests above green, and the existing login-class tests unchanged except where this rule changes their outcome (named in the report).
- [ ] The Heltec build and the host tests green; CI green.
