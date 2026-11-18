# Pile 1.15 — follow-ups

Review findings, tagged with the task that must pick them up. Only the reviewer ticks an item.

- [ ] **[next pile] The fixtures of tasks 56 and 61 wait in `tests/fixtures/contracts-ahead/`.**
  Six files, written from the lines in the tasks: `NeighboursResultTotal.json`,
  `HeartbeatUpdate.json`, `UpdateAsk.json`, `UpdateResult.json`, `AgentHeartbeatUpdate.json`,
  `HeartbeatReplyUpdate.json`. Once meshmaster.dk's tasks 239 and 244 are on its master:
  `tools/sync-fixtures.ps1 -Commit master`, compare each with the copied file byte for byte,
  point the tests at the copied tree and remove the waiting ones. A difference is a wire
  that does not match and is found before any release.
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
- [ ] **[later] Piles 1.9 to 1.13 are not reviewed.**
