# Pile 1.20 — follow-ups

Review findings, tagged with the task that must pick them up. Only the reviewer ticks an item.

- [ ] **[next] The four waiting fixtures are reconciled** once meshmaster.dk's master has them
  (its pile 4.47): sync, compare byte for byte, read them from the copied tree, remove the
  waiting files.
- [ ] **[later] A board that never sleeps could claim at once on `jobsWaiting`.** It claims at
  its next poll, thirty seconds on; 69 leaves `loop()` alone.
- [ ] **[later] The claim's room is two.** `kClaimMax`, the runner's scratch, the result
  buffer's eight results and the Walter's 6 144 bytes for a claim's answer are all sized for
  it. A wake of several claims does not need more; a board on mains might.
