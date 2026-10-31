# 30 — The uplink holds its line

**Depends on:** nothing.
**Spec:** the bench figures of 28 October (in this pile's README); `src/agent/AgentClient.cpp` (`request()`: a new `WiFiClientSecure` and `HTTPClient` per call, `setReuse(false)`, the ISRG root then the core's bundle), `src/agent/AgentClient.h`, `lib/agent-core/include/agent/IUplink.h` (what a call is to the loop: the `Call` record, how a failure is read), `src/main.cpp` (the `status` heap line, ~L230), `src/Health.cpp` (the reboot guard: a TLS stack that fails for an hour and a half), `docs/console.md`, `docs/field.md`; meshmaster.dk's agent routes (read-only): the server's keep-alive behaviour on IIS (`docs/ops/release.md`, `spec/HOSTING.md`).
**Size:** one session, long.
**Model:** claude-fable-5-1

## Goal

The bench says the MeshMaster uplink is what leaves no room for the observer:
every call builds a new TLS connection and tears it down, and with the radio,
WiFi and nothing else the free heap dips from 136 KB to 55 KB — about 80 KB —
at the call's peak. A connection that stays open pays its handshake once and
then costs a steady, smaller amount; a board whose memory is the constraint
wants the steady amount. Keep one connection to the server open between calls,
and make what every call costs a number the board reports.

## Deliverables

- **One connection, kept.** The uplink holds one `WiFiClientSecure` (and the
  HTTP client over it) across calls — keep-alive, `setReuse(true)` or the
  equivalent — so the handshake happens at the first call and again only when
  the connection is gone (the server closed it, a call failed, WiFi dropped,
  the root it verified against changed). A call that finds the connection
  dead reconnects once, inside the same call, and its failure is reported as
  today. Nothing about what the loop sees changes: the `Call` record, the
  backoff, the reboot guard and the 401 halt are the same.
- **The server keeps it.** Find out what keeps an idle connection open on
  meshmaster.dk (IIS's connection timeout; the heartbeat interval is 30 s with
  work and 120 s idle) and say in the report how long the server holds an idle
  connection, measured with `curl -v --keepalive-time` or `openssl s_client`
  from this machine against `meshmaster.dk`; if the server closes it sooner
  than the idle interval, the board reconnects per call as now and that is the
  finding, not a failure of the task.
- **What a call costs, on the console.** `status` gains, beside the heap line,
  the uplink's figures: free heap before the last call, the lowest free heap
  seen during it (sampled around the TLS connect and the body read — or from a
  wrapper of mbedtls's allocator if this core lets one be set, which is exact;
  say which), and how many handshakes since boot against how many calls.
- **A test on the host** of the reuse logic with a fake client: reuse while
  alive, one reconnect when dead, a failure after it reported as before,
  the root change forcing a new handshake.
- **`docs/console.md`** and **`docs/field.md`**: the new line, and what a
  kept connection means for a router that drops idle sessions.

## Acceptance criteria

- [ ] Calls reuse one connection on the host test; the handshake count is one per connection, not one per call.
- [ ] The loop's behaviour under a dead server, a 401 and a WiFi drop is unchanged (the existing suites, untouched).
- [ ] `status` shows the uplink's per-call figures; both board builds green, sizes in the report.

## Not verified here

The new figures on a board — Thomas, on the bench (the README's steps), before
31 sets any number from them.

## Out of scope

The observer (31).
