# 66 — The receiver is put back on a timer

**Depends on:** —
**Spec:** MeshCore at this firmware's pin, read only (`.pio/libdeps/heltec_v3/MeshCore`): `src/Dispatcher.h` (`resetAGC` ~L70; `getAGCResetInterval` ~L172: *disabled by default*), `src/Dispatcher.cpp` (~L122–136: the timer starts again after every send, and the reset is made when it has run out), `src/helpers/radiolib/RadioLibWrappers.cpp` (`doResetAGC` ~L70: *warm sleep to reset analog frontend*; `resetAGC` ~L74–85: not during a receive, and the noise floor is 0 until it is measured again), `examples/companion_radio/MyMesh.h` (~L133: upstream's companion turns it on from a setting); `src/mesh/AgentMesh.h` (the class ~L79; what it overrides of `mesh::Mesh` ~L205–216) and `AgentMesh.cpp`, `src/settings/Keys.h` (`KeyDef` ~L51, `kKeyCount` ~L66) and `Keys.cpp` (the table ~L33; the `radio.*` rows ~L38–42), `src/main.cpp` (`applyRadioSettings` ~L336; `printStatus` ~L440: its `radio` line ~L462 and `frames` line ~L468; what the observer is told of the floor ~L327), `src/observer/Dialect.cpp` (~L228–240: `noise_floor` is written when it is over -998); `test/test_console_parser`, `test/test_observer`, `test/test_flood_scope` (~L516: a rule of `AgentMesh` held by reading its source); `docs/console.md` (the keys ~L62–66, ~L88, ~L103), `docs/field.md`; `tasks/phase-1.17/FOLLOWUPS.md` (*A night Risskov did not hear*).
**Size:** one session.
**Model:** claude-opus-5

## Goal

From the evening of 19 November until it was restarted the next morning, the exit node at
Risskov did not hear what answered it. Most reads through it ended *no reply (direct)*: 10 of
12 hours for one repeater, 11 of 12 for another, and its first try at the repeater that stands
beside it failed every hour from 20:00 to 06:00 UTC. What it sent arrived as it always does: a
repeater heard it at the same -96 dBm in the middle of that night as by day. The two nights
before were clean, the mesh was ordinary, and the exit node at Aarhus C, on the same firmware,
had a clean night. A restart cured it.

A receiver that stops hearing until its radio is restarted is what MeshCore resets the radio's
AGC for. The dispatcher does it on a timer, and the timer is off unless the application turns
it on. Upstream's companion and repeater turn it on from a setting. This firmware never has.
Two repeaters had stood beside the board for days, heard at -41 and -53 dBm, and were handled
and moved that afternoon and evening.

That the AGC was the cause is not shown: nothing the server keeps says what an exit node's
receiver did. The reset is what upstream has for it, the standard profile sets it to four
seconds on the repeaters, and it would have ended that night within one interval.

## Deliverables

1. **The exit node resets its radio's receiver on a timer**, by MeshCore's own means at the
   pin: the dispatcher's timer and the radio's reset. Nothing of upstream's is edited, and
   there is no second timer of ours.
2. **A setting, `agc.reset.interval`, in whole seconds**, which is the repeaters' own word for
   it. 0 is never. It is 4 unless set, as the standard profile gives the repeaters. 0 to 3600.
   It takes effect when it is set, with no restart. It is not in the heartbeat and nothing of
   the contract with meshmaster.dk changes.
3. **A floor that is being measured again is no reading.** After a reset the radio's noise
   floor is 0 until it has been sampled again. `status` says that it is being measured, and
   the observer's message leaves `noise_floor` out for that moment. Nowhere is a 0 printed or
   published as a level.
4. **`status` says the interval** on its `radio` line: every how many seconds, or never.
5. **Nothing else of the radio changes.** No interference threshold, no channel-activity
   check, and a job waits for a reply exactly as long as it did.
6. **The words.** `docs/console.md`: the key, the `radio` line, the floor being measured.
   `docs/field.md`: a board that stopped hearing, what was seen on 19 November, that `reboot`
   ends it at once, and the setting. One line in `release-notes/unreleased/`.

## Acceptance criteria

- [ ] `agc.reset.interval` is a key with 4 as what the board uses while it is unset; it takes 0, 1 and 3600 and refuses -1, 3601 and 2.5 (tests).
- [ ] The interval the dispatcher is given is the setting in milliseconds, and 0 when the setting is 0, from one function (test); `AgentMesh` hands that function's answer to `getAGCResetInterval` and no line of ours calls a reset of the radio (a test that reads the sources, as `test/test_flood_scope` does).
- [ ] A setting changed at the console is what the next call of that function answers, with no restart (test).
- [ ] With a floor of 0: `status`' text says it is being measured and has no `0 dBm`, and the observer's message has no `noise_floor` member; with -109 both are as they are today (tests).
- [ ] `status`' `radio` line names 4 s by default, the number that was set, and *never* at 0 (test, by whatever reaches that line's text).
- [ ] The key is in `docs/console.md`'s table, and the count of keys agrees wherever it is held (the existing rails).
- [ ] `pio test -e native` green, both boards build, the frame rail green, CI green.

## What only a board can show

- ⚠ `status` on a V3 after the flash: the `radio` line says *every 4 s*, the floor is a number
  again within a moment of *being measured*, and `frames rx` goes on rising.
- ⚠ A week of nights on Risskov with nothing like 19 November. The state cannot be brought on
  at will, so this is the only proof there is.

## Out of scope

Noticing that a receiver has stopped hearing, on the board or on the server (`FOLLOWUPS.md`).
A counter for it in the heartbeat. The repeaters' own setting and the profile's four seconds.
The Walter: its radio is a repeater and has the repeater's setting.
