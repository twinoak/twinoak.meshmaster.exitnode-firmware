# Pile 1.15 — follow-ups

Review findings, tagged with the task that must pick them up. Only the reviewer ticks an item.

- [x] **[next pile] The fixtures of tasks 56 and 61 wait in `tests/fixtures/contracts-ahead/`.**
  Six files, written from the lines in the tasks: `NeighboursResultTotal.json`,
  `HeartbeatUpdate.json`, `UpdateAsk.json`, `UpdateResult.json`, `AgentHeartbeatUpdate.json`,
  `HeartbeatReplyUpdate.json`. Once meshmaster.dk's tasks 239 and 244 are on its master:
  `tools/sync-fixtures.ps1 -Commit master`, compare each with the copied file byte for byte,
  point the tests at the copied tree and remove the waiting ones. A difference is a wire
  that does not match and is found before any release.
  *Done in pile 1.16, task 62 (19 November):* the six are read from the copied tree, the waiting
  directory holds its README alone, and `master` is green again (#68).
- [ ] **[later] Neither half has read a release the other made.** Task 58 tests against the
  specification's fixtures and a release of its own tooling; meshmaster.dk's task 243 tests
  against a release of its own making. The first real release imported there is the first
  meeting, and the import says which check fails if one does.
- [ ] **[Thomas] `FIRMWARE-MANIFEST.md` does not know this product.** The product
  `twinoak-exitnode-firmware`, the role `exit-node` and its descriptor byte 32 were chosen
  on 18 November in this pile and in meshmaster.dk's tasks 243 to 245. The specification is
  the hardware repository's.
- [ ] **[later] A board whose table write was cut by the power needs a merged image**, and
  the web flasher erases the board to write one. `esptool` writes it and keeps the key, the
  token and the WiFi. A page that flashes a merged image without erasing would spare an
  owner who has no `esptool`.
- [ ] **[later] The bootloader is never updated.** A board keeps the one its first merged
  flash gave it. One that came from other firmware and was upgraded with a plain `.bin` may
  have a bootloader that does not undo a pending image; task 60 then undoes it from the
  application, which a firmware that does not run cannot do.
- [ ] **[later] A board on the framework's default table stays on it.** It has two slots and
  is updated like the others; its settings are still where a merged image reaches them
  (`README.md`, *The one exception*).
- [ ] **[later] The application must now fit the smaller slot**: 3.1 MB on the V3, where
  `app0` alone is 3.2 MB. It is 1.43 MB today.
- [ ] **[later] Ten minutes of trial and three tries of a file are constants.**
- [ ] **[later] A fetch does not go on over a restart.** One that a restart interrupts
  begins again; over WiFi a file is seconds.
- [ ] **[later] What waited in RAM for the server is posted once before a restart**, and lost
  if that one try fails.
- [ ] **[later] Flash encryption and secure boot** are the owner's choice in `README.md`. A
  board with either would refuse the table's write and perhaps the update; nothing here
  looked at such a board.
- [ ] **[later] A page that goes unanswered is not asked for again** (task 56). Each page has
  the tries every request has, and when they are spent the list ends where it is. A list of
  three pages has three chances to stop, where a status has one. Asking the same page once
  more before giving the list up would cost a transaction of the airtime budget and the
  lease's time; whether it is worth that is for the figures meshmaster.dk will now have: how
  many listings are short, and of which repeaters.
- [ ] **[later] The list is paged by position while it is sorted by who was heard last**
  (task 56). A neighbour heard between two pages moves to the front: one entry is then on
  both pages and the neighbour itself on neither. The result has as many entries as the
  repeater's count and is still not the whole list. meshmaster.dk's task 239 counts different
  neighbours for that reason. Whether another `order_by` (`runNeighbours`, the request's
  fifth byte) holds still during a read is for the repeater firmware's source to say.
- [x] **[later] Piles 1.9 to 1.13 are not reviewed.**
  *Reviewed lightly on 19 November*, by their reports and what is in service as v0.7.2: pile
  1.16's follow-ups.

## The review of 56–61 (19 November)

All six merged with CI green (#62 to #67). Nothing of the pile is released (`VERSION` is 0.7.2
and the notes pull request of v0.7.2, #61, is open), and nothing of it has run on a board: no
test here can write a table, boot a second slot or pull the power, so every bench item of 57,
59, 60 and 61 in `docs/field.md` stands. The six fixtures in `tests/fixtures/contracts-ahead/`
are byte for byte meshmaster.dk's master at b2b87cc, which has its 239 and 244.

Read line by line: `src/platform/TableFlash.cpp`, `PartitionTable.cpp`, `OtaSlot.cpp` and
`BootChoice.cpp`; the two tables against the ones before them; check 1 of
`src/update/Release.cpp`; how the trust table is made (`tools/pio/release_headers.py`,
`tools/gen-trust-table.py`); the signing steps of `release.yml`; the ask from `check` through
`adding-slot` (`Ask.cpp`); the trial's ways back (`Trial.cpp`). Read by their headers and their
reports only: `Fetch.cpp`, the rest of `Release.cpp`, `UpdateRecord.cpp`, the Python tooling and
the tests.

What was read is sound, and careful where this pile can break a board: one file writes raw flash
and bounds every call to the one sector, one file writes a slot, one sets the boot choice; the
core's `verifyRollbackLater` is answered; a fetch is refused while a firmware is on trial; the
new tables add `app1` and move nothing; a board build's trust table holds the one release key,
and the release's gate looks for the test key in the images' bytes. The first two findings
should be settled before a release of this pile reaches a board that is not on a desk.

- [x] **[next pile, before the first release of this pile] 57, 61: a write of the table that
  fails is not tried again, and the board then runs on with no table until its next reset.**
  `addSecondSlot` (`PartitionTable.cpp` ~L104–170) erases the sector and writes the new table
  once. When the erase (~L125), the write (~L131) or the read-back fails, it returns a sentence
  that says to flash a merged image over USB *before rebooting*. At the console somebody reads
  it. Asked for by the server (`Ask.cpp` ~L449–461) nobody does: the ask ends `stuck`, the
  sector reads `other`, and the next watchdog reset or power cut leaves a board that does not
  boot and needs a merged image, which erases its key, its token and its WiFi. While the
  firmware still runs it holds both tables byte for byte and can still write. So: write the new
  table again, a few times; when it cannot be written, put the old one back and read that back;
  and only when neither holds say what is true then, that the sector is no table and the board
  must not lose power. Tests with a scripted sector that fails its first write, and one that
  fails every write of the new table.
  *Done in pile 1.16, task 63 (19 November):* `addSecondSlot` tries the new table three times,
  puts the old one back as many times and ends in one of three words; with no table
  `TableKeeper` puts the old one back once a minute, and `update` and `status` say not to cut
  the power (#69). Read: `PartitionTable.cpp` ~L38–57 and ~L150–227.
- [ ] **[Thomas, before a board that is not on a desk is asked] 57, 61: the first update of a
  board writes its partition table with nobody there.** As decided on 18 November: the last
  flash over USB is the plain `.bin`, and the firmware adds the slot. In the field that means a
  board whose table is `one` does `adding-slot` at its first ask, and a power cut in that
  moment, one sector erased and written, needs USB and a merged image. That cannot be designed
  away. The order of things can: `update slot` on a V3 and on a V4 at the desk first
  (`docs/field.md` ~L394–415: `two` after the reboot, the settings and the identity as they
  were), then one ask from meshmaster.dk to a `one` board at the desk, and only then a release
  anybody else is told about. Nothing has run this function on hardware, and
  `CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED` has been built and never exercised. meshmaster.dk's
  confirm page says in its Help that the board *adds a second application slot of its own*, and
  nothing about that moment (its pile 4.42's follow-ups).
- [x] **[next pile] 60: after an update from A to B, flashing A over USB does nothing, and
  nothing says so.** *A flash over USB still wins* looks at the first slot at boot when the
  board runs from the second, and switches to an image there that the record does not know
  (`Trial.cpp` ~L263–285; `recordKnows` ~L156–162). After an update the record's `from` is A,
  and A is what lies in the first slot whether somebody flashed it a minute ago or it has lain
  there since the update: the bytes are the same. So an owner who wants the release before
  back, which is the one a person reaches for, flashes it, sees the flasher succeed, and the
  board comes up on B. A release older than this pile fares the same and more quietly: it has
  no descriptor, and the boot says nothing at all (~L263, a count of 0). The way back today is
  a merged image, and the identity goes with it. Wanted: a console command that switches to
  what lies in the other slot when the framework would load it and it is this product for this
  board, on trial like any other; and a sentence at boot when the first slot holds something
  that is not switched to.
  *Done in pile 1.16, task 64 (19 November):* `update back` (`Trial.cpp`, `goBack` ~L504), and
  the boot says why it did not switch (~L310) (#70).
- [x] **[next pile, small] 60: `rollBack()` stays in the image that failed when the framework
  refuses the rollback call.** For a pending image it calls `markInvalidAndRestart()`; when that
  returns (`ESP_ERR_OTA_ROLLBACK_FAILED`) it journals *could not be sent back* and finishes the
  record as `rolledBack` (`Trial.cpp` ~L366–383), with the failed image still running and still
  pending. A plain restart is what the header of `Trial.h` says undoes a pending image: the
  bootloader marks it aborted and chooses the other. So restart there. And a result should not
  say `rolledBack` of a board that is not back.
  *Done in pile 1.16, task 64:* a refused rollback of a pending image restarts (`rollBack`
  ~L408–439), and a board that cannot be sent back ends `stuck` and not `rolledBack`.
- [ ] **[Thomas, at the bench] Which bootloader a board in the field has is not known here.** A
  board upgraded with the plain `.bin` keeps the bootloader of its last merged image. `Trial.h`
  covers one built without the rollback (*this bootloader leaves it …, not pending: this
  firmware undoes it*), except for an image that crashes before it has counted its ten minutes:
  nothing undoes that one. The journal of the first trial on a board says which it has; read it
  on one board of each generation in service before the others are asked.
- [x] **[later] `docs/field.md` ~L204–206 says a `one` board is refused with `table`, and in the
  same sentence that the firmware adds the slot at the first ask.** The second is what `Ask.cpp`
  does; `table` is the refusal of `other` alone.
  *Done in pile 1.16, task 63:* `docs/field.md` ~L204 says `table` is the refusal of `other`
  alone.
- [ ] **[Thomas to decide] Nothing on the board refuses an older release.** It refuses `current`
  and takes any other version that is signed; that a release is newer is meshmaster.dk's rule.
  A server that asked for an old signed release would be obeyed. Going back from the page may be
  what is wanted (the third item); a floor on the board would be a decision.
- [ ] **[later] MeshCore's `ed25519_verify` checks three bits of S and not S < L**
  (`lib/ed25519/verify.c` ~L54 at the pin). It takes a signature RFC 8032 refuses, made from a
  valid one without the key; no other manifest verifies by it. meshmaster.dk's verifier may be
  stricter, so the two could disagree about such a line. Nothing to do unless the
  specification's fixtures gain one.
- [ ] **[Thomas] The first release of this pile.** Its version is `VERSION`'s, 0.7.2 today with
  seven notes waiting; 0.8.0 in the tasks and on meshmaster.dk's pages is an example. Before it:
  the notes pull request #61, and the `RELEASE_SIGNING_KEY` secret in this repository (64 hex
  characters, read as the Walter's workflow reads it; `keys/release-keys.txt` names the one
  release key of the three firmwares). The remote branch `task/p1.15-61-the-server-asks` is
  still there: the driver could not delete it.

## 0.7.3 does not boot (20 November)

- [ ] **[next pile, first] 60 and 61: the update's record is passed by value, and three of them
  do not fit the loop task's stack.** Thomas flashed 0.7.3+b6d4cfb over USB onto his own board
  (FD9B, `heltec_v3`) on 20 November. It restarts for ever: *Stack canary watchpoint triggered
  (loopTask)*, straight after `clock: no time yet`. 0.7.2's plain `.bin` brings it back.
  The backtrace, named from this clone's `.pio/build/heltec_v3/firmware.elf` (task 64's build,
  not the release's own): the ROM's `memset` of 2 764 bytes, in `loadUpdate` (a frame of 2 864
  bytes), in `Updater::atBoot` (5 696), in `setup` (672). That is 9 232 bytes on a stack of
  8 192 (`CONFIG_ARDUINO_LOOP_STACK_SIZE`, `sdkconfig.heltec_v3` ~L165; the V4's is the same).
  `UpdateRecord` is 2 764 bytes, 2 521 of them its `Journal` (`UpdateRecord.h` ~L141–156).
  `loadUpdate` builds one and returns it by value through four `return`s (`UpdateRecord.cpp`
  ~L265–301), and its callers assign the result to a member (`Trial.cpp` ~L203, ~L452, ~L505).
  `loadAsk` and `AskRecord` have the same shape (`Ask.cpp` ~L134, ~L326) and run next in
  `setup`. Every board, at every boot, before anything is written.
  Report 60 says of the journal *nothing of this is on the stack*: true of the buffer it reads
  into, not of the record around it. The review of 56–61 did not look at stack use. No test
  boots an image: the native tests run on a PC's stack.
  Wanted with the cure: a build that refuses a large frame in `src/` (`build_src_flags` is
  `-Werror` already, `platformio.ini` ~L150), and the stack's low-water mark where a person
  can read it.
