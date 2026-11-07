# Pile 1.10 — follow-ups

Review findings, tagged with the task that must pick them up: `[NN]` for a task
of this pile. A session reads this file before it starts (RUNBOOK.md section 0)
and treats every open item tagged for its task as part of the task. Only the
reviewer ticks an item.

- [ ] **An empty `get` reply is a value, not a raw line.** `cliReplyValue`
  (`lib/agent-core/src/ConfigRead.cpp`) wants `'>'` *and a space*, and
  `JobRunner` calls `trimInPlace` on the reply first, so a key whose value is
  the empty string - `handleGetCmd` prints one `sprintf(reply, "> %s", ...)`
  for every key, which is `"> "` and trims to `">"` - is kept as a raw line and
  left out of the snapshot. It matters now: meshmaster.dk's task p4.23-165 reads
  `guest.password` with the configuration and reads an **empty** answer as *this
  repeater has no guest password*, which clears the stored one. Through an
  onboard exit node that answer never arrives as a value, so a guest password
  taken off a repeater stays stored on the server until somebody clears it by
  hand. Nothing is stored wrongly - absent is read as "not known", never as
  "none" - so this is a gap and not a fault. The .NET agent was fixed in the
  same task (`CliReplies.ValueOf` reads a bare `>` as the empty value); this is
  the same one line here, plus a `test_config_read` case. Raised by
  meshmaster.dk task p4.23-165.

