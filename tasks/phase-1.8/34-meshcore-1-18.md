# 34 — MeshCore companion 1.18

**Depends on:** 33 (the bump re-reads what 31–33 cite); the branch `origin/upstream/companion-v1.18.0` (`b43c33b`, 19 October, the weekly workflow's) and its pull request [#26](https://github.com/twinoak/twinoak.meshmaster.exitnode-firmware/pull/26).
**Spec:** `docs/upstream.md` (the three tiers; *What of MeshCore this firmware actually uses* — extend it with what 32 and 33 added; *Reading the firmware you build against*; the pin's history); the same range already read for the repeater (read-only sibling, `twinoak/twinoak.meshcore.repeater-firmware`: `tasks/phase-3.5/06-meshcore-1-18.md` and `tasks/phase-3.5/reports/06.md` — 266 commits `d9296435..81dbae59`, tier three *none found* for the repeater, and the two behaviours that are new: a transport code of 0 is un-scoped, `01f9293b` (#3568); a node whose `flood.advert.interval` is 0 answers no DISCOVER); `tasks/RUNBOOK.md`; `CHANGELOG.md`.
**Size:** one session, long.
**Model:** claude-fable-5-1

## Goal

The weekly check opened #26 on 19 October: MeshCore `companion-v1.18.0`
(`81dbae59`), 266 commits on from `companion-v1.17.1` (`d9296435`), which is
this firmware's pin. Nobody has read it since. The repeater firmware walked the
same range on 22 October (its 3.5/06) and moved, so the repeater and the exit
node now build against two different MeshCores. This task reads the range for
**the companion side**, lands it, and v0.5.0 (35) ships on 1.18.

## Deliverables

1. **The pin moved, on top of 29–33.** The branch predates them all. Bring it
   up to `master` (rebase or merge, whichever leaves the cleaner history) so
   the bump is one commit on top of what 35 will tag. Use `docs/upstream.md`'s
   tier two, *a red bump is one command*, where it applies. Both boards and the
   native tests build and pass at `81dbae59`.
2. **What this firmware uses, read at the new pin.** Read every row of *What of
   MeshCore this firmware actually uses*, plus what 32 and 33 added:
   - `sendFlood` with transport codes and `TransportKey`;
   - the anonymous regions request and its reply;
   - `CTL_TYPE_NODE_DISCOVER_*` and `onControlDataRecv`;
   - the radio's noise floor and the dispatcher's air-time counters.

   Add those rows to the table. Start from the repeater's report of the same
   range, and answer for the companion side what it answered for the repeater.
   **Tier three explicitly**: no packet layout, no byte of the login, request
   or regions frame, and no reply this firmware parses has changed — or name
   what has, and what was compared against what.
3. **The two new behaviours, where they touch us:**
   - a transport code of 0 is un-scoped: 32's code already reserves 0 and
     FFFF; check that and say so;
   - a repeater with `flood.advert.interval 0` answers no node discovery: 33's
     60-second discovery misses it, and its adverts (if any) still fill the
     table. Say it in `docs/observer.md`, in one sentence.
4. **Every citation re-read.** Every comment of ours that cites a line at
   `d9296435` — in `src/`, `lib/` and `docs/`, the ones 31–33 wrote included —
   cites `81dbae59` with the line that is there now. Edit mechanically, each
   old string required to occur exactly once, as the repeater's `recite.py`
   did. The pin's history in `docs/upstream.md` is the one place `d9296435`
   stays.
5. **The cost.** RAM and Flash of both images at both pins, from the build. If
   a board is on USB, `status`'s heap line at the new pin beside 31's table; if
   not, say so, and it is part of Thomas's walk in *Afterwards*.
6. **Recorded:** `CHANGELOG.md`'s unreleased section names the pin
   (`companion-v1.18.0`, `81dbae59`), which 35 turns into v0.5.0.
7. **Landed:** #26 merged. If the rebase makes a fresh pull request cleaner,
   open one, and close #26 with a link to it. The `upstream/companion-v1.18.0`
   branch is deleted after the merge.

## Acceptance criteria

- [ ] Both boards build and the native tests pass at `81dbae59`; CI green on the pull request.
- [ ] The report answers tier three explicitly, with the evidence (the diffs read, the frames compared).
- [ ] No citation of `d9296435` is left in `src/`, `lib/` or `docs/` outside the pin history (a grep in the report).
- [ ] RAM and Flash of both images at both pins are in the report.

## Out of scope

What 1.18 adds that this firmware does not use. The repeater firmware (done).
