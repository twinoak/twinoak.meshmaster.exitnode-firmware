# Pile 1.3 — follow-ups

## Review of 12–13 (6 October) — the pile, after it landed

Read after PR #15 merged (the driver finished at 19:59 on 5 October) and
after Thomas tagged `v0.1.2` and `v0.1.3` and flashed the Heltec with the
latter on 6 October, against the two reports and the code they name. 12
ran in the evening, 13 took 28 minutes on Fable; no stop. One remote branch
the driver could not delete (`task/p1.3-13-the-rotation-probe`): Thomas's
to remove.

**12 — the small ones** did the four and sized the read for what
meshmaster.dk's 102 actually sends (29 keys, 32 transactions): the cap is
32, raw lines twelve with an overflow line, and `kDataCap` 8 192 with the
writer degrading before it fails, because the task's "size it from the
constants" is 65 KB once escaping is counted - the right reading of a
wrong instruction. `kDefaultConfigKeys` is the server's sixteen with
`agc.reset.interval` in its place (08's finding 6, closed). The 32-key
read is held at the snapshot and the writer, not over the air, because
the fake repeater is still on 08's branch. `online()` has no native seam,
as the task allowed.

**13 — the rotation probe** lifted the login sequence out of
`ensureSession` into `LoginSequence.h/.cpp` behind a three-method exchange
so that it could be tested at all natively (`JobRunner.cpp` needs the
radio and is outside `[env:native]`), and walks it against a scripted
repeater for all six of 95's cases. `ProbeAnswer` has three values, not
the task's four: a send the gate refused and a silent probe teach the same
nothing. The result record is the contract's `LoginSilentResult` byte for
byte and no fixture moved. Right, and better structured than the task
asked.

### What is open

- [ ] **08's resume now rebases over four things**: 09's `main.cpp`
  (`fillOled`), master's `AgentMesh` (the login fix it also carries), 12's
  constants and 13's `ensureSession` → `LoginSequence`. The resume session
  merges master first and expects all four; its harness is then the first
  place the 32-key read and the rotation probe run over a fake that answers
  as the firmware does - 12 and 13 both say so. Still waiting on the four
  clicks (`tasks/phase-1.1/logs/STOPPED-08.md`).
- [ ] **The bench, now that `v0.1.3` is on the board**: the four lines; `no
  token: set it` after a merged flash and before the token; `no server:
  retry 45s` through the next meshmaster.dk deploy; a job line while a job
  runs; and docs/console.md's item 17, a rotation whose reply is lost, which
  is the probe's one real test.
- [ ] **`online()` is bench-only** (12): a native seam for `AgentLoop`'s
  state would make the display's truth table a unit test; it is the same
  seam 13 cut for the login sequence, one level up. Small, when the next
  display change comes.

### Watch

- The OLED after a month lit (the ghosting note of 1.2).
- `v0.1.2` was the partition-table release: a board provisioned under
  `v0.1.1` or earlier re-provisions once on its first merged flash of it or
  later; README's *Flashing it* says so.
