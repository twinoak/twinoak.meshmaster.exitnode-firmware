# 64 — The way back

**Depends on:** —
**Spec:** `src/update/Trial.h` (the header ~L1–58: the trial, a reset, *a bootloader that does not undo*, *a flash over USB still wins*; `BootSlots` ~L118–150; `DeliberateRestart` ~L162; `BootFinding` ~L186) and `Trial.cpp` (`recordKnows` ~L156–162; `atBoot` ~L180–286: the first slot at boot ~L250–285; `rollBack` ~L366–384; `apply` ~L396; `whyTheOtherSlotMayNotBeWritten` ~L474; `report` ~L485), `src/update/UpdateRecord.h` (`Ended` ~L72), `src/update/Ask.cpp` (~L559: the trial's ending becomes the ask's outcome), `src/platform/BootChoice.cpp` (`describeOther`, `bootOther`, `markInvalidAndRestart`), `src/update/Descriptor.h`; `src/console/Parser.h` (~L95–97) and `Parser.cpp` (~L118–133), `src/main.cpp` (`printUpdate` ~L589, `printUpdateApply` ~L710); `test/test_trial`, `test/test_update_ask`, `test/test_console_parser`; `docs/console.md` (~L33–35), `docs/field.md` §9 (~L461–525: *Switching to it, and the ten minutes that decide*); `README.md` (*Flashing it*); `tasks/phase-1.15/FOLLOWUPS.md` (*The review of 56–61*, its third and fourth items).
**Size:** one session.
**Model:** claude-opus-5

## Goal

Two ways back do not lead back.

**A release before.** After an update from A to B the first slot still holds A. *A flash over
USB still wins* switches at boot to an image in the first slot that the record does not know,
and the record knows A: it is the version it switched from. So an owner who flashes A again
with the web flasher sees the flasher succeed and the board come up on B, and the boot says
nothing. The bytes in the slot are the same whether A was flashed a minute ago or has lain
there since the update, so the firmware cannot tell, and should not guess. A release older
than pile 1.15 has no descriptor and is passed over without a word. What is left today is a
merged image, and the board's identity goes with it.

**A trial that failed.** `rollBack()` calls the framework's rollback for a pending image. When
the framework refuses, the firmware journals *could not be sent back*, finishes its record as
`rolledBack`, and stays in the image that failed. A restart is what undoes a pending image: the
bootloader marks it aborted and chooses the other.

## Deliverables

1. **`update back`**, at the console: switch to what lies in the other slot, and restart into
   it. It is taken when the framework would load that image and it carries this product's
   descriptor for this board; it is then on trial like any other, and a trial it fails ends in
   the image the command was typed in. It is refused in a sentence when an update or a fetch is
   in hand, when a firmware is on trial, when the board has one slot, and when the other slot
   holds nothing it would load or something that is not this product for this board.
2. **`update` says what the other slot holds:** the version its descriptor names, or that it
   holds an image with no descriptor, or nothing that loads; and `update back` where that would
   be taken.
3. **The boot says why it did not switch.** When the board runs from the second slot and the
   first holds an image that loads and is not switched to, one line of the log says which it
   is: a version this board has run, with `update back`; an image with no descriptor; another
   product or another board. Once a boot.
4. **A trial that failed ends in a restart** when the framework refuses the rollback of a
   pending image. The boot after it finishes the record from what is running, as it does for a
   reset during a trial.
5. **`rolledBack` is said only of a board that runs the old image again.** A board that could
   not be sent back says so: its record, and through it the ask's result, end as something
   that is not `rolledBack`, with the version that runs. Say in the report which of the
   contract's four outcomes that is and why; no outcome is added.
6. **Nothing switches by itself that did not before.** The one switch at boot is the one task
   60 built.
7. **The words.** `docs/console.md`. `docs/field.md` §9: going back a release, and what the
   boot's line means. `README.md` where it says how to flash: flashing the release before does
   not bring it back on a board that has been updated from the server, and `update back` does.
   One line in `release-notes/unreleased/`.

## Acceptance criteria

The slots, the boot choice and the restart are the scripted ones of `test/test_trial`.

- [ ] Running B from the second slot with A in the first, A carrying this board's descriptor: `update back` sets the boot choice and restarts; the boot after it is A on trial; A proved is `committed` in the record with `from` B and `to` A (tests).
- [ ] That trial failed: the board is in B again, and the record says rolled back (test).
- [ ] `update back` with an image of another board, with one that has no descriptor, with a slot that does not load, on a board with one slot, during a trial, during a fetch and while an ask is in hand: seven refusals, each its own sentence, and nothing is switched (tests).
- [ ] `update` prints the other slot's version in the first case and each of the three other states of deliverable 2 (tests).
- [ ] A boot from the second slot with A in the first, A known to the record: no switch, and one log line that names A and `update back`. With an image that has no descriptor: no switch, and one line that says so. With an image the record does not know: the switch task 60 makes, as before (tests).
- [ ] A pending image whose trial fails and whose rollback call is refused: the board restarts, and nothing is finished as `rolledBack` before it has (test).
- [ ] The boot after it in the old image: `rolledBack`. The boot after it in the same image again, with a boot choice that cannot be set back: the ending of deliverable 5, and the ask's result carries the version that runs (tests, one in `test/test_update_ask`).
- [ ] `update back` parses, and `update backwards` is refused as `update` words are (test in `test/test_console_parser`).
- [ ] The one call that changes the boot choice is still in one file (`test/test_flash_writes`).
- [ ] `pio test -e native` green, both boards build, CI green.

## Out of scope

Going back from meshmaster.dk's page: whether its door takes an older release is Thomas's to
decide (`FOLLOWUPS.md`). A floor on the board under which no release is taken. Knowing that a
slot was flashed over USB: its bytes do not say. An image with no descriptor: it is not
switched to by any command.
