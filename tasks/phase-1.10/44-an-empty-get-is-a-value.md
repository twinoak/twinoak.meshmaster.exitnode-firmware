# 44 — An empty `get` is a value

**Depends on:** nothing.
**Spec:** `FOLLOWUPS.md`, the item *An empty `get` reply is a value, not a raw line* (raised by meshmaster.dk task p4.23-165); `lib/agent-core/src/ConfigRead.cpp` (`cliReplyValue`), `lib/agent-core/src/JobRunner.cpp` (the `trimInPlace` before it); MeshCore's `CommonCLI::handleGetCmd` at the pin (`"> %s"` for every key); `test/test_config_read`. Read only: meshmaster.dk's `src/MeshMaster.Agent` `CliReplies.ValueOf` (the .NET agent's fix of the same line).
**Size:** one session, short.
**Model:** claude-opus-5

## Goal

A key whose value is the empty string answers `"> "`, which trims to `">"`. `cliReplyValue` wants
`'>'` and a space, so the line is kept as a raw line and the key is left out of the snapshot.
meshmaster.dk reads an **empty** `guest.password` as *this repeater has no guest password* and
clears the stored one. Through this firmware that answer never arrives as a value, so a guest
password taken off a repeater stays stored on the server until somebody clears it by hand.

## Deliverables

1. A bare `>` is the empty value, as the .NET agent reads it. Nothing else about reply parsing moves.
2. `test_config_read`: `"> "`, `">"` and `">  "` each give an empty value; a value with spaces inside
   keeps them; a line that is not a `get` reply is still raw.
3. The `FOLLOWUPS.md` item ticked, with this task's report named.

## Acceptance criteria

- [ ] An empty `get` reply is the empty value (tests); nothing else of reply parsing changed; the host suite green.

## Out of scope

meshmaster.dk (its side was fixed in p4.23-165).
