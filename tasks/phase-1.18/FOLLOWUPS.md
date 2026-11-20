# Pile 1.18 — follow-ups

Review findings, tagged with the task that must pick them up. Only the reviewer ticks an item.

- [ ] **[later, meshmaster.dk] Nobody is told that an exit node has stopped hearing.** On
  19 November it was found the next day, from the holes in two repeaters' charts. The server
  has what would say it: the heartbeat's `overheard` counter, kept with its history, stops
  rising; and every read through one exit node failing while the same repeaters answer another.
  That night's `overheard` history would also show whether the receiver was the cause
  (`GET /api/v1/agents/2/counters`, which wants a token; the page shows the newest fifty
  changes only).
- [ ] **[Thomas to decide] Four seconds.** It is the repeaters' value from the standard
  profile, taken because the fleet runs with it. An exit node asks and waits where a repeater
  only relays; nothing here measured what a reset costs a reply that is on its way.
- [ ] **[later] The two findings this pile is made from** stand in pile 1.17's `FOLLOWUPS.md`
  and are ticked there when 66 and 67 have been reviewed.
