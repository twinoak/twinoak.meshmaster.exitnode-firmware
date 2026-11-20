# Pile 1.16 — follow-ups

Review findings, tagged with the task that must pick them up. Only the reviewer ticks an item.

- [ ] **[Thomas to decide] Going back from meshmaster.dk's page.** Its door takes any served
  release that is not the one running, and its page offers only a newer one (its pile 4.42's
  follow-ups, *The review of 243–245*). Nothing on the board refuses an older release. Task 64
  gives the console a way back and leaves the page alone: either the door holds the page's
  rule, or going back a release is offered there on purpose.
- [ ] **[later] An image with no descriptor is never switched to**, by `update back` or at boot.
  A release older than pile 1.15 has none, so the only way to one of those from a board that
  boots from its second slot is the merged image. Nobody has asked for that way.
- [ ] **[later] The items of pile 1.15's review that are not built here**: which bootloader the
  boards in the field have (the bench reads it, above), and MeshCore's `ed25519_verify`
  checking three bits of S. Both stand in pile 1.15's `FOLLOWUPS.md`.

## The review of 62–64 (19 November)

The three merged with CI green (#68 to #70) between 17:01 and 19:07, with no deviation in any
report. `VERSION` is 0.7.3, in no release yet. Read: the three reports; `PartitionTable.cpp`
(`writeTheTable` ~L38–57, `addSecondSlot` ~L150–227); `Trial.cpp`'s `rollBack` (~L408–439) and
where `update back` and the boot's sentence are (`goBack` ~L504, ~L310); the waiting directory,
which holds its README alone. Not read: `TableKeeper`'s turn in `main.cpp`, the refusals of
`goBack`, the tests. They do what the review of 56–61 asked, and its items are ticked in pile
1.15's follow-ups.

- [ ] **[Thomas] None of piles 1.15 and 1.16 has run on a board.** There is no bench now, so
  the first board is his own, with the cable in: the release as the plain `.bin`, `update`,
  `update slot`, `reboot`, `update` again (the word is `two`, and the token and the identity
  are as they were). An update asked for from meshmaster.dk needs a second release to go to,
  so that half waits for the release after this one.
- [ ] **[none] Piles 1.9 to 1.13, lightly**: every report's deviations and open criteria (36 to
  55), against what has been in service since v0.7.0 and v0.7.2. No open criterion, and nothing
  found that asks for a task. No code was read for them.
