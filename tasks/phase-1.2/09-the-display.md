# 09 — The display

**Depends on:** nothing.
**Spec:** README.md (*Provisioning it*, *What you should see*, the rails), `docs/ops/agent.md` if it describes the display, meshmaster.dk `spec/EXIT-NODES.md` §10 (online, quiet, offline); tasks p1-06 (the display, the button, the watchdog), p1-07; finding 1 in the README.
**Size:** one session.
**Model:** claude-opus-5

## Goal

The OLED goes dark five minutes after the last button press and shows, when
it is lit, four lines written for the bench rather than for the window a
board is mounted in. Thomas wants it on, toggled by the PRG button, and
saying four things: who this is and what time it is, whether WiFi and the
server are there, when the server last heard from it, and whether it is
doing anything right now.

## Deliverables

- **It stays on.** `Oled::loop()` turns the display off at `offAt_`
  (`kAutoOffMs`, five minutes). Replace the constant with a setting,
  `display.timeout`, seconds, `0` meaning never, **default `0`**, in
  `Keys.cpp`'s table with the others (`set display.timeout 300` gives
  today's behaviour back without a rebuild; the help text says so). Read at
  `Oled::begin` and again when `set` changes it, the way the radio and log
  settings are re-applied, so no reboot is needed. `Oled.h`'s header comment
  and README's display paragraph say the new default.
- **The PRG button toggles.** The button is MeshCore's `user_btn` (GPIO 0 on
  the V3), already polled in `Oled::loop()` with the one meaning "turn on and
  restart the timer". A `BUTTON_EVENT_CLICK` now toggles: off when on, on
  when off, and restarts the timeout when one is set. The other events
  `MomentaryButton` reports (long, double, triple) are left unused and
  unhandled. GPIO 0 held through a reset is the bootloader, which a click in
  the loop cannot cause; say so in one sentence in the header comment so
  nobody wonders.
- **Four lines, as a pure function.** The frame is built by one function
  with no dependency on the board - `src/ui/DisplayLines.h/.cpp`,
  `renderLines(const DisplayState&, char lines[Oled::kLines][Oled::kCols])`
  - over a plain struct the loop fills (`main.cpp`'s `fillOled` gathers it
  from `wifi`, `agentClock`, `agent`, `jobs`, the identity). The font gives
  21 characters a line (`kCols` − 1), and the wording below is chosen to fit:
  1. `ID: 46F6` left - the first four hex characters of the public key, the
     prefix meshmaster.dk shows - and the clock `hh:mm:ss` right-aligned when
     `AgentClock::hasTime()`, `no time` right-aligned otherwise. `no identity`
     alone when there is none.
  2. `WiFi: yes Online: yes` - exactly 21, so no comma. *WiFi* is
     `WifiLink::connected()`. *Online* is *the last call to MeshMaster was
     answered*: a heartbeat has happened, `consecutiveFailures == 0`, not
     `halted_`. The IP address leaves the display; `status` has it.
  3. `Heartbeat: hh:mm:ss` from `lastHeartbeatAtEpoch_`; `Heartbeat: none`
     until the first. (*Last heartbeat:* would be 24 characters.)
  4. What it is doing. While `JobRunner::claimAndRun` has a job open:
     `Job 1234 acl.set` - id and type, truncated to the line - which is live
     because `pumpDuringAJob` refreshes the display during a job. Otherwise
     `Idle, last 12:34:56` from the newest `recent_` record, `Idle, no jobs
     yet` before any. The reasons the board is *not* working, which line
     three carries today (task 06's words), move here, since nothing runs in
     those states: `token revoked/wrong` after a 401, `no server: retry 30s`
     while backing off, `no wifi` when that is why. Line two's *no* says
     something is wrong; line four says what. `JobRunner` exposes the open
     job (id and type, or none) and the newest record; `AgentLoop` exposes
     what the struct needs instead of printing a line itself, and the three
     `oledLine` methods go.
- **Tests, natively.** `test/test_display_lines/`: one case per state above
  with the exact 21-character strings - the fitting of line two, the
  right-alignment of line one with and without time, the four states of
  line four and the three faults, truncation of a long job type, a missing
  identity - and one that asserts no line ever exceeds `kCols − 1`. The
  toggle and the timeout are not reachable natively (`Oled` needs the
  board); the report says so and lists the bench checks for Thomas: click
  off, click on, `set display.timeout 10`, wait, click.
- **The build.** `pio run -e heltec_v3` and `-e heltec_v4` clean; the RAM
  and flash lines in the report beside `v0.1.1`'s.

## Acceptance criteria

- [ ] `display.timeout` is in `settings` with default `0`; `set display.timeout 300` takes effect without a reboot; `Oled::loop` never turns the display off at `0`.
- [ ] A click toggles the display; nothing else about the button changed.
- [ ] `renderLines` produces exactly the strings above for each state, every line ≤ 21 characters, under `pio test -e native`.
- [ ] `main.cpp`'s `fillOled` is the only place the struct is filled, and `oledLine` exists nowhere.
- [ ] `Oled.h`'s header and README describe the four lines, the default, the button.
- [ ] `pio run -e heltec_v3`, `pio run -e heltec_v4`, `pio test -e native` green; CI green.

## Not verified here

The display on a board: the toggle, the timeout, the ghosting over weeks.
The report lists the bench checks.

## Out of scope

The backoff and the merged-image sentences (10); the partition table (11);
a pixel shift against ghosting (the Watch list).
