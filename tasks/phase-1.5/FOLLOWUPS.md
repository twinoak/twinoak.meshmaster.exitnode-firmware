# Pile 1.5 — follow-ups

Review findings, tagged with the task that must pick them up: `[NN]` for a
task of this pile. A session reads this file before it starts (RUNBOOK.md
section 0) and treats every open item tagged for its task as part of the task.
Only the reviewer ticks an item.

## Left by 15 (14 October)

- [ ] **Delete `origin/task/p1.1-08-the-host-harness`** (Thomas). Task 15's
  report says why in one paragraph: the host harness 08 wanted is this pile,
  15–17, built the other way round — interfaces in front of the mesh, the
  uplink and the platform rather than a second `JobRunner` for the host —
  and nothing from that branch is merged. The three readings it left behind
  that *were* worth keeping already landed on `master` by other routes (the
  login reply that carries no tag of ours, in `AgentMesh::sendLogin`; the
  `agc.reset.interval` key, 12; the rebase notes in the FOLLOWUPS of 1.2–1.4).
  `tasks/phase-1.1/logs/STOPPED-08.md` is git-ignored and goes with the clone.
- [ ] **`LINK-PROTOCOL.md` §6 and `IMesh`** (Thomas, in
  `twinoak.meshcore.standard-repeater-design-v2`): 15's report walks `IMesh`
  method by method against §6 and finds three things the link does not
  carry — `uniqueTime()` (nothing in the library calls it since 15; drop it
  from `IMesh` in 16 or 17 rather than add a request), `overheard()` and the
  per-type receive counters behind it (§6.4 `MESH_COUNTERS` has no
  `overheard` or by-type figure, and the heartbeat's `overheard` counter needs
  one), and the awaited-reply timeout, which the link takes at the send
  (`timeout_ms`) and the interface takes at `awaitReply()` — implementable
  with a ceiling at the send, but worth a sentence in §6.2. `setRing()` is
  §7's `OBS_PULL`, not §6's, and the task's "§6 alone" should read "§6 and
  §7".
