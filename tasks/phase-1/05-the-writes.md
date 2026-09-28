# 05 — The writes

**Depends on:** 04.
**Spec:** [CONFIGURATION §5, §6, §7](https://github.com/twinoak/meshmaster.dk/blob/fbf4625a0352663e1c26bd6877e45e6c555d4f7c/spec/CONFIGURATION.md), [EXIT-NODES §7](https://github.com/twinoak/meshmaster.dk/blob/fbf4625a0352663e1c26bd6877e45e6c555d4f7c/spec/EXIT-NODES.md), tasks p4-53, 54, 55, 57.
**Size:** one session.
**Model:** claude-fable-5-1

## Goal

The six job types that change a repeater — `config.apply`, `cli`,
`advert.request`, `clock.sync`, `password.set`, `reboot` — with every rule
phase 4 learned, on a board. It is Fable's for the same reason 53 and 55
were: a `set` sent twice is a change made twice, and a password line is a
password line.

## Deliverables

- **`config.apply`** (task 53): the plan's commands in order, each sent
  **exactly once**, every reply captured verbatim, stop at the first reply
  that does not start with `OK` and at the first silence (`not-sent` for
  the rest), then the re-read of every touched key plus `ver`; **strictly
  increasing sender timestamps** across every command of an apply and its
  re-read (task 57: an equal timestamp is a retry the firmware does not
  run); a regions entry as its lines (`ConfigCommand.Lines`), sent without
  waiting for a reply where the line expects none, the terminator (one
  space) sent even when the sequence dies; the result shape of
  `ConfigApplyResult`.
- **`cli`** (task 54): one line, one reply, the blocked list is the
  server's (the firmware sends what it is handed and refuses nothing but a
  malformed line); `advert.request` (`advert`, the flood); `clock.sync`
  (`time <unix>` from the board's clock, the reply recorded);
  `password.set` (task 55: the command with the candidate the claim
  named, the restore **after** the password line per task 60, the session
  dropped after); `reboot` (the command, no restore, the session dropped,
  `ok` on silence).
- **The restore** on every admin session as 04 built it, and the
  exceptions of task 60 (`reboot`, `password.set` after).
- **`jobTypes`** becomes all thirteen; the heartbeat says so.
- **The console**: `log.frames` covers the writes with the same redaction
  (the `password` line's value is `*`s).

## Acceptance criteria

- [ ] Native tests: an apply's commands are sent once each, in order, with strictly increasing timestamps; a rejected second command leaves the third `not-sent`; the region sequence sends the terminator after a failure; the result JSON equals the .NET expectation for the same replies.
- [ ] `password.set` restores after the password line and drops the session; `reboot` sends no restore.
- [ ] `pio run -e heltec_v3` green; the heartbeat advertises the thirteen types.

## Not verified here (Thomas, with the board)

`FIRST-APPLY.md` step one through the board (a harmless key), then the
refusal, then the partial plan; a rotation on a test repeater; the
transaction counts.

## Out of scope

Anything that changes what the server sends.
