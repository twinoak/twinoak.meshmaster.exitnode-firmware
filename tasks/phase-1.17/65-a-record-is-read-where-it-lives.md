# 65 — A record is read where it lives

**Depends on:** —
**Spec:** `src/update/UpdateRecord.h` (`Journal` ~L102–144: `kCapacity` ~L141; `UpdateRecord` ~L146–158; `loadUpdate` ~L162) and `UpdateRecord.cpp` (`Journal::readFrom` ~L213; `loadUpdate` ~L265–302 and its four `return`s), `src/update/Trial.h` (`record_` ~L335) and `Trial.cpp` (`record_ = loadUpdate(…)` ~L203, ~L452, ~L505; `record_ = UpdateRecord()` ~L190, ~L317, ~L482, ~L492, ~L550, ~L561), `src/update/Ask.h` (`AskRecord` ~L202–222; `loadAsk` ~L227; `record_` ~L346) and `Ask.cpp` (`loadAsk` ~L134–198; `Asked::atBoot` ~L324–326; `record_ = AskRecord()` ~L411), `src/main.cpp` (the objects ~L105–117; `printStatus` ~L418 and the `stack` lines it has ~L542–547; `setup` ~L1049: `updater.atBoot()` ~L1125, `asked.atBoot()` ~L1208; `loop` ~L1212), `src/net/MqttClient.cpp` (~L118–121: a task's low-water mark, as it is read today), `sdkconfig.heltec_v3` and `sdkconfig.heltec_v4` (~L165: `CONFIG_ARDUINO_LOOP_STACK_SIZE=8192`), `tools/check-sdkconfig.py` (the two files are the core's but for the keys it names), `platformio.ini` (`build_flags` ~L117; `build_src_flags` ~L150 and the paragraph above it: the library carries its own flags, and another firmware compiles it), `.github/workflows/ci.yml` (the build job ~L134–209), `tools/check-partitions.py` (a rail with a self-test); `test/test_trial`, `test/test_update_ask`; `docs/console.md` (`status` ~L22, ~L148), `docs/field.md` §9 (~L354), `docs/release.md` §2; `tasks/phase-1.15/FOLLOWUPS.md` (*0.7.3 does not boot*), `tasks/phase-1.15/reports/60.md` (~L163–168).
**Size:** one session.
**Model:** claude-opus-5

## Goal

0.7.3 does not start. Thomas flashed it onto his own board on 20 November, a Heltec V3, and it
restarted for ever:

```
[+0s] I clock: no time yet
Guru Meditation Error: Core  1 panic'ed (Unhandled debug exception).
Debug exception reason: Stack canary watchpoint triggered (loopTask)
Backtrace: 0x400570e5:0x3fce9a30 0x42020b4a:0x3fce9a40 0x4201f8e1:0x3fcea570 0x4201133e:0x3fcebbb0 0x4204aa1a:0x3fcebe50
```

Named from a build of the same commit: the ROM's `memset` of 2 764 bytes, in `loadUpdate`
(a frame of 2 864 bytes), in `Updater::atBoot` (5 696), in `setup` (672). That is 9 232 bytes
on a loop task that has 8 192.

An `UpdateRecord` is 2 764 bytes, and 2 521 of them are its journal. `loadUpdate` builds one
and returns it by value, and what it returns is assigned to the member that keeps it: three
records on the stack to fill one. `loadAsk` and `AskRecord` have the same shape and run next.
It happens on every board at every boot, before anything is written. The release was
withdrawn the same morning; no other board had taken it.

No test found it, because no test runs on a board's stack. So the cure comes with two things
that would have: a compiler that cannot copy a record, and a build that refuses a large frame.

## Deliverables

1. **A record is read into the object that keeps it.** Loading fills a record it is given, and
   emptying one is done in place. No function of ours returns an `UpdateRecord`, an `AskRecord`
   or a `Journal`, and none takes one by value.
