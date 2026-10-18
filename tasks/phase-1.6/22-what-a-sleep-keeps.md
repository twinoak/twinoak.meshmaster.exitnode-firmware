# 22 — What a sleep keeps

**Depends on:** 21.
**Spec:** the 1.5 review ([`../phase-1.5/FOLLOWUPS.md`](../phase-1.5/FOLLOWUPS.md), *The offline result buffer and the observation ring do not survive a deep sleep*); task p1.5-18 (`state()` / `restore()`, `WakeState` as a trivially copyable POD for retained storage); `lib/agent-core/include/agent/AgentLoop.h` (`WakeState` ~L213, `state()`/`restore()` ~L274), `ResultBuffer.h` (12 KiB arena, eight results, a queue), `Observations.h` (`ObservationRing`, 64 adverts), `IPlatform.h` (seven primitives and a clock), `JobRunner.cpp` (the credential wipes); `docs/library.md` §5 and §6; the hardware repository's `software/spec/SYSTEM.md` §5 (*duty cycle*); the Walter's phase-3 review (its RTC memory is nearly full) if `twinoak.meshcore.walter-firmware` is beside this repository.
**Size:** one session.
**Model:** claude-fable-5-1

## Goal

A result whose post failed is kept in RAM and sent again — on a Heltec,
which does not sleep. On a unit, the next deep sleep loses it, and its job
stays leased until the server's lease lapses: the promise *no job left leased
and silent* holds only within one boot. The same is true of the adverts a
wake could not post. `WakeState` cannot carry them (twelve kilobytes of
results against an RTC memory that is nearly full). Give a sleeping consumer
a way to keep them across the sleep in storage it chooses, and make losing
them a decision with a counter instead of an accident.

## Deliverables

- **A seam for what outlives a sleep.** An optional interface the consumer
  hands to the loop (`IRetained` or a better name; one header, in the
  library's own style), with the smallest surface that holds: its capacity in
  bytes, write one image, read it back. Flash or NVS behind it is the
  consumer's business. No allocation in the library; no file system in its
  includes; the Heltec passes none and is unchanged, byte for byte.
- **One image, written once a wake.** At the end of `wake()` — and only when
  what it would hold changed — the loop writes a versioned image with a
  checksum: the kept results first, oldest first, as many whole results as
  fit; then the ring's waiting adverts with their original timestamps, as
  many as fit after them. Results come first because a lost result leaves a
  job leased; a lost advert is heard again. What does not fit is counted in
  the counters the heartbeat already has (`resultsDropped`,
  `observationsDropped`) and logged once. `restore()` (or a sibling the
  consumer calls with it) reads the image back into the buffer and the ring;
  a torn, short, foreign-version or bad-checksum image is discarded whole,
  counted and logged — never half-read.
- **Nothing secret goes to storage.** Result bodies are written as they would
  be posted, and the runner's rule already keeps credentials out of them;
  prove it on the image's bytes with a rotation, a guest login and a `cli`
  job's results, using the fixtures' sample passwords, and say in the header
  that the image may hold nothing a result does not.
- **Wear.** A wake writes at most one image, none when nothing is kept and
  the previous image is already empty; the header says so, so a consumer can
  size its storage for a few writes a day.
- **Tests** with 17's fakes and a fake store: a sleep simulated by destroying
  the loop and the runner and building new ones from `state()` plus the
  image — a kept result is posted on the next wake with its original client
  id (the server de-duplicates on it) and its job does not stay leased; a
  store too small for two results keeps the oldest whole one; adverts keep
  their timestamps across the sleep; each kind of bad image is discarded and
  counted; the Heltec's suites untouched.
- **`docs/library.md`** §5/§6 gain the seam and the rule (*kept results
  first, then adverts, whole or not at all, one write a wake*), and §11 what
  the Walter implements for it. `CHANGELOG.md` gets the line under a new
  `v0.3.0 (unreleased)` heading, which 23 and 24 fill and close.

## Acceptance criteria

- [ ] With a store, no claimed job is left leased across a simulated sleep when its result was kept (the property test of 18 and 21, extended across a sleep).
- [ ] Without a store, behaviour and the Heltec's heartbeat bytes are unchanged.
- [ ] Every bad image is refused whole and counted; the image never contains a credential (test on bytes).
- [ ] `pio test -e native`, `check-library-host.py` (20's matrix) and both board builds green.

## Not verified here

The Walter's store over its flash, and a real deep sleep (its phase 4).

## Out of scope

A journal for the Heltec (it does not sleep, and a power cut losing a result
it could not post is the documented behaviour of EXIT-NODES §5). Changing the
buffer's or the ring's sizes.
