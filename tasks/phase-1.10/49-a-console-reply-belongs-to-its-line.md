# 49 — A console reply belongs to its line

**Depends on:** 47.
**Spec:** this repository: `src/mesh/AgentMesh.cpp` (`sendCli` ~L419, `takeText` ~L522: *the first text of CLI data from the peer while one is awaited is the answer*), `lib/agent-core/src/JobRunner.cpp` (`cliLine` ~L465, `runConfigRead` ~L1025, the apply), `lib/agent-core/include/agent/IMesh.h`, `lib/agent-core/src/ReplyCodec.cpp` (`parseCliReply`), `lib/agent-core/include/agent/CliReplies.h`, `test/support/FakeMesh.h`, `test/test_config_read`, `test/test_config_apply`; MeshCore at the pin: `examples/simple_repeater/MyMesh.cpp`, `handleCommand` (*optional prefix (for companion radio CLI)*: a line whose third character is `|` has its first three characters reflected at the head of the reply).
**Size:** one session.
**Model:** claude-opus-5

## Goal

A console reply carries no tag. While one is awaited, the first console text from that repeater is
taken as the answer. Replies arrive late on this mesh (47, and meshmaster.dk's task 187 has the
numbers), so the reply to `get name` can arrive while `get radio` is awaited and be stored as the
radio. A configuration read is thirty such lines in a row.

Nobody has seen a swapped value. Nothing would show one either: a `get` answers a value and not the
key it belongs to.

The firmware has a tag for this. `handleCommand` reflects a two-character prefix and a `|`:
`a7|get name` is answered `a7|> …`.

## Deliverables

1. **Find out where the prefix can be used.** From the pinned MeshCore source and its history, say
   which repeater firmware reflects it and since when, and what one that does not makes of
   `a7|get name`. Put the answer in the report before any code.
2. **Where it can be used, every console line carries one and only a reply that starts with it is
   the answer.** A reply with another prefix, or with none, is a late one: dropped with a line in
   the job's log, and the wait goes on. The prefix changes with every line and every try, so a late
   reply to try 1 is not the answer to try 2 of a line that is not safe to run twice.
3. **Where it cannot be told that the repeater reflects it, the line goes as it does today.** If the
   login's firmware level is what tells the two apart, use that and nothing looser. If nothing does,
   say so, stop at deliverable 1, and leave a line in FOLLOWUPS with what would.
4. **A password never leaves differently.** `set guest.password` and `password` lines are sensitive
   in the log today and stay so, prefix or not.
5. **The words.** `docs/library.md`, `CHANGELOG.md`.

## Acceptance criteria

- [ ] The report's answer to deliverable 1, with the source lines it rests on.
- [ ] A late reply to the line before is dropped and the right one is taken (test, config read). Without the fix the test stores the wrong value.
- [ ] A repeater that does not reflect the prefix is read as today (test).
- [ ] The apply, the `setperm` lines and the password lines carry the prefix under the same rule (tests).
- [ ] `tools\verify.ps1` green. CI green.
