# Pile 1.13 — follow-ups

Review findings, tagged with the task that must pick them up. Only the reviewer ticks an item.

- [ ] **[later] Pile 1.12's follow-ups are still open.**

## From the review of 55 (16 November)

- [ ] **[Thomas] `master`'s head had no ci run.** The merge of pull request #60 (27dd494,
  16 November 12:54 UTC) started no `ci` run on `master`: GitHub lists none for that commit, and
  the pull request's own run was green. A release reads the run of a push to `master` and waits
  twenty minutes for it, so it would have ended red for a run that was never coming. The commit
  that carries this note is a push and gets one. Why GitHub started none is not known.
