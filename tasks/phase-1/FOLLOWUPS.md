# Phase 1 — follow-ups

## Review of 01–06 (29 September, 06:10) — the phase, after it landed

Read after PR #8 merged, against the six reports and the tree: the rails
first (`grep` for insecure TLS finds nothing; `TlsRoots.h` is ISRG Root X1
with the chain the day it was read; `meshmaster.token`'s key row says
*never printed* and nothing prints it; every apply command takes
`nextTimestamp()`; fourteen native test suites; CI once per commit with
the two checks). No board was flashed by anybody yet — every report's
*Not verified* list is the bench walk, and `docs/console.md` steps 1–18
are the same list in walking order.

**01** — the three inherited decisions are the right ones: the token in
NVS under a 15-character key with `WiFi.persistent(false)` so there is
one copy on flash; ISRG Root X1 first and the core's bundle only on a
verify failure (the client cannot do both at once); the console echoes
stars for the two secrets and `set` answers the key alone. `erase
confirm` keeps the identity, correctly — a new key would flag the exit
node as *companion changed*. MeshCore's headers as system headers for
`-Werror`, with the reason `-isystem` cannot be used on Windows.

**02** — milestone 1 as specified, and stronger than asked: only an
advert reaches MeshCore's dispatch, so no payload is ever decrypted and
no ECDH computed for a stranger's packet; RSSI taken at `logRx` while it
is still the packet's; a 64-entry compile-time ring; JSON streamed by
hand so a batch cannot fragment the heap and doubles print the way
System.Text.Json prints them. `companionFirmware` is the MeshCore *tag*,
honestly. A name is never in a log line.

**03** — the important architectural call: `AgentMesh` is a `mesh::Mesh`,
not a `BaseChatMesh`, because the latter has four paths that put a packet
on the air by themselves (ACKs, path returns). One packet per transaction
is the rail and this is what keeps it true; the four primitives are
written out and `docs/upstream.md` lists them for a bump. A password-class
job is `unsupported` at no transaction until 04; a key not in the ACL is
`unreachable` (stock firmware answers silence), which is also what the
Pi agent sees against a real radio.

**04** — the credential is parsed out of the claim's bytes into one fixed
buffer, wiped whatever the outcome, read by one thing; the redaction
stars the AES body since there are no clear password bytes on the air;
the restore is not sent into a silence (one transaction fewer than the
Pi, same end state by the next login). A direct neighbour's known path
is sent DIRECT. Right.

**05** — one send per command, strictly increasing timestamps, `expect`
per line as p4-57 built it, and one deliberate exception to "nothing
past a lease": the region loader's terminator, because a repeater left
in load mode reads every later command as a region name. Correct
priority. A plan the firmware cannot hold fails the job, not the claim.

**06** — one thread, one watchdog at 90 s fed through every job wait,
honestly described; the failure count only against a live WiFi link
(a router that is off is not a wedged stack); the heartbeat sent from
inside a long job, without which every multi-transaction job took the
board off the page as *quiet*; the V4's transmit-power fallback 10 dBm
because its FEM adds 12. RTC-memory reboot reasons.

Open, carried forward:

- [ ] **The bench walk is the whole of what is unverified**: `docs/console.md`
  steps 1–18 in order — flash and provision (01), the exit node online
  with `onboard · Heltec V3` and unknown nodes arriving with `tx 0` (02),
  the association through the board and Read now with the transaction
  counts (03/04), the first apply through the board per meshmaster.dk's
  `FIRST-APPLY.md` (05), the watchdog, the four resets and an outage (06).
  Note the two heap figures (after the TLS handshake; after an hour) in
  this file. **meshmaster.dk must be released first**: production is still
  the phase-3 build, which knows neither `onboard` (task 66) nor the ACL
  level in the claim envelope (task 60).
- [ ] **Sync the contract fixtures** to meshmaster.dk `b04799f` (pile 4.3's
  task 71 added seventeen envelope files; the manifest here names
  `f518ebd1`), and have `test_envelopes` read `ObservationBatch.json` and
  `ClaimResponse.json` instead of its inline expectations, as 02 and 03
  asked for.
- [ ] **`StatusPayload.Version`** (meshmaster.dk task 68): a nullable flag
  the server sets to `false` when the daily version read is not wanted;
  ignoring it is correct but costs an owner-info request on every acl
  status read from a board. One `if` in the status handler.
- [ ] **The loop blocks during an HTTPS call** (02): adverts in that window
  are missed, not queued. 06 kept one thread deliberately; the connection
  kept alive (`setReuse`) or the radio on its own task is the next
  architecture question, to be decided from the bench's heap figure.
- [ ] **The watchdog has never fired** (06): one build with a deliberate
  hang proves the reboot and the RTC reason; not the image that ships.
- [ ] **Delete the merged task branches** on origin (`task/p1-02` … `-06`
  are still there); the meshmaster.dk driver deletes its own, this copy
  should too.
- [ ] **`AgentConfig` has no contract fixture** (01) — meshmaster.dk's side,
  with the envelope fixtures above.
- [ ] **The backoff after a server deploy** (bench, 3 October): a release on
  meshmaster.dk cost five failed calls and the doubling wait (30 s … 480 s)
  then outlasted the outage by a quarter of an hour, so the exit node read
  *quiet* long after the server was back. Honour `Retry-After` on the
  deploy's 503s, and once the link has been proven up (WiFi connected, SNTP
  answered) cap the wait lower than ten minutes — two, say — because a server
  that is back deserves a call within minutes, not within the cap.
  `AgentLoop::noteFailure`, `ServerPolicy.h`'s `kMaxBackoffMs`. Small.
