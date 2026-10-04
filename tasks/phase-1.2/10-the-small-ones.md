# 10 — The small ones

**Depends on:** nothing.
**Spec:** `src/agent/ServerPolicy.h` (the policy as written), meshmaster.dk `spec/API.md` §3 and `spec/EXIT-NODES.md` §7 (the credential class per job type, as task p4.7-94 corrected it: *"the binary neighbours request answers any login at `d9296435`"*) and §10 (quiet); README.md *Flashing it*; `.github/workflows/release.yml` (the release notes); tasks p1-06, p1-07; findings 2, 3 and 4 in the README.
**Size:** one session, short.
**Model:** claude-opus-5

## Goal

Three boxes from the first day on the air: a backoff that outlived the
outage it was waiting out, a job class the server now sends and this
firmware refuses, and a sentence in the README that the bench proved false.

## Deliverables

- **The backoff after a server deploy.** A release on meshmaster.dk answers
  503 for a minute or two. `ServerPolicy` classifies that as `BackOff`
  (30 s doubling to `kMaxBackoffMs`, ten minutes), and `WaitRetryAfter` is
  the 429's alone; so five failed calls during a deploy left the board
  waiting 480 s with the server long since back, and the server read it
  *quiet*. Two changes. **Honour `Retry-After` on a 503** (and 502, 504)
  when the answer carries one, as the 429 path does - `AgentClient::Call`
  already parses the header into `retryAfterS`; the policy reads it for
  these statuses too, bounded by `kMaxRetryAfterMs`. **Cap the backoff
  lower once the link is proven**: when WiFi is connected and SNTP has
  answered, the board's own link is not the problem, so the cap is
  `kMaxBackoffWhenLinkUpMs` = two minutes; `kMaxBackoffMs` stays the cap
  for a board whose link is not proven (a WiFi that associates and goes
  nowhere, a captive portal). `AgentLoop::noteFailure` picks the cap. A
  heartbeat answered resets the backoff as it does today. Tests in a new
  `test_server_policy` (the policy is a pure function and has had none):
  the 503 with and without `Retry-After`, the two caps, the 429 unchanged,
  the 401 still a halt.
- **`neighbours` at a guest login.** `typeAllowsClass` in
  `JobEnvelopes.cpp` allows `neighbours` at `Acl` and `Admin` only; the
  firmware source this project pins answers `REQ_TYPE_GET_NEIGHBOURS` to any
  login (its `handleRequest` checks `isAdmin()` for the access list and for
  nothing else), and meshmaster.dk's task 94 now sends the job at class
  `guest` for a guest-only pair. Allow `Guest` for `neighbours`, as
  `status` and `telemetry` already are; the comment names the source line
  it is read from. A case in `test_envelopes` (or wherever `canRun` is
  tested) for the three classes. Check whether 94 changed a contract fixture
  (`tools/sync-fixtures.ps1 -Commit <meshmaster.dk master> -Verify`); if it
  did, sync it in this task and say which file.
- **The merged image is the first flash, not the upgrade.** README's
  *Flashing it* says *flashing does not touch the settings (NVS) or the
  identity (SPIFFS)*, which is true of the application image at `0x10000`
  and of `pio run -t upload` and **false of the merged image**: `esptool`'s
  merge fills the gap between the partition table (`0x8000`) and `boot_app0`
  (`0xE000`) with `0xFF`, and `default_8MB.csv` keeps NVS at `0x9000`, inside
  it. The identity in SPIFFS sits above the application and survives either
  way. Rewrite the paragraph: the merged image for **a board out of the box
  or one coming from other firmware**, erasing the settings as it goes; the
  application image for every upgrade, keeping them; `erase confirm` as
  before. The release notes `release.yml` writes say the same in two
  sentences beside the two files, and the SHA256SUMS line stays. Whether the
  MeshCore web flasher can be given the `0x10000` offset for the application
  image is a question for Thomas, not for this task: say so in the report.
  Task 11 then makes the sentence about the merged image untrue in the good
  direction, and rewrites it again.

## Acceptance criteria

- [ ] A 503 with `Retry-After: 90` holds for 90 s and does not double; a 503 without one backs off, and with WiFi connected and SNTP answered the wait never exceeds two minutes; without a proven link it may reach ten.
- [ ] A 429 behaves exactly as before; a 401 halts.
- [ ] `canRun` accepts a `neighbours` job at class `guest`, `acl` and `admin`, and refuses `config.read` at `guest` as before; the contract fixtures verify.
- [ ] README's *Flashing it* and the release notes name which image erases the settings and which does not.
- [ ] `pio run -e heltec_v3`, `pio run -e heltec_v4`, `pio test -e native` green; CI green.

## Not verified here

A real deploy's 503s (the next release of meshmaster.dk is the test: the
board should be back within two minutes of the server), and a real
repeater answering `neighbours` to a guest login.

## Out of scope

The partition table (11); the display (09); the watchdog proof build.
