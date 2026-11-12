# 48 — A line that cannot be asked twice

**Depends on:** 47 (the blank login this uses must not be answered by a late reply).
**Spec:** meshmaster.dk, read only: `spec/EXIT-NODES.md` §4.1 (steps 3 and 4; the cost: *one ACL transaction with a login of its own*), §4.3 (*Our own logins*, *Revoke*), §7; `tasks/phase-4.32/188-…` and `189-…` there are the server's half. This repository: `lib/agent-core/src/JobRunner.cpp` (`runAclSet` ~L899–945, `restoreAcl` ~L495–545, `cliLine` ~L465 and `exchange` ~L416, `probeReachable` ~L364), `lib/agent-core/src/Sessions.cpp` (`get`, `put`), `lib/agent-core/src/Airtime.cpp` (`transactionsFor`), `lib/agent-core/src/JobResults.cpp` (`writeAclSetResult`), `test/test_runner`, `docs/library.md`, `docs/console.md`; MeshCore at the pin: `examples/simple_repeater/MyMesh.cpp` (`onPeerDataRecv`: a console line is run only for a sender that `isAdmin()`; the `setperm` branch of `handleCommand`), `src/helpers/ClientACL.cpp` (`applyPermissions`).
**Size:** one session.
**Model:** claude-opus-5

## Goal

`setperm <our own key> <level>` is sent on a session whose key is, at that moment, admin. On stock
firmware the session is the ACL entry. Once the line has run, the key is at `<level>`, and the
repeater drops every further console line from it in silence. So when the "OK" is late or lost, the
retry cannot be answered, and the job ends `unreachable` with the line having done its work.

meshmaster.dk, 12 November: 32 grants confirmed and 20 failed this way after an answered admin
login (jobs 4558, 4568, 4590, 4644, 21870, 26068 and others). The restore after an admin job is the
same line and fails the same way: association 3 there was found *elevated* 7 times in 39 days.

Level 0 has a second reason. `applyPermissions` deletes the entry by moving the later ones down,
and `onPeerDataRecv` then addresses the "OK" to whichever client now sits in that slot. Unless our
key was the last one in the table, the reply cannot reach us at all. That is how the source reads;
nobody has tried it on a board, so check it in the pinned source before building on it and say in
the report what you found.

One more thing rides on this. After a confirmed grant the session is kept as an `acl` session, and
the verify that follows reuses it: on meshmaster.dk job 26077, *session reused (acl, read-only,
146 s old)*, and the association went active as *acl login ok* with no login made.

## Deliverables

1. **The line is sent once, and its silence is checked with a blank login**, in `runAclSet` for our
   own key and in `restoreAcl`:
   - answered at the level asked for: the line ran. `confirmed: true`, and the log says it was
     confirmed by the login and not by an "OK";
   - answered at admin: the line did not arrive. The session is admin again; send it once more, and
     check once more. Two rounds at most;
   - answered at guest, or not at all: not confirmed, as now.
2. **Level 0:** a blank login answered at guest is the confirmation that the key is off. No answer
   is not one (a repeater with a guest password answers a key it does not know with silence, and so
   does one that is off the air): the result says the line went out and could not be confirmed.
3. **A grant drops the session.** After a confirmed `acl.set` on our own key at level 1 or 2 the
   session is not kept, so the verify logs in by key, which is what the spec prices. The restore
   keeps its session as now.
4. **The price is right.** `transactionsFor` and the lease estimate count the check where it can
   happen; an estimate may be too high and never too low.
5. **The words.** `docs/library.md`, `docs/console.md` (walk items: a grant whose "OK" is lost; a
   revoke of a key that is not last in the table), `CHANGELOG.md`.

## Acceptance criteria

- [ ] Grant, "OK" lost: one blank login answered read-only, `confirmed: true`, outcome `ok`, three transactions (test).
- [ ] Grant, the line itself lost: blank login answered admin, the line again, "OK": `ok` (test). Lost twice: not confirmed, and the session is dropped (test).
- [ ] Restore, "OK" lost: the same check, and a `config.read` reports `aclRestored: true` (test).
- [ ] Level 0, no "OK", blank login answered guest: `confirmed: true` (test). No answer to the blank login: `confirmed: false` with the line in the log (test).
- [ ] After a confirmed grant, the next `acl` job logs in (test).
- [ ] `transactionsFor` covers the check (test).
- [ ] `tools\verify.ps1` green. CI green.
