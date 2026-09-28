# 06 — A node that stays up

**Depends on:** 05.
**Spec:** [EXIT-NODES §5, §10](https://github.com/twinoak/meshmaster.dk/blob/fbf4625a0352663e1c26bd6877e45e6c555d4f7c/spec/EXIT-NODES.md), [docs/ops/exit-node-field.md](https://github.com/twinoak/meshmaster.dk/blob/fbf4625a0352663e1c26bd6877e45e6c555d4f7c/docs/ops/exit-node-field.md), [docs/ops/agent.md](https://github.com/twinoak/meshmaster.dk/blob/fbf4625a0352663e1c26bd6877e45e6c555d4f7c/docs/ops/agent.md).
**Size:** one session.
**Model:** claude-opus-5

## Goal

The Pi agent is a systemd service with a journal, an offline buffer and a
person who can ssh in. The board has none of that, so this task gives it
what a device on a windowsill needs to run for a year: a watchdog, honest
reconnection, results and observations that survive the server being away,
a way to read what happened, the Heltec V4 target, and the runbook.

## Deliverables

- **Watchdog and health**: the task watchdog on the main loop and the
  network task; a reboot on a wedged TLS stack after a bounded number of
  consecutive failures, counted in the heartbeat's `errors` and shown on
  the console after the reboot (the reason kept in RTC memory).
- **Buffers**: results and observation batches kept in RAM while the
  server is away, bounded, oldest dropped and counted; a job's result
  posted late is still posted (the server takes late results); the lease
  respected — a job whose lease passed while the server was away is not
  re-run.
- **Reconnection**: WiFi and server with backoff and jitter; the OLED says
  which of the two is missing; the mesh keeps running and hearing while
  the network is down.
- **The console**: `errors` (the counters), `reboots` (the last five
  reasons and times), `dump` (the last 4 kB of the log ring, redacted).
- **The Heltec V4 target**: `heltec_v4` as a second environment when the
  pinned MeshCore has a board definition for it; if it has not, the report
  says what the variant needs and whether moving the pin forward is safe
  for everything the earlier tasks read from the source at `d9296435`.
- **Docs**: `docs/field.md` — the bench provisioning, the window placement,
  what the OLED's four lines mean, what to read on the console when the
  exit node's page says *quiet*, and how to pull the log ring; the project
  README's rails list checked against the tree.

## Acceptance criteria

- [ ] Native tests: the buffers' bounds and drop counting; a late result is posted and a lapsed lease is not re-run; the backoff sequence.
- [ ] `pio run -e heltec_v3` (and `-e heltec_v4` when it exists) green in CI.
- [ ] The grep test for insecure TLS and the redaction tests of 01/04 still pass; the rails list matches the tree.

## Not verified here (Thomas, with the board)

A week at the window; pull the log ring after it; the heap and the reboot
counters.

## Out of scope

OTA of this firmware (a decision, later; the board is on a USB cable at a
window); a captive portal (not planned).
