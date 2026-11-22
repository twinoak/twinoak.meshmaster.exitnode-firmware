# Pile 1.19 — follow-ups

Review findings, tagged with the task that must pick them up. Only the reviewer ticks an item.

- [ ] **[later] An ask to a board whose table is still `one` has not been seen.** Risskov and
  Aarhus C both had their slot before they were asked (20 November). The first board of
  another owner will be the first to add its slot with nobody there.
- [ ] **[later] No V4 has run 0.7.4 or anything after it.** Sunds is the only one in the fleet.
- [ ] **[later] Nothing starts an image before it is released** (pile 1.17's follow-ups). It
  still stands.

## The review of 68 (21 November)

Merged at 13:36 CET (#78, master ee1f046), 89 minutes, seven of seven criteria. Read: the
report whole, and the change in `Fetch.cpp`. Not read: `Connection.cpp`, `AgentClient.cpp`, the
tests. Nothing found. Not on a board.

- [ ] **[none] The slots stand down before the manifest is asked for.** `Fetcher::start` calls
  the observers' `standDown()` and waits in a step of its own until none is connected or
  connecting. On the board that is at once: a slot's close destroys its client and frees its
  TLS session before it returns, so the wait is 0 ms; 5 s is the bound for anything slower, and
  reaching it does not end the fetch.
- [ ] **[none] They come back at once after any ending but *fetched*,** with what was left of a
  slot's own backoff kept. After *fetched* they hold, and open by themselves after a minute if
  no switch followed (a refused switch, or `update fetch` at a bench).
- [ ] **[none] A range that stops short says why, in four short words,** because a journal
  note is 96 characters: `client N`, `tls -0xNNNN`, `block N`, `no memory`. `docs/field.md` §9
  spells each out.
- [ ] **[none] `kFileTries` is three in a row now, and a file may cost twice its ranges plus
  three requests** (51 for 0.7.5). A range that brought bytes is asked on from where it
  stopped and uses up no try.
- [ ] **[later, meshmaster.dk] A slot's state has an eighth word, `standing-down`,** and it is
  in the heartbeat's `observer.slots[]`. The contract types the state as a string and nothing
  on the server checks it against its list (`AgentObserverStates`, `Envelopes.cs` ~L79–104:
  seven words, `IsKnown` called by nobody), so the Observer card prints a word it has no
  sentence for. The list and the card want it.
- [ ] **[none] Aarhus C took 0.7.5 on its second attempt** (21 November 12:01 CET, on 0.7.4,
  with both slots up): 45 seconds of fetch, committed 12 seconds into the trial. So what
  stopped the first attempt comes and goes. Whether it was memory is still not shown; the next
  fetch that stops short on this firmware says.
- [ ] **[none] The second first attempt that failed, and what it adds (21 November).** v0.7.6
  (f7eae8f, tasks 67 and 68) was asked of both boards from meshmaster.dk at 15:21 CET, each
  still on 0.7.5 and so fetching by the old rules. Risskov: 24 requests for the file's 24
  ranges, committed 10 seconds into its trial. Aarhus C: `refused: range 719014-784549 stopped
  at 31824 of 65536 bytes (3 failure(s), which is all this fetch gets)` after 24 seconds, and
  it took the release on Thomas's retry ten minutes later. 31 824 is two records less the same
  944 bytes: 2 x 16 384 - 944, as 48 208 was three. Twice now the board with two observer
  slots, each time at a record's edge, and never Risskov. Both boards run 0.7.6, so the next
  fetch on Aarhus C is the first by task 68's rules.
