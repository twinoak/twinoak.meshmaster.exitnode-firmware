# 56 — A listing says how many there were

**Depends on:** —
**Spec:** meshmaster.dk's `tasks/phase-4.42/239-a-neighbour-stays-a-while.md` (deliverable 1: `total`, and the fixture `NeighboursResultTotal.json`); `tests/fixtures/contracts/README.md` and `tools/sync-fixtures.ps1` (the only way a fixture arrives here), `docs/upstream.md` (~L170: there first, here second); `lib/agent-core/src/JobRunner.cpp` (`runNeighbours` ~L1115–1160: the request's bytes ~L1124–1131, the two places a list stops ~L1136–1150, `total` ~L1151, the cap ~L1153–1155, the log line ~L1157), `lib/agent-core/include/agent/ReplyCodec.h` (~L145–166: nine a page, `kMaxNeighbours`, `NeighboursPage.total`), `lib/agent-core/src/JobResults.cpp` (`writeNeighboursResult` ~L112) and `lib/agent-core/include/agent/JobResults.h` (~L57), `lib/agent-core/include/agent/JobRunner.h` (~L213, ~L478); `test/test_job_results/test_job_results.cpp` (~L147: the writer held to the fixture byte for byte), the job runner's tests over `test/support/FakeMesh.h`; `docs/library.md`.
**Size:** one session, a short one.
**Model:** claude-opus-5

## Goal

A repeater's neighbour list is read nine to a page. Every page says how many neighbours the
repeater has. When a page goes unanswered after an earlier one was read, `runNeighbours`
reports what it has and the job ends `ok`: *the server gets what was heard rather than nothing
at all*. It writes *the list stops at 9 of 17* into the job's log, and a result of nine
neighbours.

The server cannot read a log. It takes the nine for the whole list, counts the other eight as
gone, and finds them again at the next listing. meshmaster.dk's task 239 gives the result a
place for the repeater's own count, `total`. This task writes it.

## Deliverables

1. **The fixture waits here.** `NeighboursResultTotal.json` is made by meshmaster.dk's task
   239, which may not be on its master when this task runs. So the file is written here
   from its one line, with one trailing newline as every fixture has, into
   `tests/fixtures/contracts-ahead/`: a directory the sync does not touch and `MANIFEST.txt`
   does not list.

   ```
   {"neighbours":[{"prefix":"a1","lastHeardS":42,"snr":7.25},{"prefix":"7f","lastHeardS":3600,"snr":-2.5}],"total":17}
   ```

   `tests/fixtures/contracts/` is not edited and no sync is run. When a later sync brings
   the real file the two must be the same bytes, and this one goes (`FOLLOWUPS.md`).
2. **The result carries `total`**, after `neighbours`: the count the repeater gave in the
   last page that was read. Written whenever the job ends `ok`. It is the number of entries
   when every page was read; more when a page went unanswered or did not parse after an
   earlier one was read, and when the list was cut at `kMaxNeighbours`; 0 beside an empty
   list.
3. **Byte for byte.** The fixture's two neighbours and its `total` through the writer are
   `NeighboursResultTotal.json`.
4. **Nothing else moves.** The pages asked for, the tries, the order, the log's lines and the
   outcome of every case are what they are today. A job whose first page is not answered ends
   as it does now, with no result.
5. **Room.** A result of `kMaxNeighbours` neighbours at the eight-byte prefix and a `total`
   fits the result buffer.
6. **The words.** `docs/library.md`, where the neighbours result is described.
   `docs/upstream.md`: what a waiting fixture is, and that the sync's copy is the one that
   holds. One line in `release-notes/unreleased/`.

## Acceptance criteria

- [ ] `tools/sync-fixtures.ps1 -Verify` green with `tests/fixtures/contracts/` unchanged; the waiting file is the line above and a newline, byte for byte (test).
- [ ] The writer, given the fixture's two neighbours and 17: the fixture byte for byte (test).
- [ ] A repeater scripted with 17 neighbours, both pages answered: 17 entries, `total` 17 (test).
- [ ] The same with the second page never answered: `ok`, 9 entries, `total` 17, and the log's line as today (test).
- [ ] The same with a second page that does not parse: 9 entries, `total` 17 (test).
- [ ] A repeater scripted with 60: 54 entries, `total` 60 (test).
- [ ] A repeater with none: `{"neighbours":[],"total":0}` (test).
- [ ] A repeater whose pages say 17 and then 18: `total` 18 (test).
- [ ] The first page never answered: the outcome and the absence of a result as today (test).
- [ ] 54 neighbours and a `total` fit the result buffer (test).
- [ ] The build and the native tests green.

## Out of scope

Asking a page again before the list is given up (`FOLLOWUPS.md`). What the server makes of
`total`. The Walter: it takes this library at a release. A release: Thomas presses the button.
