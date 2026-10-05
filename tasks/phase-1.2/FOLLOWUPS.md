# Pile 1.2 — follow-ups

## Review of 09–11 (5 October, morning) — the pile, after it landed

Read after PR #13 merged (the driver finished at 00:34), against the three
reports and the code they name. 09 and 10 ran in the evening, 11 took thirty
minutes on Fable; no stop. Nothing here has met a board yet: the bench walk
is below, and it is the first thing to do.

**11 — the partition table** is right as far as a file can be. Both tables
are the framework's defaults with `nvs` moved to `app1`'s old offset
(`0x340000` on 8 MB, `0x650000` on 16 MB), `app1` gone with the reason
written, the rest of its region unallocated, and `otadata`, `app0`, `spiffs`
and `coredump` byte-identical to the defaults - `tools/check-partitions.py`
holds that against the *installed* framework on every CI build and against
the real merged image's length (`0x15AF70` on the V3, far below `0x340000`).
The recovery on a blank or foreign NVS region is confirmed where it actually
lives: `initArduino()` (`esp32-hal-misc.c`), which runs before `setup()` and
erases-and-retries on `NO_FREE_PAGES` and `NEW_VERSION_FOUND`, not
`Preferences.begin` as the task guessed. README's *Flashing it* now says which
flash forgets the settings and when, and that the identity never goes.

**10 — the small ones**: a 502/503/504 that names `Retry-After` is honoured
as the 429 is and counts as neither a consecutive failure nor one of the
reboot guard's twelve; the backoff's ceiling is two minutes once WiFi is up
and the clock came from SNTP or the server, ten otherwise; `neighbours` is
answered at a guest login with the source line cited; `test_server_policy`
exists for the first time, 156 lines.

**09 — the display** made the frame a pure function (`renderLines` over
`DisplayState`), tested natively for every state and for the 21-character
width, which is what let the bench checks be a short list. Two gaps, below.

### The bench walk (Thomas, before tagging a release)

From 11's report, in the order that costs least if something is wrong, the
identity's first eight hex characters written down before each step:

1. **The application image over the old table.** `pio run -e heltec_v3 -t
   upload` on the provisioned board. It must come back with WiFi, URL and
   token, `status` the same identity, a heartbeat. Proves an upgrade is still
   an upgrade - and shows the new display.
2. **The first merged flash, without an erase.** `esptool.py --chip esp32s3
   write_flash 0x0 firmware-merged.bin`. Expected: the banner, the same
   identity, `wifi off`, `server not configured`, and **no** `Failed to
   initialize NVS` line. Then *Generate a new token* on the exit node's page,
   `set wifi.ssid`, `set wifi.pass`, `set meshmaster.token`; the page must
   show the same companion key and the node `online` without *companion
   changed*, and a job through a repeater whose ACL holds the board must come
   back `ok`. A different identity at any point is the one failure this pile
   exists to prevent: stop.
3. **The second merged flash.** The same line again. Expected: WiFi, URL and
   token intact, a heartbeat within a poll interval. This is the whole point.

The release that carries the table is a `v0.2.0`, not a `v0.1.2`: it
re-provisions a board once.

### What is open

- [ ] **The rotation probe** (meshmaster.dk task p4.7-95's finding, written
  there in this firmware's own terms): `JobRunner::ensureSession` is the
  .NET agent's loop before 95 - two `loginOnce`, `NoReply` twice is
  `Outcome::Unreachable` - so a wrong *new* candidate is reported
  `unreachable` and CONFIGURATION §7 step 4's fallback never happens for an
  onboard exit node. The change: `Context` gains `silentAfterReachable`,
  `probeAnsweredRole`, `probeRefused`; a `probeReachable` sends a blank
  login (`sendLogin("", tag)`, one transaction through the gate, no session
  stored) when the first candidate try was silent; a second silence after an
  answered probe is reported as the refusal it is, with the probe's answer
  in the result's `data`, matching the .NET agent's `LoginSilentResult`.
  The fake repeater of 08's harness already answers a blank login as the
  firmware does. Medium; it is the next firmware pile's first task, and it
  wants 08 merged first so the host harness can prove it.
- [ ] **Line four has no *not configured* state** (09): `faultOf` knows a
  refused token, no WiFi and a backoff; a board with WiFi up and no token -
  exactly the state after the merged flash of 11 - says *Idle, no jobs yet*
  under *Online: no*. One more fault, `NoToken`, reading *no token: set it*
  (17 characters), ahead of the backoff in the order. Small.
- [ ] **A `Retry-After` hold is invisible to the display** (09 + 10):
  `online()` is `everHeartbeated && consecutiveFailures == 0 && !halted`, and
  a 503 with `Retry-After` is by design not a consecutive failure - so during
  a deploy the OLED says *Online: yes* and *Idle*, while the server is
  answering 503. `online()` should also be false while `holdUntil_` is in the
  future, and `DisplayState.backingOff` should be `retrySeconds() > 0`
  rather than the failure count, so line four says *no server: retry 45s*
  through the hold. Small; the native test for `renderLines` already has the
  state.
- [ ] **08 is still stopped** on its branch for Thomas's four clicks
  (`tasks/phase-1.1/logs/STOPPED-08.md`). It now rebases over 09's
  `main.cpp` (the `fillOled` rewrite) and master's `AgentMesh` as well; the
  resume session should merge master first and expect those two.
- [ ] **The merged image's `0xFF` fill still covers `otadata`** at `0xE000`
  - harmless, since `boot_app0.bin` is written there by the same merge and
  the bootloader wants exactly that - but worth one sentence in
  `boards/README.md` so nobody moves `otadata` to "protect" it.

### Watch

- The OLED after a month lit: an SSD1306 ghosts; the fix if it shows is a
  pixel shift every few minutes, not a timeout.
- The next meshmaster.dk deploy: the board should be back within two
  minutes of the server (10), and the display should say *retry* through it
  once the box above is done.
