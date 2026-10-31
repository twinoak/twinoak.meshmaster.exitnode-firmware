# 31 — The observer fits

**Depends on:** 30.
**Spec:** the bench figures of 28 October and of 30's walk (this pile's README); `src/observer/Connection.h` (`kTlsConnectionBytes`, `kUplinkReserveBytes`, `kHeapReserveFree`, `kHeapReserveLargest`, `kQueueBytes`, `kHeapYieldMs` — the reasoning in their comments), `Connection.cpp` (the gate ~L230), `src/net/MqttClient.cpp` (the `esp_mqtt_client_config_t`, which sets no buffer or task size today), `src/main.cpp` (`heapFree`, `heapLargestBlock`: `ESP.getFreeHeap()` and `ESP.getMaxAllocHeap()`), `boards/heltec_v4.json` (`BOARD_HAS_PSRAM`, 2 MB QSPI), `docs/observer.md` (*What it costs*), task p1.7-27's report.
**Size:** one session, long.
**Model:** claude-fable-5-1

## Goal

On a Heltec V3, one observer slot costs about 63 KB of heap, not the 40 KB the
gate assumes, and with one slot open a MeshMaster call took the board to
**3 088 bytes free**. The gate checked the heap before the slot opened and
never again, against figures borrowed from another firmware. Make the gate
use what this board actually costs, keep watching after a slot opens, make
each slot smaller, and let a board with PSRAM put its TLS buffers there.

## Deliverables

1. **Smaller slots.** Set the MQTT client's buffers (`buffer.size`,
   `buffer.out_size`) and task stack (`task.stack_size`) to what the observer's
   largest message and the client's own needs require, with the arithmetic in
   the code; the packet queue (`kQueueBytes`) stays or shrinks with a reason.
   Report what one slot costs after this on the host where it can be counted,
   and what is left as a bench figure.
2. **The gate from measured figures.** `kTlsConnectionBytes` becomes *what one
   slot costs* and `kUplinkReserveBytes` *what the uplink's call dips* — each a
   constant per board, set from the bench figures in this pile's README
   (and 30's, if Thomas has walked it before this task runs: read the README's
   *Measured* table), with a margin stated as a constant of its own. Where a
   figure is not measured, the constant is the conservative one and says so.
3. **The gate keeps watching.** After a slot is open, every uplink call reads
   30's *lowest free heap during the call*; when it falls under a floor (a
   named constant: what WiFi and the radio need to survive a call), the
   highest slot closes and stays down for `kHeapYieldMs`, counted as *gave way*
   — the mechanism 27 built for a failed call, now also for a call that
   nearly failed. A test with fakes: a call that dips under the floor closes
   slot 2 and keeps slot 1; a second dip closes slot 1.
4. **PSRAM where there is some.** On a board built with `BOARD_HAS_PSRAM` (the
   V4), find out where this core's mbedtls and esp-mqtt allocate their large
   buffers (internal or PSRAM, through `CONFIG_SPIRAM_USE_MALLOC` and its
   threshold, or an mbedtls allocator hook) and make them PSRAM if the core
   allows it without a custom ESP-IDF build; the gate then counts **internal**
   heap for what must be internal and PSRAM for the rest, and `get mqtt.status`
   prints both. If it needs a custom build, say so and leave the V4 on the V3's
   rule.
5. **One slot on a V3, two on a V4** (Thomas, 31 October). The figures leave
   no room for a second TLS connection on a board without PSRAM: idle ~136 KB,
   one slot ~63 KB, the uplink ~45 KB steady once 30 keeps its connection (or
   an 80 KB dip without it), and mbedtls's two 16 KB content buffers per
   connection cannot shrink without a custom ESP-IDF build. So the number of
   slots is a per-board constant: **1** on `heltec_v3`, **2** on `heltec_v4`
   (once deliverable 4 puts its TLS buffers in PSRAM; 1 if it cannot). On a
   V3, `set mqtt2.preset …` is refused with one line saying why (*this board
   has memory for one broker; a V4 carries two*), a slot 2 stored by `v0.4.0`
   is reported off with the same reason, and `get mqtt.status` shows one slot.
   The Danish guide's MeshMapper line on a V3 is therefore the one line that
   does not take — `docs/observer.md` says so (it already recommends MeshView
   in slot 1 on a V3 and MeshMapper on the V4s, Thomas's advice of 31 October;
   keep that recommendation), and the release notes' V3 sentence stays true.
6. **What fits, said plainly.** `docs/observer.md`'s *What it costs* becomes a
   table per board: idle free heap, one slot, the uplink's dip, and how many
   slots the gate will open — from the figures, not from the observer
   firmware's. `get mqtt.status`'s memory line names the board's figures.

## Acceptance criteria

- [ ] The gate's constants are per board, each with its source (measured, with the date, or conservative and why).
- [ ] The watching gate and the yield are tested with fakes; the uplink's existing suites unchanged.
- [ ] The MQTT client's sizes are set with their arithmetic; both board builds green, sizes in the report.
- [ ] `docs/observer.md` has the per-board table; a V3 refuses slot 2 with the reason and a V4 opens two (tests per board).

## Not verified here

Both slots on a V4 and one slot on a V3 for an hour beside the uplink, with
`min ever` read — Thomas, on the bench.

## Out of scope

A custom ESP-IDF build (smaller TLS content buffers) — if 30 and this are not
enough for one slot on a V3, that is the next pile. Relaying through
MeshMaster.
