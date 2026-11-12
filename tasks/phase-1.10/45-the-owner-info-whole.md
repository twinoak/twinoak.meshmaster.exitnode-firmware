# 45 — The owner info, whole

**Depends on:** nothing in this pile.
**First:** in `meshmaster.dk` (read only), check that `master` has `tasks/phase-4.30/reports/181.md`. If it does not, write `logs/STOPPED-45.md` saying so, change nothing else, and end. 181 sets the contract this task follows.
**Spec:** meshmaster.dk, read only, as merged with 181: `spec/API.md` §3 (`StatusResult`'s `nodeName` and `ownerInfo`, the status payload's `version`), `spec/EXIT-NODES.md` §7, `src/MeshMaster.Contracts/Payloads.cs` (`StatusPayload.ReadsVersion`, the rule of *when*), `src/MeshMaster.Agent/Handlers.cs` (the .NET agent's half: the same parsing); this repository: `lib/agent-core/src/JobRunner.cpp` (`runStatus` ~L540–570, `readsFirmwareVersion`), `src/ReplyCodec.cpp` (`parseOwnerInfoFirmware` ~L238; the login reply's `firmwareLevel` ~L196), `include/agent/JobResults.h` and `src/JobResults.cpp` (`writeStatusResult` ~L43); `test/test_reply_codec`, `test/test_job_results`, `test/test_runner`; MeshCore 1.18 `examples/simple_repeater/MyMesh.cpp` (`REQ_TYPE_GET_OWNER_INFO`: `FIRMWARE_VERSION\nnode_name\nowner_info`; `FIRMWARE_VER_LEVEL` 2) and `src/helpers/CommonCLI.h` (`owner_info[120]`; `set owner.info` stores `|` as a newline).
**Size:** one session, short.
**Model:** claude-opus-5

## Goal

meshmaster.dk's 181 shows *Owner info* and *Name on the radio* on every repeater page with an
association of any class. Both come from the reply of the owner-info request that the status read
already sends once a day for the firmware version. agent-core keeps that reply's first line and
drops the rest, and sends the request at class `acl` only. The firmware answers it to any login
from level 2.

## Deliverables

1. **When.** The status read sends the owner-info request at **any** class when the payload asks
   for it, by 181's rule as `Payloads.cs` writes it. Not before level 2: when the login reply says
   level 1, or says nothing, the request is skipped and the log says why, so an old repeater costs
   no timeout. A local job (40, 41) follows the same rule as one on the air.
2. **All of it.** The reply is split at its first two newlines: version, node name, owner info.
   The owner info is the rest, verbatim, newlines kept. `writeStatusResult` writes `nodeName` and
   `ownerInfo` as 181 named them, JSON-escaped, bounded at the firmware's own sizes, and absent when
   not read. A cut value says so in the log.
3. **Tests.**
   - A three-line reply.
   - An owner info of two lines.
   - A reply with the version alone.
   - A reply with no newline at all.
   - An empty owner info, which is absent, not `""`.
   - A guest-class status that asks and one that does not.
   - Level 1, skipped.
   - The result's JSON against 181's contract fixture, if it added one to the shared contract tests.

## Acceptance criteria

- [ ] The tests above green, and the existing status, reply-codec and result tests unchanged.
- [ ] `lib/agent-core` stays what `docs/host.md` says it is: host-portable, header-only where it was, no allocation.
- [ ] The Heltec build and the host tests green; CI green.
