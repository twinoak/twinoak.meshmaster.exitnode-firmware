# Pile 1.17 — follow-ups

Review findings, tagged with the task that must pick them up. Only the reviewer ticks an item.

- [ ] **[Thomas to decide] Nothing starts an image before it is released.** 0.7.3 was built,
  tested, signed and published as *Latest* without ever having run. Task 65 holds two causes of
  this kind (a record copied, a large frame); a deep chain of small frames it does not see. Two
  ways, not exclusive: CI starts the image in Espressif's QEMU for the S3, as the Walter's
  firmware does for its own (not tried here: whether this image gets past its radio there is
  not known); or a release is a pre-release until one board has run it.
- [ ] **[later] The least stack ever free is read at the console only.** The heartbeat's
  counters are counts that rise, and this number falls, so it does not belong among them.
- [ ] **[later] What the loop task really uses.** The numbers Thomas reads off his board after
  task 65 go here: after ten minutes online, and after a fetch. They say whether 16 384 is
  generous or needed.

## The review of 65 (20 November)

Merged at 10:15 CET (#72, three checks green), nine of nine criteria, no deviation of
substance. Read: the report's deviations, its measured table and what it could not verify; the
change in `UpdateRecord.h` and `.cpp`, `Trial.cpp`, `Ask.cpp`, `Release.cpp`, `main.cpp`,
`LoopStack.cpp` and `sdkconfig.defaults`; the rail's limit and its names. Not read: the tests,
the rest of `tools/check-frames.py`, the words. Nothing found. It has not run on a board.

- [ ] **[none] The cure is the one asked for.** A load fills the member and every way out of it
  leaves the record empty (`UpdateRecord.cpp`, `Ask.cpp`); the nine assignments of a fresh
  record are `clear()`; `Journal`, `UpdateRecord` and `AskRecord` can be neither copied nor
  moved. By the compiler's figures the three frames of the panic went from 9 232 bytes to
  1 008, and the *before* build gives the panic's own 672, 5 696 and 2 864.
- [ ] **[Thomas, at the flash] The boot's `stack` line proves both halves**: that the board
  starts, and that the loop task has 16 384 bytes (`sdkconfig.defaults`; the line prints what
  `getArduinoLoopTaskStackSize()` says). The least ever free, after ten minutes and after a
  fetch, goes into the item above.
- [ ] **[none] `readRelease` fills the caller's `ReleaseInfo` as it reads**, where it filled a
  local and copied it at the end (1 616 bytes to 896). Checked: it is zeroed first, no refusal
  before the fill touches it, and the two refusals after the fill zero it again, so a refused
  release leaves nothing behind (`Release.cpp` ~L344–447; one caller, `Fetch.cpp` ~L331).
- [ ] **[none, first seen at a fetch] `findOneDescriptor` reads a slot in windows of 512 bytes,**
  was 1 024, to come under the limit. Held by tests against the fixture images on a PC.
- [ ] **[later] Two frames keep a ceiling of their own in the rail:**
  `Observer::reportNeighbours` (1 888) and `observer::makeToken` (1 296). Both were in 0.7.2.
- [ ] **[none] `begin()` of the updater and of the ask now put the per-boot state back.** It is
  called once on a board; the tests call it again on one object.
- [ ] **[next, small] The loop task's `stack` line is in `get mqtt.status`, and the words say
  `status`.** Found on 20 November, when Thomas typed `status` on Aarhus C (0.7.4) and it had no
  such line. The task was wrong about where the observer's `stack` lines are: it said
  `printStatus`, and they are in `printMqttStatus` (`main.cpp` ~L533–577). The session put the
  new line beside them (~L567), as the task also said, and wrote *in `status`* in
  `docs/console.md` (~L149) and `docs/field.md`. Either `status` prints the line too, or the
  words name the command it is in. The boot's line is right.
- [ ] **[none] The first reading (Aarhus C, `heltec_v3`, 0.7.4+a3bc485, 20 November):**
  `stack     loop task: 16384 bytes, the least ever free 10112`, some minutes after a power-on,
  both observer slots connected and five calls to the server answered. So the loop task had
  used 6 272 bytes at most. On the 8 192 bytes it had until 0.7.2 that would have left 1 920.
  Not in it yet: a fetch, a release's signature, a switch.
- [ ] **[later] The observer's two tasks have about 1 550 bytes never used, of 6 144 each**
  (`slot 1 task: 6144 bytes, the least ever free 1560`, slot 2: 1552; the same reading). They
  are not this pile's and not new; nobody had read them on a board that was told of here.
  `docs/console.md` ~L157 shows 8 192 in its sample.
- [ ] **[none] The first slot added on a board (Aarhus C, `heltec_v3`, 0.7.4, 20 November).**
  `update slot` at 10:59:33: *added: app1 at 0x350000, 0x320000 bytes*, ending `added`. After
  `reboot`, `update` says `table two`, both slots at their designed places, and *app1 holds
  nothing this firmware would boot*. meshmaster.dk has the same exit node heartbeating: the
  same companion key, the token taken, 0.7.4, *It can be updated from here*. That is step 1 of
  pile 1.16's bench, on a V3. Not done: a V4; a fetch, a switch, a trial, `update back`; an ask
  from the server to a board that still says `one`.

## A night Risskov did not hear (19 to 20 November)

- [ ] **[next, Thomas to decide] The exit node never resets its radio's receiver.** MeshCore's
  dispatcher resets the AGC every `getAGCResetInterval()` and that is 0, *disabled by default*
  (`Dispatcher.h` ~L172, `Dispatcher.cpp` ~L130–135 at this firmware's pin); upstream's
  companion and repeater turn it on from a setting (`examples/companion_radio/MyMesh.h` ~L133),
  and `AgentMesh` does not override it. `resetAGC()` is a warm sleep of the radio *to reset the
  analog frontend* (`RadioLibWrappers.cpp` ~L70–78).
  What was seen on meshmaster.dk, 0.7.2 on Risskov (FD9B): from about 18:00 to 20:00 UTC on
  19 November until its restart at about 07:00 UTC on the 20th, most reads through it got
  *no reply (direct)*: of the hourly `status` reads between 19:00 and 07:00 UTC, Temp1 failed
  10 of 12, Temp2 7 of 13 and Ebeltoft 11 of 12, and its first try at TwinOak Risskov, beside
  it, failed in every hour from 20:00 to 06:00 (the Walter took ten of those, one expired).
  AAR_LH got through on its retries. The nights of 17 and 18 November were clean, and
  Aarhus C, same firmware, was clean that night. What the board sent arrived as before: Temp1
  heard it at -96 dBm and SNR 11.75 at 01:00 and 06:00 UTC, as by day. TwinOak Risskov's own
  figures were ordinary (1 000 to 1 300 packets an hour, noise floor -116 to -120). Every hour
  since the restart is clean. Temp1 and Temp2 stood beside the board until that afternoon and
  evening (it was heard at -41 and -53 dBm) and were moved at about 15:30 and 19:15 UTC.
  So: a receiver that stopped hearing and a restart that cured it. That the AGC was the cause
  is not shown; nothing the server keeps says what the board's receiver did. A reset on a
  timer, as upstream has it, costs a moment of deafness each time and would have ended this
  within one interval.
