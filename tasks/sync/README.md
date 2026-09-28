# MeshCore sync tasks

The second tier of keeping up with MeshCore. The first is automatic — a weekly
workflow opens a pull request that moves the pin, and a green one is merged by
a person — and this is what happens when that pull request is **red**:
`tools/new-sync-task.ps1 -Tag companion-vX.Y.Z` writes a task file here from
[`TEMPLATE.md`](TEMPLATE.md) with the tag, the pull request and the failing
job's log in it, and the driver runs it like any other task:

```powershell
.\tools\new-sync-task.ps1 -Tag companion-v1.18.0 -Pr 12
.\tasks\run-all.ps1 -Phase sync -From 01
```

[`docs/upstream.md`](../../docs/upstream.md) is the page that explains all
three tiers. Reports land in `reports/NN.md` as everywhere else.

## Order and dependencies

Newest last. Each one is independent — a bump is a bump — but they are numbered
in the order they were written, and the driver runs them in that order.

<!-- new-sync-task.ps1 inserts its row directly under the |---| line, in the
     driver's format: | NN | [name](NN-name.md) | tag | what happened | -->

| # | Task | Tag | Why it needed a session |
|---|---|---|---|

Empty: every bump so far was green, or there has not been one yet.
