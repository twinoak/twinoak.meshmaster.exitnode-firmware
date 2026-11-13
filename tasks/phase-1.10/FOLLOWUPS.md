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
  **Done by task p1.10-51** (`tasks/phase-1.10/reports/51.md`): the order was held and checked
  before anything was written. meshmaster.dk's newest successful `release` run on 13 November is
  `9796e1e`, which carries `tasks/phase-4.33/reports/191.md` and therefore its piles 4.30 to 4.33 -
  180 and 186 among them - so the server was deployed with 45's and 46's halves, and with 47's,
  48's and 50's, before `v0.7.0` was tagged. The release is the one that carries 45 and 46.
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

## Left behind by 49 (12 November)

- [ ] **[later] A console line on a *reused* session carries no prefix.** Task
  p1.10-49 puts the firmware's two-character prefix on every console line where
  the login's `FIRMWARE_VER_LEVEL` says the repeater reflects one, and nothing
  looser - which is right, because a prefix a repeater does not strip is run as
  part of the command. But `Context::hasFirmwareLevel` is **this job's own
  login's** (task p4.30-181 made it that), so a job that found a live admin
  session and made no login of its own does not know the level and sends its
  lines bare. That is the conservative branch and not a fault - the lines go
  exactly as they went before 49 - but it is the one case where the protection
  is absent, and it is a case that happens: a `config.read` claimed in the same
  slot as another admin job for the same repeater reuses its session, and that
  read is thirty console lines in a row. What would close it: keep the level
  with the session (`RepeaterSession` in `agent/Sessions.h`, filled by
  `SessionStore::put`) and let the prefix rule read *the level this job's login
  named, or the level the session being reused was opened at*. 49 left
  `SessionStore` alone on purpose - the owner-info gate of task p1.10-45 reads
  the same two fields, and widening them is that task's call and not 49's.
- [ ] **[upstream] A prefixed `region` would write past the repeater's reply
  buffer.** Read off the pinned source while 49 was deciding which lines may
  carry a prefix, and it is **MeshCore's**, not ours.
  `MyMesh::onPeerDataRecv` gives a console reply `uint8_t temp[166]` with the
  reply at `&temp[5]` - 161 bytes - and `handleCommand` advances that pointer by
  three when it reflects a prefix, leaving 158. `CommonCLI::handleRegionCmd`
  then writes the map with `RegionMap::exportTo(reply, 160)`, which fills up to
  159 characters and a NUL: **two bytes past the end of `temp`**, on the stack,
  for a full region map asked for with a prefix. 49 keeps this firmware out of
  it (`region` and `region def …` go bare, `cliLineTakesPrefix`), and nothing of
  ours can reach it - but the **companion radio's own CLI** is what the prefix
  exists for, so a phone asking a repeater with a full map for `region` through
  a companion does reach it. Worth an issue upstream (`docs/upstream.md` is the
  page for how this repository tracks that); it is not this firmware's bug and
  not this pile's to fix.
