# The fixtures that wait

**Not a copy — yet.** Every file here is an envelope of the agent protocol that
twinoak/meshmaster.dk has not put on its `master` at the time the task that needs
it ran, written here **from the line the task carries** so that this side can be
built and tested against the same bytes. The two repositories' piles run at the
same time, and neither waits for the other.

`tools/sync-fixtures.ps1` does not touch this directory and
[`MANIFEST.txt`](../contracts/MANIFEST.txt) does not list it. The copied tree,
[`tests/fixtures/contracts/`](../contracts/README.md), is the contract and stays
the contract: when the other side lands,

```powershell
.\tools\sync-fixtures.ps1 -Commit master
```

brings the real file across, it must be **the same bytes** as the one here, the
tests are pointed at the copied tree and the file here goes. A difference is a
wire the two ends do not agree on, and it is found before any release — it is the
first item of the pile's `FOLLOWUPS.md`.

| File | Written for | From meshmaster.dk's |
|---|---|---|
| `NeighboursResultTotal.json` | task p1.15-56 | task 239, deliverable 1 |
| `HeartbeatUpdate.json` | task p1.15-61 | task 244, the contract's shape 1 |
| `AgentHeartbeatUpdate.json` | task p1.15-61 | task 244, the contract's shape 1 |
| `UpdateAsk.json` | task p1.15-61 | task 244, the contract's shape 2 |
| `HeartbeatReplyUpdate.json` | task p1.15-61 | task 244, the contract's shape 2 |
| `UpdateResult.json` | task p1.15-61 | task 244, the contract's shape 3 |

The two pairs **nest**, as the copied tree's pairs do:
`AgentHeartbeatUpdate.json` is the copied `AgentHeartbeat.json` with
`HeartbeatUpdate.json` appended as its last member, and
`HeartbeatReplyUpdate.json` holds `UpdateAsk.json` verbatim.
`test/test_contract_fixtures` holds all three relations.

[`docs/upstream.md`](../../../docs/upstream.md) is the page: there first, here
second, and the sync's copy is the one that holds.
