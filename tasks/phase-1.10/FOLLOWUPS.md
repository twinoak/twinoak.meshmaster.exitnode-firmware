# Pile 1.10 — follow-ups

Review findings, tagged with the task that must pick them up: `[NN]` for a task
of this pile. A session reads this file before it starts (RUNBOOK.md section 0)
and treats every open item tagged for its task as part of the task. Only the
reviewer ticks an item.

- [x] **[44] An empty `get` reply is a value, not a raw line.** `cliReplyValue`
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
  meshmaster.dk task p4.23-165. **Done by task p1.10-44**
  (`tasks/phase-1.10/reports/44.md`): `cliReplyValue` reads a bare `>` as the
  empty value, `CliReply.AnEmptyGetReplyIsTheEmptyValueAndNotARawLine` covers
  `"> "`, `">"` and `">  "`, and nothing else of reply parsing moved.


## The review of 45 and 46 (12 November)

Both merged (#49, #50) with their pull requests' ci green. Nothing in either diff needs changing.
45 was right to follow the spec against this pile's own task file on the empty owner info: `""` is
a reading and absent is not.

- [ ] **[the release that carries 45 and 46] Not before meshmaster.dk's pile 4.30 is deployed.**
  46 turns an `acl` login answered at guest into an `ok` with `loggedInAs`. A server without 180
  ignores the field and reads that `ok` as the pair verifying itself - in the flow's verify step,
  as the ACL entry working. The order is the server first, then this firmware.
- [ ] **[later] `loggedInAs` on an outcome that is neither `ok` nor `unauthorized`.** Report 46
  says the server reads the tier "on an `ok` and on a refusal alike". It does, and on nothing
  else: `AclLoginFellShort` judges `unauthorized` and `ok`, so an `unreachable` that carries
  `loggedInAs: guest` moves no association. Nothing is lost that the next read does not bring;
  the sentence in the report is wider than the server.
- [ ] **[bench] The two walks of reports 45 and 46 are Thomas's**, with a board: the owner info at
  `acl` and at guest (`docs/console.md`), and item 21 for the guest answer. The first one that
  matters is AAR_LH through Risskov.
- [ ] **[by hand] The driver could not delete five remote branches:**
  `task/p1.10-41-no-password-for-a-local-login`, `task/p1.10-43-a-local-job-is-priced-as-local`,
  `task/p1.10-44-an-empty-get-is-a-value`, `task/p1.10-45-the-owner-info-whole` and
  `task/p1.10-46-a-guest-answer-the-job-can-use`.
