# 63 — A table that could not be written

**Depends on:** —
**Spec:** `src/platform/PartitionTable.h` (the three words; `AddSlotResult` ~L70; `addSecondSlot` ~L78) and `PartitionTable.cpp` (`readTable` ~L47–102; `addSecondSlot` ~L104–170: the erase ~L125, the write ~L131, the read-back), `TableFlash.h` and `TableFlash.cpp` (the one file that erases or writes raw flash), `TableBytes.h`; `src/update/Ask.h` (`AskTable` ~L161–170) and `Ask.cpp` (`decide` ~L429–466: `adding-slot` ~L449–461; `afterTheSlot` ~L469–476), `src/main.cpp` (`AskTableNow` ~L215–232; `printUpdate` ~L589; `printUpdateSlot` ~L736), `src/update/Trial.h` (`DeliberateRestart` ~L162); `test/test_partition_table`, `test/test_update_ask`, `test/test_flash_writes`; `docs/field.md` §5 (~L196–206: the endings, and what a `one` board does) and §9 (~L380–415: *the one command that writes flash*), `docs/console.md` (~L33); `tasks/phase-1.15/FOLLOWUPS.md` (*The review of 56–61*, its first and sixth items); meshmaster.dk's `docs/dev/exit-node-updates.md` (the result's outcomes: nothing in the contract changes).
**Size:** one session.
**Model:** claude-opus-5

## Goal

Adding the second slot is one sector erased and one table written. `addSecondSlot` does each
once. When the erase, the write or the read-back fails it returns a sentence: *flash the merged
image of a release over USB before rebooting*.

At the console somebody reads that sentence. Asked for by the server nobody does. The ask ends
`stuck`, the sector reads `other`, and the board runs on with no partition table until its next
reset. After that it does not boot, and the merged image that brings it back erases its key,
its token and its WiFi.

While the firmware still runs it can still write, and it holds both tables byte for byte. A
board should not be left without a table while there is anything it can still do about it.

## Deliverables

1. **The new table is tried again.** Erase, write and read back, up to three times, before
   anything is given up.
2. **Then the old one is put back**, the same way and as many times, and read back. A board
   whose table is the one it had is a board that boots.
3. **Three endings, and each says which it is:** the slot was added; it was not, and the table
   is the one the board had; it was not, and the sector holds no table. The sentence of the
   third says what is true: do not cut the power, and what to flash. `update slot` prints the
   one that happened, and how many tries it took when more than one.
4. **While the sector holds no table the firmware keeps trying**, the old table once a minute
   for as long as it runs, and the first that holds ends it. `update` and `status` say in their
   first lines that the board must not lose power, for as long as that is so.
5. **The ask.** An `adding-slot` that did not add the slot ends `stuck`, as it does today, and
   its journal says which of the two it was. The board restarts only when the slot was added.
   Nothing of the contract with meshmaster.dk changes.
6. **Still one file that writes raw flash**, and every call inside the one sector.
7. **The words.** `docs/field.md`: the three endings where `update slot` is walked, what to do
   at each, and the sentence of §5 that says a `one` board is refused with `table` and, in the
   same breath, that the firmware adds the slot at the first ask. The second is what the
   firmware does: `table` is the refusal of `other` alone. `docs/console.md`. One line in
   `release-notes/unreleased/`.

## Acceptance criteria

The sector is the scripted one of `test/test_partition_table`.

- [ ] A sector whose first write fails and whose second holds: added, the table reads `two`, and the sentence says two tries (test).
- [ ] A sector that reads back one wrong byte once: the same (test).
- [ ] A sector that takes no write of the new table and takes the old: not added, the table reads `one` byte for byte as before, and the sentence says the board is as it was (test).
- [ ] A sector that takes no write at all: not added, the third sentence, and no more than six erases were asked for (test).
- [ ] That board a minute later, with a sector that now takes a write: the table reads `one`, and `update` no longer says to keep the power on (test).
- [ ] An erase that fails with the table untouched: nothing is written over it, and the ending is the second (test).
- [ ] An ask on a `one` board whose sector takes no write of the new table: `stuck`, a journal entry that says the table is as it was, no restart, and the next ask is taken (test in `test/test_update_ask`).
- [ ] The same with a sector that takes nothing: `stuck`, a journal entry that says the sector holds no table, no restart (test).
- [ ] An ask on a healthy `one` board is what it was: `adding-slot`, the restart, `fetching` (the tests of task 61 stay).
- [ ] `test/test_flash_writes` still finds one file that erases or writes raw flash.
- [ ] `pio test -e native` green, both boards build, CI green.

## Out of scope

A power cut between the erase and the write: nothing in a firmware survives that one. A copy
of the table kept somewhere else in the flash. The bootloader. A table this firmware does not
know. Any change to what meshmaster.dk is told.
