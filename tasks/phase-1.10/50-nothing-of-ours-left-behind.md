# 50 — Nothing of ours left behind

**Depends on:** 48.
**First:** in `meshmaster.dk` (`twinoak/meshmaster.dk`: the sibling folder, or through `gh api`; read only), check that `master` has `tasks/phase-4.33/reports/191.md`. If it does not, write `logs/STOPPED-50.md` saying so, change nothing else, and end. 191 sets the contract this task follows.
**Spec:** meshmaster.dk, read only, as merged with 191: `spec/API.md` §3 (`credential.acl`: 0), `spec/EXIT-NODES.md` §4.3 and §7; this repository: `lib/agent-core/src/JobRunner.cpp` (`restoreAcl` ~L495: it acts on a level of 1 or 2 today), `lib/agent-core/src/JobEnvelopes.cpp` (the claim's `acl`), `lib/agent-core/src/Airtime.cpp`, `test/test_runner`, `tests/fixtures/contracts`, `docs/library.md`, `docs/console.md`.
**Size:** one session, short.
**Model:** claude-opus-5

## Goal

A login with the admin password puts this exit node's key into the repeater's ACL at admin, saved.
Where the exit node holds a grant, the session ends by putting the key back to the granted level.
Where it holds none, nothing takes the key off: a guest association whose exit node carries the
daily configuration read, or a one-off admin job through an exit node with no association, leaves
this board's key at admin on somebody's repeater.

meshmaster.dk's 191 makes the claim say so: `acl: 0` on an admin job means *you should hold nothing
here*.

## Deliverables

1. **`acl: 0` on an admin claim:** the session's last command is `setperm <own key> 0`, with 48's
   check for level 0. Then the session is dropped.
   - Sent after the job's own lines, whatever the job came to, as the restore is.
   - Not for an `acl.set` on our own key, which is the level itself; not after a `reboot`, for the
     reason the restore gives there.
   - A job that ended in silence sends nothing, as the restore does not, and says the key may still
     be at admin.
2. **`acl: 0` on a job of another class changes nothing.** A blank login that is answered above
   guest where the claim says 0 is not this task's to judge.
3. **The price**: one more transaction on such a claim, in `transactionsFor` and the lease estimate.
4. **The contract fixtures** synced from meshmaster.dk at the commit that has 191.
5. **The words.** `docs/library.md`, `docs/console.md` (a walk item: a config read through a guest
   association, then `get acl` on the repeater's own console), `CHANGELOG.md`.

## Acceptance criteria

- [ ] An admin `config.read` with `acl: 0`: the last line sent is `setperm <own key> 0`, the session is gone, and the result says what became of it (test).
- [ ] The same claim with no `acl`: nothing is sent (test). With `acl: 1`: the restore of today (the existing tests stay).
- [ ] `acl.set` on our own key and `reboot` with `acl: 0`: no second `setperm` (tests).
- [ ] `tools\verify.ps1` green. CI green.