2. **The compiler refuses a copy.** The three types can be neither copied nor moved.
3. **Nothing stored changes.** The keys and their values are what 0.7.3 wrote, and a record
   that is not believed today is not believed after: every way out of a load leaves the record
   it was given empty, whatever it held before.
4. **No frame of ours is over 1 024 bytes**, in `src/` and in `lib/agent-core`, on both boards,
   and the build says no when one is. A frame the compiler cannot bound is refused too. Say
   which of the compiler's means holds it and why. The library's `library.json` is left as it
   is: the Walter's firmware compiles the library with another compiler. A function outside
   `src/update/` that is over the limit today is listed in the report with its size; it is
   brought under where that is a small change, and otherwise named in the rail with today's
   size as its ceiling and a reason. `src/update/` has no such name.
5. **The loop task's stack is 16 384 bytes** on both boards. Everything the network does runs
   on that task, the TLS handshake and a release's signature among it, and nobody has measured
   what is left. Say in the report where the pinned core takes the number from.
   `tools/check-sdkconfig.py` knows the key and its reason.
6. **The board says what is left.** One line of the boot log when `setup()` ends, and a `stack`
   line in `status` beside the ones the observer's tasks have: the loop task's size and the
   least that was ever free. Both from one function.
7. **What the cure is measured by**, in the report, for both boards and from the compiler's own
   figures: the frames of `setup`, `Updater::atBoot`, `loadUpdate`, `Asked::atBoot` and
   `loadAsk` before and after, and the twenty largest frames of ours after.
8. **The words.** `docs/console.md` (the `stack` line). `docs/field.md` §9: what the boot's
   line means and what to do when the number is small. One line in
   `release-notes/unreleased/`: 0.7.3 did not start on any board and was withdrawn, and this
   release is what 0.7.3 announced.

## Acceptance criteria

- [ ] `UpdateRecord`, `AskRecord` and `Journal` are neither copy- nor move-constructible nor assignable (a `static_assert` on each in a native test).
- [ ] A load from an empty store, from a state this firmware did not write, from `done` with no ending and from a record in hand with no `to`, each into a record that held a full journal: the record is empty afterwards (tests in `test/test_trial`). The same for the three ways `loadAsk` gives up (tests in `test/test_update_ask`).
- [ ] A record saved and loaded again is the record that was saved, journal and all, and the store holds the bytes it held before this task for the same record (tests).
- [ ] Every test of `test/test_trial` and `test/test_update_ask` that was there stays, and passes.
- [ ] The rail refuses a function with a local of 2 000 bytes in `src/update/` and one in `lib/agent-core`, and a function whose frame the compiler cannot bound; it passes the tree as it is after this task (its self-test, and CI for both boards).
- [ ] After this task the five frames of deliverable 7 are each under the limit, and `setup`, `Updater::atBoot` and `loadUpdate` together are under 2 048 bytes (the report's table, both boards).
- [ ] `CONFIG_ARDUINO_LOOP_STACK_SIZE` is 16384 in the configuration both boards are built with, and `tools/check-sdkconfig.py --built` holds it for each (its self-test refuses 8192).
- [ ] The boot's line and `status`' line print the same two numbers from one function, through a seam a test scripts (test).
- [ ] `pio test -e native` green, both boards build, CI green.

## What only a board can show

Nothing here can start an image. For Thomas, with the cable in, before anybody else hears of
the release:

- ⚠ The plain `.bin` on a V3: the boot passes `clock:` and reaches the `stack` line. If it
  restarts instead, 0.7.2's plain `.bin` brings the board back.
- ⚠ `status` after ten minutes online, and again after an `update fetch`: the least ever free.
  Both numbers go into this pile's `FOLLOWUPS.md`.

## Out of scope

How long a journal is. Moving the network's work off the loop task. An image started by CI
before a release (`FOLLOWUPS.md`). The low-water mark in a heartbeat. Anything in the
contract with meshmaster.dk.
