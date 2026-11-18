# 61 — The server asks

**Depends on:** 60
**Spec:** meshmaster.dk's `tasks/phase-4.42/244-an-update-is-asked-and-answered.md` (the other end, with the same contract as below; it writes `docs/dev/exit-node-updates.md`, which may not be there yet); `tests/fixtures/contracts/README.md`, `tools/sync-fixtures.ps1` and `docs/upstream.md` (how a fixture arrives here), and task 56's waiting fixture in `tests/fixtures/contracts-ahead/`; `lib/agent-core/include/agent/AgentLoop.h` (~L1–50: the heartbeat, its reply and the claim; ~L294–330: `IHeartbeatObserver`, an optional member of the heartbeat that a consumer provides; ~L527–541), `IUplink.h` (~L66–101) and `IJobExtension.h`; `docs/library.md` (§4 `IUplink`, §6 the lifecycle, §9 the fixtures are the wire contract, §10 versioning: what is a compile error for a consumer, ~L1365); `src/agent/AgentClient.cpp` (`postHeartbeat` ~L402, the result's post ~L513–535), `src/main.cpp` (~L737: where the observer is handed to the loop); `test/test_envelopes`, `test/test_contract_fixtures`, `test/test_loop`, `test/test_library`, `test/test_seams`; tasks 57, 59 and 60 (the slot, the fetch, the switch and the trial, and their records); `README.md`'s rails; `docs/field.md` §5 and §9.
**Size:** one session, a long one.
**Model:** claude-opus-5

## Goal

A person at the console can add the slot, fetch a release, switch to it and see it proved or
undone (57, 59, 60). This task lets a person on meshmaster.dk do the same with one press.

The server does not send a job for it: a job belongs to a repeater. It asks in the reply to
this board's heartbeat, the board does the steps in order by itself, and it posts one result
at the end. An exit node's heartbeat goes out every thirty seconds to two minutes, so an ask
is heard within that.

Nothing starts an update but an ask or a person at the console.

## The contract

Three shapes and one route. Not a job: a job belongs to a repeater, and this is about the exit
node itself. Five fixtures, each one line and a newline, and the five lines are below.
meshmaster.dk's task 244 makes them with its serializer. The firmware's task 61 is written
against the same lines and keeps them as waiting files until a sync brings the real ones, so
the two tasks can land in either order and neither may differ from a line by a byte.

**1. The heartbeat's `update`.** Written by a build that can be updated from the server, and
by no other. `HeartbeatUpdate.json`, the member alone:

```
{"table":"one","id":17,"step":"proving"}
```

| | |
|---|---|
| `table` | `one`: the board holds this firmware's table with one application slot, and the firmware adds the second when an update is asked for. `two`: a second slot is there. `other`: a table this build does not know, and no update from the server |
| `id`, `step` | only while an update is in hand: the ask's `id`, and the newest step of its journal |

The member is the heartbeat's last. `AgentHeartbeatUpdate.json`, a heartbeat that carries it,
is `AgentHeartbeat.json` with the member added:

```
{"agentVersion":"0.3.1","transport":"onboard","at":"2026-09-23T14:00:00Z","companionPublicKey":"a1b2","companionFirmware":"v1.17.1","radio":{"freqMHz":869.618,"bwKHz":62.5,"sf":8,"cr":8},"jobTypes":["status","telemetry","neighbours","config.read","config.apply","cli","trace","discover","advert.request","clock.sync","password.set","acl.set","reboot"],"budget":{"maxTransactionsPerMinute":2,"minGapSeconds":20},"companionConnected":true,"queueDepth":0,"errors":{"uplink":0},"board":"heltec_v3","nextCheckInS":3600,"maxTransactionsThisWake":12,"update":{"table":"one","id":17,"step":"proving"}}
```

**2. The heartbeat reply's `update`: the ask.** In every reply to that exit node from the
moment a person asked until the result is recorded, or the ask is withdrawn or runs out.
`UpdateAsk.json`, the member alone:

```
{"id":17,"releaseId":9,"product":"twinoak-exitnode-firmware","version":"0.8.0","base":"/agent/v1/firmware/9","expectedSha256":"3b1f0c4e5d6a79880817a6b5c4d3e2f1a0b9c8d7e6f5041322314051607f8e9d","size":1432000}
```

`base` is the release's route prefix, on the agent's token (meshmaster.dk's
`docs/dev/unit-protocol.md` §4): the manifest is `{base}/manifest.json`, its signature
`{base}/manifest.json.sig`, a file `{base}/files/{nameOrSha256}`. `expectedSha256` and `size`
are the file the server expects the board to choose. They are information, as in an offer:
the manifest names the files, and the manifest is what is checked.

The member is the reply's last. `HeartbeatReplyUpdate.json`, a reply that carries it:

```
{"serverTime":"2026-09-23T14:00:01Z","budget":{"maxTransactionsPerMinute":2,"minGapSeconds":20},"pollIntervalS":30,"jobsWaiting":false,"update":{"id":17,"releaseId":9,"product":"twinoak-exitnode-firmware","version":"0.8.0","base":"/agent/v1/firmware/9","expectedSha256":"3b1f0c4e5d6a79880817a6b5c4d3e2f1a0b9c8d7e6f5041322314051607f8e9d","size":1432000}}
```

An ask is taken once. The same `id` again while it is in hand changes nothing. The same `id`
after its result was posted has the result posted again.

**3. The result.** `POST /agent/v1/updates/{id}/result`, on the agent's token.
`UpdateResult.json`:

```
{"outcome":"rolledBack","fromVersion":"0.7.2","toVersion":"0.7.2","refusal":"check","check":4,"journal":[{"at":"2026-11-20T09:00:00Z","step":"check","note":"table one, 0.7.2 running"},{"at":"2026-11-20T09:00:01Z","step":"adding-slot"},{"at":"2026-11-20T09:00:40Z","step":"fetching","note":"0.8.0, 1432000 bytes"},{"at":"2026-11-20T09:01:10Z","step":"switching"},{"at":"2026-11-20T09:01:30Z","step":"proving","note":"mesh up, wifi up"},{"at":"2026-11-20T09:11:30Z","step":"rolling-back","note":"no heartbeat answered in 10 min"},{"at":"2026-11-20T09:11:50Z","step":"restored","note":"0.7.2 running"}]}
```

The fixture holds every field of the shape, so a parser meets each. No real result has a
`refusal` beside `rolledBack`.

| `outcome` | what it means | what the server does |
|---|---|---|
| `committed` | the new firmware runs and has proved itself | records it |
| `rolledBack` | the new firmware ran, did not prove itself, and the old one runs again | records it and raises nothing: it is the system working |
| `refused` | nothing was switched; `refusal` says why | records it |
| `stuck` | the board cannot say what happened | records it |

| `refusal` | |
|---|---|
| `inProgress` | an update is in hand, or a firmware is on trial |
| `table` | the board's table is `other` |
| `current` | the version asked for is the one running |
| `fetch` | the release could not be fetched |
| `check` | a check failed, and `check` is its number |

`check` is 1 to 5, as `FIRMWARE-MANIFEST.md` §5 numbers them: 1 the signature, 2 the manifest
and its product, 3 an image for this board, 4 the file's size and hash, 5 the descriptor in
the file.

`fromVersion` is what ran when the ask was taken, `toVersion` what runs when the result is
written.

`journal` has `FirmwareJournalEntry.json`'s shape and bounds: `at`, `step`, `note`, oldest
first, 40 entries and 2 000 characters of step and note in all. The step words are the
board's. The server keeps the journal as it came and reads `outcome`, `refusal`, `check` and
the two versions.

The answer is `200` and `{}`. An `id` that is not this exit node's, or is no ask at all, is
the one 404. A result posted again is `200`, and the first one stands.

**4. What a `2xx` to a heartbeat is.** A heartbeat whose `update` says `step` `proving`, and
which is answered `2xx`, is the third condition of the trial: the new firmware has reached the
server over TLS with its token.

**5. While an ask has been handed over and has no result**, the server hands that exit node no
job, and the board claims none while an update is in hand.

## Deliverables

1. **The library carries the two members and the post**, and nothing of an update. The
   heartbeat's `update` comes from a provider the consumer hands to the loop, as its
   `observer` does; the reply's `update` is handed to that provider; the result is posted
   for it. A consumer that hands none writes no member, reads none and compiles as before:
   no method of `IMesh`, `IUplink`, `IPlatform` or `IJobExtension` is added without a body,
   removed or changed. The Walter takes the library at the next release and must build
   unchanged.
2. **The heartbeat says what the board can do**: the table word of task 57, and the ask in
   hand with its newest step. A build with no release key (task 58) writes no member.
3. **An ask is taken once, in order:**
   - `check`: refused as `inProgress` when an update is in hand or a firmware is on trial,
     as `table` when the word is `other`, as `current` when the version is the one running;
   - `adding-slot`, when the word is `one`: task 57's function, then a restart, and the ask
     goes on after it. A table that is not `two` after that restart ends the ask `stuck`;
   - `fetching`: task 59, and its ending is the refusal `fetch`, or `check` with its number;
   - `switching`, then the trial and its end: task 60.

   Each step is one more entry of the journal, written before the step begins.
4. **The ask outlives the restarts.** Its `id`, the release and the step it stands at are in
   the settings store with task 60's record. A boot that finds an ask in hand goes on with
   it without being asked again, and takes the same `id` in a reply as nothing new.
5. **The trial's third condition** is a heartbeat that carried `update` with `step`
   `proving` and was answered `2xx`.
6. **One result.** When the ask has ended, by a refusal, a commit, a rollback or `stuck`,
   the result is posted to `/agent/v1/updates/{id}/result` as the contract has it, and
   posted again at each heartbeat's turn until it is answered `200` or the one 404. It
   survives a restart until then. The same `id` in a later reply has it posted once more.
7. **No job while an update is in hand.** From `check` passed until the ask has ended the
   board claims nothing, and a job it was running when the ask came is finished first.
8. **The rails hold.** The token and the manifest's bytes are in no log line. A result's
   journal says versions, sizes, steps and reasons, and no key, token or header.
9. **The words.** `docs/library.md`: the provider, in the section of the heartbeat's
   optional members, and in §10 that this is an addition. `docs/field.md` §5 and §9: what
   the exit node's page says while an update runs, and each ending. `README.md`: one rail,
   *an update is started by a person, on the server or at the console, and by nothing
   else*. One line in `release-notes/unreleased/`.
10. **The five fixtures wait here**, as task 56's does: each written from its line above with
    one trailing newline, into `tests/fixtures/contracts-ahead/`. `tests/fixtures/contracts/`
    is not edited and no sync is run. When a sync brings the real files they must be the
    same bytes, and these go (`FOLLOWUPS.md`).
11. **For the bench**, as ⚠ items in `docs/field.md`, with meshmaster.dk's pile 4.42
    deployed, release A on a board that has never had a second slot, and release B released
    or canary for it:
    - *Update to B* on the exit node's Firmware tab: within two minutes the page says
      *handed over*; the board restarts twice; the page ends *updated to B* with a journal
      of `check`, `adding-slot`, `fetching`, `switching`, `proving` and the commit;
    - the same press again: the page refuses with *the version is the one it runs*;
    - a job queued for a repeater of that exit node while the update runs is carried after
      it, and not lost.

## Acceptance criteria

- [ ] The five waiting files are the five lines above, byte for byte; the library writes `AgentHeartbeatUpdate.json` and reads `HeartbeatReplyUpdate.json` byte for byte; `AgentHeartbeat.json` is still what a consumer with no provider writes; `tools\sync-fixtures.ps1 -Verify` passes with the copied tree unchanged (tests).
- [ ] The result the firmware writes for the fixture's sample is `UpdateResult.json` byte for byte (test).
- [ ] `test/test_library` and `test/test_seams` build a consumer with no provider, unchanged (tests).
- [ ] An ask with the table `two`, a release that judges, and a trial that passes: the steps in order, one result `committed` with `fromVersion` and `toVersion`, and the journal's steps are the ones taken (test with scripted restarts).
- [ ] The same from the table `one`: `adding-slot` and a restart come first, and the ask goes on without a second ask (test).
- [ ] Each refusal of deliverable 3, alone: one result `refused` with that word, nothing fetched for the first three, nothing switched for any (tests).
- [ ] A trial that fails: one result `rolledBack`, posted by the old image, with `toVersion` the old version (test).
- [ ] The same `id` in three replies while in hand: one update. The same `id` after the result was answered `200`: the result is posted once more and nothing else happens (tests).
- [ ] A result's post that fails twice and then is answered `200`: three posts, then none; a restart between the second and the third does not lose it (test).
- [ ] A result answered with the 404 is not posted again (test).
- [ ] During the trial the heartbeat carries `"step":"proving"`, and the image is not marked valid before a `2xx` to such a heartbeat (test).
- [ ] From `check` passed to the end no claim is made; a job running when the ask came is finished and its result posted first (tests).
- [ ] A build with no release key writes no `update` member and answers an ask with nothing (test).
- [ ] `pio test -e native` green, both boards build, CI green.

## Out of scope

The server's side (meshmaster.dk's tasks 243 to 245). Progress posts: the heartbeat's `step` is all
the server hears before the result. An ask for another product. An update the server did
not ask for and no person started. The Walter, which updates itself by a job of its own.
