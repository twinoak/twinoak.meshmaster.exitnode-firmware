# 55 — A batch keeps its id over a sleep

**Depends on:** nothing in this pile.
**Spec:** `lib/agent-core/include/agent/KeptImage.h` (the layout, version 1; the header's `reserved`), `lib/agent-core/include/agent/IRetained.h`, `lib/agent-core/include/agent/AgentLoop.h` (`setRetained` and `restoreKept` ~L370–391, `batchId_` and `batchCount_` ~L544), `lib/agent-core/src/AgentLoop.cpp` (`buildBatch` ~L426, `doObservations` ~L489 and its first comment, `wakeObservations` ~L752, `setRetained` ~L924, `restoreKept` ~L954, `keepAcrossSleep` ~L1028), `lib/agent-core/include/agent/Observations.h` (the ring drops its oldest when full; `consume`), `test/test_loop/test_loop.cpp` (the kept image's cases), `docs/library.md` (the kept image), meshmaster.dk's `spec/EXIT-NODES.md` §6 (*de-duplicated on `clientId`*)
**Size:** one session.
**Model:** claude-opus-5

## Goal

`doObservations` says it itself: a batch built and not yet accepted *must go out again exactly
as it was, under the client id it was built with*, because the server de-duplicates on that id.
The id and the count live in two fields of the loop. A board that sleeps loses its RAM, and the
image a sleep keeps holds the adverts and not the batch they were in.

The Walter unit shows what that costs. The answer to its observations post is lost at nearly
every wake (its own fault, its own pile). The server has the batch. The unit sleeps, wakes with
the adverts and no id, builds a new batch of the old adverts and the new ones, and posts it. The
server cannot know, and stores every advert again: on 16 November batches of 13, 14, 15, 16, 17
and 18 adverts arrived in six wakes running, and the 100 newest rows from that unit were 19
different adverts.

With the id kept, the first post after the sleep is the same batch. The server answers *had it
already*, the ring is drained, and what was heard since goes out as a new batch.

The Heltec hands the loop no store and loses no RAM. Nothing changes for it.

## Deliverables

1. **The image keeps the pending batch.** Its id and how many of the ring's oldest adverts it
   holds, in a version 2 of the kept image. Written at the end of every wake that leaves a
   batch pending. Where the bytes go is the session's to choose; the layout is written down in
   `KeptImage.h` as version 1's is.
2. **`restoreKept` puts it back.** After it, the first observations post carries that id and
   those adverts, oldest first, and a server that answers *had it already* costs the ring
   exactly those. What was heard since follows as a new batch under a new id.
3. **A batch that is no longer whole is no longer that batch.** If the adverts it named are not
   all still the ring's oldest (the image did not hold them all, or the ring was full and
   dropped its oldest while the batch waited), the id is forgotten and a new batch is built.
   Say in the report what the loop did in that case before this task, with or without a sleep.
4. **Version 1 is still read.** An image the library wrote before this task is taken at the
   first boot after an update, with no batch pending. Any other version is refused whole, as
   today.
5. **A figure for whoever sizes the scratch.** The largest image the library can write, as a
   constant, so that a board's buffer is held to the library's number and not to a sum done by
   hand.
6. **Nothing for a board with no store.** The Heltec's build and behaviour are as they are.
7. **The words.** `docs/library.md`. One line in `release-notes/unreleased/`.

## Acceptance criteria

- [ ] A batch of five posted, the answer lost; the image is written, the loop is rebuilt as after a deep sleep and `restoreKept` runs: the next post carries the same id and the same five (test).
- [ ] The server answers it *had it already*: five leave the ring; three heard after the sleep go out next, under another id (test).
- [ ] The same with the answer lost a second time and a second sleep: the third post is still that id and those five (test).
- [ ] A batch pending, and the ring filled until it drops its oldest: the id is forgotten and the next batch is a new one; nothing is consumed unsent (test).
- [ ] An image that could not hold every advert of the pending batch: no batch is pending after `restoreKept` (test).
- [ ] A version 1 image written by the code as it was: restored, no batch pending, nothing refused (test, with the bytes as a fixture).
- [ ] An image of a version this code does not know: refused whole and counted, as today (test).
- [ ] The largest-image constant equals what the writer produces for a full result buffer, a full ring and a pending batch (test).
- [ ] A loop with no store: every existing test unchanged.
- [ ] Tests as listed; the build and the native tests green.

## Out of scope

Why the Walter's answer is lost, and the Walter's side of this (its phase 15, tasks 02 to 04; it
takes this library at a release). The server: it de-duplicates on the id already. The adverts
meshmaster.dk stored twice. A release: Thomas presses the button.
