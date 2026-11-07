# 38 — Line by line

**Depends on:** pile 1.9 merged and `v0.6.0` published (both are).
**Spec:** `lib/agent-core/include/agent/JobRunner.h` (`JobLog` ~L105–145: `add()` joins entries with `"; "`; `drop()` cuts the oldest half at byte 512, wherever that falls; the 1 024-byte buffer), `lib/agent-core/src/JobRunner.cpp` (the ~95 `g_log.add(...)` lines; several have a `"; "` of their own inside one entry, e.g. ~L494–541), `lib/agent-core/include/agent/JsonWriter.h` (~L246: `\n` already leaves as `\\n`), `lib/agent-core/src/JobEnvelopes.cpp` (~L421: `"log"`), `test/test_job_results/`; meshmaster.dk's `src/MeshMaster.Agent/AgentLog.cs` (`JobLog.Append` ~L99: the .NET agent puts a line break between entries) and `Pages/Jobs/Details.cshtml` (~L361–366: *The agent's log* is a `<pre>` with `whitespace-pre-wrap`, so it keeps line breaks), read only.
**Size:** one session.
**Model:** claude-opus-5

## Goal

Thomas, 5 November: *the agent's log under a job seems to be missing line
breaks, which makes it rather hard to read.* The page keeps whatever line
breaks a log has. The .NET agent sends one entry per line. This firmware sends
every entry of a job in one line, joined by `"; "`, so the page shows one long
wrapped paragraph. On the page, the joins can't be told from the `"; "` that
several entries have inside them. The page can't split it afterwards without
cutting some entries in half. So the fix is here, where the entries are still
separate.

## Deliverables

1. **One entry per line.** `JobLog::add` puts a `'\n'` between entries instead
   of `"; "`, as the .NET agent does. The JSON writer already escapes it, and
   the 4 000-character bound of API §3 is unchanged.
2. **Drop whole entries.** When the buffer fills, `drop()` removes the oldest
   entries up to the first line break after the half, so the log never starts
   mid-sentence. If one entry is longer than half the buffer, it is cut as
   today: there is no line break to cut at.
3. **Words:** the README's line about the result's log, `CHANGELOG.md`
   (*Unreleased*), and the host tests. Nothing on the server changes.

## Acceptance criteria

- [ ] Host test: three entries give `a\nb\nc`, and the envelope's `"log"` carries `a\\nb\\nc`.
- [ ] Host test: filling the buffer drops whole entries, so the first line after a drop is a whole entry. A single entry longer than half the buffer is still bounded.
- [ ] `pio test -e native` green, both board builds green, CI green.

## Out of scope

The wording of the entries. Timestamps per entry. The server's page, which
already keeps line breaks. Releasing: this ships with the next release of the
firmware.
