# 24 — The relaxed dialect

**Depends on:** 23, and meshmaster.dk's **pile 4.15 merged** (tasks 129 and 131: the relaxed encoder with `JsonEscaping.json`, the shorter `JobCredential.json` samples, `AgentHeartbeatNoWake.json`). Before anything else, look: if `tests/fixtures/contracts/JsonEscaping.json` and `AgentHeartbeatNoWake.json` are not on meshmaster.dk's `master` (`gh api repos/twinoak/meshmaster.dk/contents/tests/fixtures/contracts`), stop on purpose: write `tasks/phase-1.6/logs/STOPPED-24.md` naming the missing file (RUNBOOK.md §5), change nothing else, and end — Thomas resumes with `-From 24` when they are there.
**Spec:** meshmaster.dk at `master` (read-only): `tests/fixtures/contracts/README.md` (the escaping rule 129 wrote down), `docs/dev/unit-protocol.md`, `src/MeshMaster.Contracts/ContractsJson.cs`; the 1.5 review ([`../phase-1.5/FOLLOWUPS.md`](../phase-1.5/FOLLOWUPS.md): *Left by 18*, the second heartbeat fixture; *Left by 17*, the sixteen-character password; the `[Thomas]` item about `docs/library.md` §2); this repository's `lib/agent-core/include/agent/JsonWriter.h` (the default encoder's rule, mirrored byte for byte, ~L47–60 and `escape()` ~L162), `tools/sync-fixtures.ps1`, `test/test_envelopes/` (`AnAgentThatProposesNothing…` ~L105, which cuts the fixture), `test/test_contract_fixtures/`, every `TheSameInputMakesTheDotNetFixtureByteForByte`, `lib/agent-core/src/LoginSequence.cpp` (`noteCut`), `CHANGELOG.md`, `docs/library.md` §2, §9, §10.
**Size:** one session.
**Model:** claude-opus-5

## Goal

meshmaster.dk now writes its contracts with the relaxed encoder: `+`, `>`,
`Å` as themselves. This firmware's one JSON writer copies the default
encoder's escaping byte for byte, so its fixture tests are about to compare
against bytes it no longer makes. Move the writer to the server's new rule,
sync the fixtures, take the two new samples into the tests they were made
for, and close `v0.3.0`.

## Deliverables

- **Fixtures synced** (`tools/sync-fixtures.ps1 -Commit master`; the
  manifest's commit moves; `-Verify` green).
- **`JsonWriter` writes the relaxed rule**, as the fixtures README states it
  and `JsonEscaping.json` pins it: a test writes that fixture's string and
  compares bytes, and each clause of the rule has its own assertion so a
  failure names the clause. Where .NET escapes by a Unicode table this
  writer cannot carry (unassigned code points, if the rule names them), write
  the nearest cheap rule, say which way it differs in the header comment, and
  test that the server reads it (a parse of the bytes against the record,
  not a byte comparison). The header's first paragraph and its `string()`
  comment are rewritten for the new rule. Report the bytes a typical CLI
  reply and a Danish repeater name save per result.
- **The second heartbeat fixture.** `AnAgentThatProposesNothing…` reads
  `AgentHeartbeatNoWake.json` byte for byte instead of cutting the other
  file at `,"nextCheckInS":`; the cut goes.
- **The shorter credential samples.** Any test that relied on
  `JobCredential.json`'s sample being longer than fifteen characters (the
  `noteCut` path) builds its own long password instead, and says why: the
  server now refuses to store one (meshmaster.dk 131), and this end still
  cuts and says so if one ever arrives.
- **`docs/library.md` §2 for a private repository.** A `git:` URL at a tag
  cannot be fetched without a credential: describe what the Walter did (a
  checked copy of `lib/agent-core/` at a tag, with the tag and a hash
  recorded, and the script that refreshes it), and the read-only token
  route as the alternative, with where the token lives (never in the
  repository).
- **`v0.3.0` closed.** `CHANGELOG.md`'s section gets the fixtures' new
  manifest commit — *the dangerous one*: the wire dialect moved — and the
  writer's rule in a sentence a consumer reads before it syncs. The tag is
  Thomas's.

## Acceptance criteria

- [ ] `-Verify` green at the new commit; `test_contract_fixtures` green.
- [ ] Every byte-for-byte fixture test passes against the regenerated fixtures, and `JsonEscaping.json` is matched clause by clause.
- [ ] The Heltec's heartbeat equals `AgentHeartbeatNoWake.json` byte for byte; no test cuts a fixture any more.
- [ ] `pio test -e native`, 20's matrix and both board builds green; sizes in the report against `v0.2.0`.

## Not verified here

A board's output read by the real server — the next bench walk (a `frame tx`
line and one job result's raw body pasted into its report).

## Out of scope

The Walter's own sync (its next pile, against `v0.3.0`).