- [ ] **The merged image erases the settings** (bench, 4 October): Thomas
  re-flashed `v0.1.1`'s `-merged.bin` over a provisioned board and lost WiFi,
  URL and token. Not a bug in the firmware and not the web flasher's doing:
  `esptool merge_bin` fills the gap between the partition table (`0x8000`)
  and `boot_app0` (`0xE000`) with `0xFF`, and `default_8MB.csv` puts NVS at
  `0x9000–0xDFFF` — inside the span. The application image at `0x10000` and
  `pio run -t upload` (separate regions) leave NVS alone; the identity in
  SPIFFS sits above the app and survives either way, which is why the
  repeater's ACL entry still holds. README.md's "flashing does not touch the
  settings" is true of those two and false of the merged image. Two fixes,
  pick one: **(a)** say so — the release notes and README name the merged
  image as *for a board out of the box only* and the application image as
  the upgrade, and the web flasher's step says which file; or **(b)** a
  custom partition table with NVS moved above the app (the merged image then
  ends at the app's end and never reaches it), `spiffs` kept at its present
  offset so the identity survives the move, one re-provisioning on the day
  it lands (WiFi and token re-entered; the token is a new one, rotated on the
  server first). Not (c) skipping the merged image: it is the only way onto a
  board that is not already running this firmware - out of the box, or
  coming from the repeater image as Thomas's V3 did - because the application
  image presumes the bootloader and our partition table are there. (b) is
  the real fix, after which one file serves both the first flash and the
  upgrade; (a) is a sentence and should land first either way. Small.
- [ ] **The display stays on** (Thomas, 4 October: "i'd like that the display
  on the heltec doesn't turn off"). Today `Oled::loop()` turns the OLED off
  five minutes after the last button press (`kAutoOffMs` in `Oled.h`,
  `offAt_`), and the button brings it back. Change the default to *never
  off*: a setting `display.timeout` in seconds, `0` meaning never, default
  `0`, read in `Oled::begin` and on `reconfigure` so `set display.timeout
  300` restores today's behaviour without a rebuild. The PRG button
  (MeshCore's `user_btn`, GPIO 0 on the V3, already read in `Oled::loop`)
  becomes a toggle: a click turns the display off when it is on and on when
  it is off, and restarts the timeout if one is set. `MomentaryButton`
  also reports long, double and triple presses; leave them unused for now.
  `Oled.h`'s header comment and README's display paragraph say the new
  default and the button. One thing to
  know, not a reason to decide otherwise: an SSD1306 showing the same four
  lines around the clock ages the lit pixels faster than the dark ones, so
  over months the frame ghosts; the one-second re-render does not help
  with that. If it ever shows, the cheap mitigation is shifting the text a
  pixel every few minutes, not a timeout. Small; not reached by the native
  tests or the host harness, so the bench proves it.
- [ ] **The display's four lines** (Thomas, 4 October; same task as the one
  above). Replace `fillOled` in `main.cpp` and the three `oledLine`s it
  calls. The font gives 21 characters a line (`Oled::kCols` − 1), which
  decided the wording below.
  1. `ID: 46F6` left — the first four hex characters of the public key, the
     prefix MeshMaster shows — and the clock `hh:mm:ss` right-aligned when
     `AgentClock::hasTime()`, `no time` otherwise. `no identity` alone on
     the line when there is none.
  2. `WiFi: yes Online: yes` — exactly 21, so no comma. *WiFi* is
     `WifiLink::connected()`; *Online* is "the last call to MeshMaster was
     answered": a heartbeat has happened and `consecutiveFailures == 0` and
     not `halted_`. The IP address leaves the display; `status` has it.
  3. `Heartbeat: hh:mm:ss` from `lastHeartbeatAtEpoch_` (`Last heartbeat:`
     would be 24 characters); `Heartbeat: none` until the first.
  4. Whether it is working: while `JobRunner::claimAndRun` has a job open,
     `Job 1234 acl.set` (id and type; the display refreshes from
     `pumpDuringAJob`, so this is live), and otherwise `Idle, last 12:34:56`
     from the newest `recent_` record, `Idle, no jobs yet` before any. The
     runner exposes the open job (id, type) and the newest record for it.
     The reasons the board is *not* working, which line three carries today
     (task 06), move here, since nothing runs in those states anyway:
     `token revoked/wrong` after a 401, `no server: retry 30s` while backing
     off, `no wifi` when that is why. Line two's `no` says that something is
     wrong; line four says what.
  Oled.h's header comment describes the new frame. Bench only, as above.
- [ ] **`neighbours` at guest** (meshmaster.dk task p4.7-94, 4 October): the
  server now runs the neighbour-list job at class `guest` for a guest-only
  pair, because `REQ_TYPE_GET_NEIGHBOURS` answers any login at `d9296435`
  (its `handleRequest` checks `isAdmin()` for the access list only). This
  firmware refuses it: `JobEnvelopes.cpp:72` requires admin for `neighbours`,
  so a board would answer `unsupported` where the Pi agent answers the list.
  Drop the check to the same rule the server and the firmware source have -
  status, telemetry and neighbours at any login, the access list at admin -
  and add the case to the host harness's fake. Small.
