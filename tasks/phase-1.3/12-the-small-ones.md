# 12 — The small ones

**Depends on:** nothing.
**Spec:** `src/ui/DisplayLines.h` (the state and the faults, task 09), `src/agent/AgentLoop.h` (`online()`, `retrySeconds()`, `holdUntil_`), `src/agent/ServerPolicy.h` (task 10's `WaitRetryAfter` on 502/503/504), `boards/README.md` and `boards/partitions/*.csv` (task 11), `src/agent/ConfigRead.h` (`kMaxConfigKeys`, `kDefaultConfigKeys`) and `src/agent/JobEnvelopes.cpp` (the claim's key list), meshmaster.dk `spec/CONFIGURATION.md` §3 and its task p4.9-102; findings 1, 2, 3 and 5 in the README.
**Size:** one session, short.
**Model:** claude-opus-5

## Goal

Four boxes: two on the display 09 built, one a sentence, and the key cap
that meshmaster.dk's task p4.9-102 runs into the same night.

## Deliverables

- **`no token` on line four.** `DisplayFault` gains `NoToken`, ahead of
  `Backoff` in `faultOf`'s order and after `NoWifi` (a board with no WiFi
  cannot use a token yet): `DisplayState.serverConfigured` from
  `AgentClient::configured()`, and the line reads `no token: set it`
  (17 characters). `renderLines`' native tests gain the case, and the
  order test gains the row.
- **A `Retry-After` hold is a wait the display can see.** `AgentLoop::online()`
  is also false while `holdUntil_` is in the future - the server has said
  when to come back, which is not *answered* - and `DisplayState.backingOff`
  is `agent.retrySeconds() > 0` rather than the failure count, so line four
  reads `no server: retry 45s` through a deploy's 503s as it does through a
  doubling backoff. The heartbeat's counters are untouched: a 5xx with a
  header is still not a consecutive failure (task 10's reasoning holds for
  the reboot guard); this is only what the OLED says. One native test for
  `renderLines` with the hold; one for `online()` if `AgentLoop` has a
  native seam for it, else the report says it is bench-only.
- **The read takes thirty-two keys, and the default list is the server's.**
  meshmaster.dk's task p4.9-102 makes the daily `config.read` carry an
  explicit list of every key the Configuration tab shows - about
  twenty-six on a V3 repeater at v1.17 - and `JobEnvelopes.cpp` refuses a
  claim with more than `kMaxConfigKeys` = 20 (*a config.read asks for more
  keys than this firmware can read in one job*). Raise it to 32: the
  constant sizes both `ClaimedJob::configKeys` (33 bytes a key) and
  `ConfigSnapshot::values` (`kMaxConfigValueLen` + the key, about 240 bytes
  a slot, so some 7.7 KB static for the snapshot - say the two sizes in the
  report); keep the refusal for 33. The read loop already does the right
  thing with a key the firmware does not know: `cliReplyValue` fails on
  `??: <key>` and the reply goes to `snapshot.addRaw` verbatim, the read
  stays `ok` - but `kMaxConfigRawLines` is **6** and `addRaw` drops the
  seventh silently, and a V3 asked the full list at v1.17 answers `??` to
  the FEM gains at least; raise it to 12 and make an overflow one last raw
  line that says how many were dropped. And `kDataCap` = 4 608 bytes is the
  result's buffer, with `writeConfigReadResult` answering 0 - the job
  `Failed` - when the snapshot does not fit: size it from the constants
  (32 values at their maximum, the ACL at its maximum, the regions reply,
  twelve raw lines) with a `static_assert`, or make the writer drop raw
  lines first and say so in the JSON; never a failed read for a long
  `owner.info`. One native test reads a claim of 32 keys against the fake
  with three unknown keys, an `owner.info` holding quotes and a backslash,
  and a full ACL, and asserts the JSON parses and holds all of it. And `kDefaultConfigKeys` gains `agc.reset.interval` in the
  server's position (08's finding 6), so the fallback list is the server's
  sixteen exactly; `test_config_read` pins the count and the order against
  `ConfigReadPayload.DefaultKeys` in the contract fixtures if the fixtures
  carry it, else against the literal with a comment naming the file.
- **One sentence in `boards/README.md`**: the merged image's `0xFF` fill
  reaches `otadata` at `0xE000` and that is correct - `boot_app0.bin` is
  written there by the same merge and the bootloader wants exactly that -
  so `otadata` stays where the framework put it.

## Acceptance criteria

- [ ] A state with WiFi up, no token and no heartbeat renders `no token: set it` on line four; with a token and a 503 hold of 45 s it renders `no server: retry 45s`; the order of faults is tested.
- [ ] `online()` is false during a `Retry-After` hold and true again after a heartbeat.
- [ ] A claim listing 32 keys parses; 33 is refused with the sentence; a read of 32 keys with three unknown, a quoted `owner.info` and a full ACL ends `ok` with a result that parses and holds all of it; a thirteenth raw line is counted, not lost.
- [ ] The report states the snapshot's and the claim's new static sizes and the result buffer's.
- [ ] `kDefaultConfigKeys` is the server's sixteen in the server's order, pinned by a test.
- [ ] `boards/README.md` carries the `otadata` sentence.
- [ ] `pio run -e heltec_v3`, `pio run -e heltec_v4`, `pio test -e native` green; CI green.

## Not verified here

The OLED on a board during a real deploy of meshmaster.dk: the next release
is the test.

## Out of scope

The rotation probe (13).
