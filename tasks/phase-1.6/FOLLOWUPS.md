# Pile 1.6 — follow-ups

Review findings, tagged with the task that must pick them up: `[NN]` for a
task of this pile. A session reads this file before it starts (RUNBOOK.md
section 0) and treats every open item tagged for its task as part of the task.
Only the reviewer ticks an item.

## Review of 20–24 (26 October) — the pile, after it landed

Read against the five reports, the task files and the diffs (PRs #22–#25, #27; `v0.3.0` is `cc5a343`).
20 42 min, 21 65, 22 66, 23 56; 24 stopped on purpose after 2 min on 18 October (meshmaster.dk 4.15
not merged) and ran 48 min on 20 October. Every pull request green in its first CI round. The pile did
what it promised and the Heltec's behaviour and heartbeat bytes are unchanged where promised. One real
defect came in with 24, and one gap is in 22.

- [ ] **[1.7] `JsonWriter` lets invalid UTF-8 through to the wire since 24.** The decoder
  (`lib/agent-core/include/agent/JsonWriter.h` ~L107–132) accepts overlong forms, code points above
  U+10FFFF and lead bytes F5–FF, and since clause 5 copies a character's source bytes verbatim, an
  overlong `E0 83 85` (Å) goes out as those three bytes; `F4 90 80 80` and `F5 80 80 80` come out as
  lone `\uDC..` escapes (those predate 24). Before 24 every non-ASCII code point was escaped, so the
  output was ASCII whatever came in. Only advert names are checked first (`validUtf8PrefixLength`,
  `src/mesh/AgentMesh.cpp` ~L183); CLI replies and `config.read` lines go straight to `w.string()`.
  A repeater whose name was set with such bytes gives a result System.Text.Json refuses — and a
  result the server refuses is kept and retried at the head of the ordered queue, blocking the ones
  behind it until the buffer evicts it. Fix: the same checks as `validUtf8PrefixLength` (overlong,
  surrogates, above U+10FFFF, F5–FF) inside `string()`, each as a `Clause8` case
  (`C0 80`, `E0 83 85`, `F0 80 83 85`, `F4 90 80 80`, `F5 80 80 80`); a cut sequence gives one U+FFFD,
  not two (maximal subpart).
- [ ] **[1.7] [walter] A kept advert gets the wrong time when the boot that wrote the image had no
  clock.** `AgentLoop::keepAcrossSleep` stores epoch 0 when `!clock_->hasTime()`, `restoreKept` then
  sets `heardAtMillis` to the restore, and `buildBatch` stamps the advert with that — wrong by the
  whole sleep. That is exactly the wake that keeps adverts (heartbeat failed, server or LTE away). The
  test (`test_loop.cpp` ~L1572) sets the clock before the write, so this case never runs. Drop and
  count such adverts (or carry their age and the sleep's length).
- [ ] **[1.7] A type the mesh declines is still priced by `wakeJobs`** (23's report, *Deviations*): a
  bound `trace` in a tight wake is reported `failed`, *wake budget*, rather than `unsupported`. One
  check before `transactionsOf`.
- [ ] **[walter] The store must be atomic, and `restoreKept()` must run before the first `wake()`.**
  `IRetained.h` says a torn write is not the store's problem, but with one slot a torn write also
  loses the previous image (and its jobs stay leased): an NVS blob is power-safe, a raw flash region
  wants two slots chosen by CRC. And a first wake before the restore overwrites the store.
- [ ] **[walter] `HeartbeatIdentity::transport` is the second member** (`AgentLoop.h` ~L272): a
  positional `{ver, board, companion}` still compiles and shifts every field. Initialise by name, as
  `main.cpp` does; the value must be exactly `native`, which nothing validates.
- [ ] **[reviewer] 22 grew the Heltec's RAM by 304 B** where the task said *byte for byte*:
  `heardAtEpoch` × 64 (256), 20 in `AgentMesh`, 28 of loop members. Behaviour and bytes on the wire
  are unchanged; accepted. A store-passing consumer that does not sleep loses the results that did
  not fit at the write (`dropNewest`) — documented.
- [ ] **[reviewer] The unassigned-code-point exception is documented here and not on meshmaster.dk**
  (`tests/fixtures/contracts/README.md`, `docs/dev/unit-protocol.md` §8 say the writer mirrors the rule
  without it); and it is proven by an ArduinoJson parse only, never by System.Text.Json. For
  meshmaster.dk's next pile.
- [ ] **[reviewer] 21's byte cap does not count the config, heartbeat and claim bodies, nor refused
  bodies**; *covers the claim* in the CHANGELOG is true of seconds only. The Walter's metered plan
  needs the margin. And 23 validates the transport against nothing, because the fixtures carry only
  `onboard` (a meshmaster.dk fixture of `AgentTransports.All` would close it).
- [ ] **[reviewer] 20's matrix** is the host's GCC; the board compiles the library as `gnu++11` with
  the Xtensa GCC, outside it. CI's `ubuntu-latest` will drift; the check prints the version.
- [ ] **[Thomas]** Flash `v0.3.0` and walk the bench (one `frame tx` line, one raw result body with
  `> ` and a Danish name). **Read `status`'s heap with WiFi and a TLS call done before pile 1.7 adds
  TLS MQTT** — there is no measured free-heap figure for any v0.2/v0.3 image; static RAM is now 49 %
  of the V3's 320 KB (it was 25 % when the README's example figures were written). AarhusC and
  Grindsted are still on `v0.1.3`.
- [ ] **[Thomas] Housekeeping:** `origin/task/p1.6-21-…`, `-22-…`, `-23-…` (the driver could not
  delete them); `out/` holds a 24 MB `.elf`, images of 15–24 and `patch-24*.py`; the driver's
  preflight said *no g++ and no Docker* on 20 October while the session used Docker — its check is
  wrong.

### Fine as it is

- **20:** `epoch`; the predefined-name check asks the compiler and adds other targets' names, with a
  self-test in CI; the ArduinoJson pragma is scoped to one include and `test_library` forbids any
  other; no `-Wno-error`; both boards ±0.
- **21:** one ledger (`UplinkMeter`) that every `IUplink` call goes through; one bounded flush;
  results kept in order; `loop()` the same path unbounded.
- **22:** the image is written field by field, little-endian, with magic, version, length and
  CRC-32; every record bounds-checked before it is read, a bad one refused whole; no field for
  anything but result bodies and adverts; unchanged images not rewritten; nothing linked into the
  Heltec.
- **23:** `supports()` defaults to true and is asked at the heartbeat and before each job; `viaPath`
  and `snr` gone; 0.3.0 in all three places; the CHANGELOG names the compile error.
- **24:** clauses 1–7 match the server's rule for valid UTF-8, `JsonEscaping.json` byte for byte;
  `AgentHeartbeatNoWake.json` read whole and no test cuts a fixture; fixtures at `79b1e9c`; §2 for a
  private repository carries no secret; CHANGELOG `v0.3.0` closed.
- **Sizes against v0.2.0:** V3 RAM +336, flash +1 892; V4 RAM +336, flash +1 884.
