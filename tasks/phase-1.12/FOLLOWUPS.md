# Pile 1.12 — follow-ups

Review findings, tagged with the task that must pick them up. Only the reviewer ticks an item.

- [ ] **[Thomas to decide] Patch or more, from the button.** After a release the version moves
  to the next patch; v0.7.1 followed v0.7.0 where every pile before had been a minor step. A
  bigger step is a pull request made beforehand (`docs/release.md` §1). A choice on the button
  would be the other way.
- [ ] **[Thomas to decide] `master`'s required checks and the notes' pull request.** While
  `master` requires `build (heltec_v3)` and `native tests`, every release leaves one pull request
  to merge by hand. meshmaster.dk's merges itself because its `master` requires a pull request
  and no check.
- [ ] **[later] `loggedInAs` on a timeout**, carried from pile 1.10's follow-ups. meshmaster.dk's
  tasks 215 and 218 both need to know that a login was answered in an attempt that ended without
  an answer to something later (its jobs 28352 and 28495).
- [ ] **[later] A wake claims once, two jobs at most** (`AgentLoop::wakeJobs`, `kClaimMax`).
  With meshmaster.dk's diagnostics switch on, a unit's report takes one of the two, so the switch
  asks at every fifth check-in. A second claim while jobs wait and time is left would end that.
- [ ] **[later] Replies lost more often than the signal explains.** Exit node 9 on 13 November,
  repeater 44, trace 4 to 4.75 dB each way: logins 5 of 8 answered, status 1 of 5. *First try no
  reply, second try answered* recurs (meshmaster.dk jobs 28495, 28503, 28549, 28565). A repeater
  answers a request directly along the reply path it keeps with the sender's ACL entry, which
  only a path packet from the sender sets and a flood login clears, and floods the answer when it
  has none (`MyMesh.cpp`, `onPeerDataRecv`, `onPeerPathRecv`). To read: whether this firmware
  sends its path back after a flood login, and how long it waits for a flooded answer.

## Review of 54 (14 November)

Read: the report, `release.yml`'s last step, `tools/release.ps1` (`Get-NotesPullRequestOutcome`,
`-Action outcome`, `-Action pr-body`, the self-test's cases for them), `docs/release.md` §5, and
the runs on github.com (pull request #59 merged as 2358192; `ci` green on it and on `master`
after it; the dry run 43580722678 green). **It does what the task asked. No finding needs a
task.**

- One place decides how the step ends and writes its summary; the step itself only gathers how
  far it got. Pushed, opened, not merged is green with the pull request named in the first line
  and a warning on the run; not pushed, and not opened, are red with their own sentences.
- The summary and the pull request's body come from the same script, so the rail reads the text
  it guards.

- [ ] **[later] `master`'s two required checks are written down a second time**
  (`$script:MasterRequires` in `tools/release.ps1`), because the job's token cannot read branch
  protection. The report says so. It moves when the rules do (*Thomas to decide*, above).
- [ ] **[later] Not seen until a release publishes.** `release-notes/unreleased/` is empty and
  `VERSION` is 0.7.2, so the next press refuses for want of a line. The ending this task built is
  first read at the release after the next task that leaves one.
- [ ] **[later] The repeater firmware has not had the same yet.** Its phase 3.12 stopped at its
  first task on 14 November: that repository is public since 13 November and still asks for the
  organisation's runners, which public repositories are not offered. This repository's own rule
  (`tools/check-runners.py`) is the model its task 03 follows.
