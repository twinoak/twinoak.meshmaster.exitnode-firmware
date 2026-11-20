# 67 — The stack line, where the words say

**Depends on:** —
**Spec:** `src/main.cpp` (`printStatus` ~L440–480; `printMqttStatus` ~L533–580: the loop task's line ~L567 and the observer's tasks' lines ~L574–577; the boot's line ~L1249), `src/platform/LoopStack.h` and `LoopStack.cpp` (the one function that forms the line), `test/test_loop_stack`; `docs/console.md` (*The `stack` lines, at boot and in `status`* ~L149–175: its sample shows 8 192 bytes for a slot's task), `docs/field.md` (*The `stack` line, and what a small number means* ~L388); `tasks/phase-1.17/FOLLOWUPS.md` (*The loop task's `stack` line is in `get mqtt.status`*, and the first reading).
**Size:** one session, a short one.
**Model:** claude-opus-5

## Goal

Task 65 gave the loop task's stack a line and the words say it is in `status`. It is in
`get mqtt.status`. The task was wrong about where the observer's own `stack` lines are, the
line went beside them, and the two documents were written from the task. Thomas typed `status`
on 20 November and the line was not there.

## Deliverables

1. **`status` prints the loop task's `stack` line**, from the one function that forms it.
2. **`get mqtt.status` keeps what it prints today**, the loop task's line among it.
3. **The words say where each line is**, and the sample in `docs/console.md` shows a slot's
   task with the size it has on a board, which was 6 144 bytes on 20 November and not 8 192.
   Say in the report where that number comes from.
4. **No line in `release-notes/unreleased/`** unless a person with a board would do something
   differently: say which in the report.

## Acceptance criteria

- [ ] `status`' text has the line `stack     loop task: …` with the two numbers the boot's line has, from the same function (test, by whatever reaches `status`' text; a test that reads the source if nothing does).
- [ ] `get mqtt.status` prints the three `stack` lines it printed before (test, the same way).
- [ ] `docs/console.md` and `docs/field.md` name the command each line is in, and the sample's sizes are a board's.
- [ ] `pio test -e native` green, both boards build, the frame rail green, CI green.

## Out of scope

The observer's tasks' own margin (`FOLLOWUPS.md` of pile 1.17). The stack in a heartbeat.
