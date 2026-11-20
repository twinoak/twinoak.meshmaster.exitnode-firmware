# Pile 1.17 — follow-ups

Review findings, tagged with the task that must pick them up. Only the reviewer ticks an item.

- [ ] **[Thomas to decide] Nothing starts an image before it is released.** 0.7.3 was built,
  tested, signed and published as *Latest* without ever having run. Task 65 holds two causes of
  this kind (a record copied, a large frame); a deep chain of small frames it does not see. Two
  ways, not exclusive: CI starts the image in Espressif's QEMU for the S3, as the Walter's
  firmware does for its own (not tried here: whether this image gets past its radio there is
  not known); or a release is a pre-release until one board has run it.
- [ ] **[later] The least stack ever free is read at the console only.** The heartbeat's
  counters are counts that rise, and this number falls, so it does not belong among them.
- [ ] **[later] What the loop task really uses.** The numbers Thomas reads off his board after
  task 65 go here: after ten minutes online, and after a fetch. They say whether 16 384 is
  generous or needed.
