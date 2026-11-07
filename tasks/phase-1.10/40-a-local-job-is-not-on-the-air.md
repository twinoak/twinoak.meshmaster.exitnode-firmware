# 40 — A local job is not on the air

**Depends on:** nothing (after 39 in the order).
**Spec:** `lib/agent-core/include/agent/IMesh.h` (the interface a board implements; `beginJob` ~L150, `supports`, the defaulted virtuals such as `floodScope` and `setFloodMode`), `lib/agent-core/src/JobRunner.cpp` (`idle` ~L107: `platform_->delayMs(1)` in a loop until the deadline; `spend` ~L134: the airtime gate's wait, `gate_.addWait`, `gate_.took`, `ctx.transactions++`), `lib/agent-core/include/agent/Airtime.h`, `docs/library.md` (the `IMesh` contract, the budget), `test/` (the runner's tests and their fake mesh); **read only, from the sibling repository** `twinoak.meshcore.walter-firmware`: `components/agent/include/walter_agent/link_mesh.h` (*the local door*: a job for the unit's own repeater sends nothing on the mesh) and `tasks/phase-8/FOLLOWUPS.md` (*A busy wait below one tick still starves the watchdog* and *A local job is paced like the air*, 7 November).
**Size:** one session.
**Model:** claude-opus-5

## Goal

The library is also the agent of a repeater-v2 unit's Walter. There, a job for the unit's own
repeater never goes on the air: the board's mesh answers it over the UART to the radio it is wired to
(*the local door*).

On 7 November, a `config.read` of that repeater (34 lines) was paced by the exit node's airtime budget
anyway (*2 a minute, gap 20 s*): `spend` → `idle` waited up to about 40 s before the third line. On
the Walter that wait spun (its own bug, being fixed there), the task watchdog reset it, and even
without the reset the read would have taken 11 minutes for nothing. A telemetry read waited 20 s for
nothing too.

The budget exists to share one radio channel. A job that does not use the channel should neither
wait for it nor spend it.

## Deliverables

1. **The board says when a job is local.**
   - `IMesh` gains a defaulted virtual (for example `bool answersLocally() const { return false; }`)
     that is true from a `beginJob` the board will answer without the air until the `endJob`.
   - This board's `AgentMesh` keeps the default: it has no local door.
   - Document it in `docs/library.md` beside the other defaulted virtuals.
2. **`spend` skips the gate for a local job.** No wait, and no `took`: a local job neither waits for
   the airtime budget nor uses it up for the next job.
   - Decide whether its transactions still count in `ctx.transactions`, which the server reads as
     *transactions*. Choose the reading that keeps meshmaster.dk's planned load about the air, and say
     why in the report and in `docs/library.md`.
   - The lease check stays: a local job still ends when its lease does.
3. **Tests** with the fake mesh answering locally:
   - a 34-line `config.read` at a budget of 2 a minute finishes with no budget wait and no `idle`
     longer than the reply waits;
   - the same job on a non-local mesh still waits as today;
   - a non-local job after a local one is not given a free slot by it.
4. **Nothing else changes for this board.** Its build and every existing test stay as they are.

## Acceptance criteria

- [ ] `IMesh` can say a job is local (defaulted, documented); `AgentMesh` keeps the default.
- [ ] A local job neither waits for nor spends the budget; transactions counted as decided and written down.
- [ ] Tests as listed; the build and the native tests green.

## Out of scope

The Walter's side (its pile 9 syncs this library at a commit of `master` and implements the virtual).
The login material of a local job (41).
