# 01 — The board speaks

**Depends on:** the repository (pile 4.2, task 65).
**Spec:** [EXIT-NODES §5, §6](https://github.com/twinoak/meshmaster.dk/blob/fbf4625a0352663e1c26bd6877e45e6c555d4f7c/spec/EXIT-NODES.md), [API §3](https://github.com/twinoak/meshmaster.dk/blob/fbf4625a0352663e1c26bd6877e45e6c555d4f7c/spec/API.md), [SECURITY §6](https://github.com/twinoak/meshmaster.dk/blob/fbf4625a0352663e1c26bd6877e45e6c555d4f7c/spec/SECURITY.md), [DECISIONS D-23, D-28](https://github.com/twinoak/meshmaster.dk/blob/fbf4625a0352663e1c26bd6877e45e6c555d4f7c/spec/DECISIONS.md).
**Size:** one session.
**Model:** claude-fable-5-1

## Goal

A Heltec V3 that boots this firmware, takes its settings over the USB
serial console, joins WiFi, sets its clock, and makes one authenticated
HTTPS call to meshmaster.dk — `GET /agent/v1/config` — with the certificate
chain verified. Nothing on the air yet: the radio is initialised so the
identity exists, and no packet leaves. It is Fable's because this task
decides where the token lives, how the server's certificate is checked and
what the serial console will and will not print, and every task after it
inherits those three decisions.

## Deliverables

- **The build** — **pile 4.2's task 65 left this one done**, and its
  report is the one to read before touching `platformio.ini`: MeshCore is a
  **dependency** pinned at `d9296435`, `pio run -e heltec_v3` and
  `pio test -e native` are green, and CI runs both once per commit and keeps
  the `.bin`. What is left here is to make the build ours: the environment
  builds MeshCore's stock `companion_radio` example today (the
  `BUILD_EXAMPLE` and `MC_UI_FLAVOR` defines), and this task replaces it
  with our application in `src/`, which is where those two defines go away.
- **Identity and radio.** MeshCore's identity on first boot (kept in the
  file system as the companion firmware keeps it), the SX1262 initialised
  with the Danish channel from the settings (`radio.freq`, `bw`, `sf`,
  `cr`, `tx` — the bench sets them, like a repeater), and the node in
  **companion** role: it does not repeat. `status` on the console prints
  the public key, the radio parameters and the firmware string
  (`meshmaster-agent <version>+<sha> on MeshCore <tag>`).
- **The serial CLI** (115 200, line-oriented, `>` prompt): `set <key>
  <value>`, `get <key>`, `get` (everything), `status`, `wifi` (scan and
  state), `reboot`, `erase` (with a confirmation word), `help`. Keys:
  `wifi.ssid`, `wifi.pass`, `meshmaster.url` (`https://…`, the scheme
  required), `meshmaster.token`, `radio.*`, `log.level`, `log.frames`
  (off by default). Stored in NVS under one namespace; `get wifi.pass` and
  `get meshmaster.token` print `set` or `unset` and never the value; `get`
  prints the same. The parser is a pure function with a native test per
  line shape and per refusal.
- **WiFi and time.** Station mode from the settings, reconnect with backoff,
  the state on the console and on the OLED's first line; SNTP against the
  pool, and the server's `serverTime` from the config reply as the
  fallback when NTP is blocked — the clock is what every login and every
  sender timestamp depends on (EXIT-NODES §5 "Clock").
- **TLS.** An HTTPS client (ESP-IDF's `esp_http_client` or the Arduino
  core's, whichever the build already links) with the chain verified
  against a bundled ISRG Root X1 and the core's certificate bundle;
  `Authorization: Bearer <token>` and `X-Agent-Version` on every request;
  the reply parsed with ArduinoJson into a struct that mirrors
  `HeartbeatReply`/the config answer field for field. A native test parses
  the same JSON the .NET fixtures carry. `grep -rn "setInsecure\|INSECURE\|verify=false"` of the tree finds only the test that forbids it.
- **The OLED**: four lines — WiFi state and IP, time or *no time*, server
  state (*config ok 12:03:14*, or the HTTP status), and the identity's
  first eight hex characters. Off after five minutes without a button
  press, on with the button.
- **Docs.** `README.md` with flashing (`pio run -t upload`, and the
  `esptool` line for a `.bin` from CI), the console session that provisions
  a board from nothing, and the rails list from the project README.

## Acceptance criteria

- [ ] `pio run -e heltec_v3` and `pio test -e native` green locally and in CI; the workflow runs once per commit.
- [ ] The CLI's native tests cover every key, both secret keys' redaction, a malformed line, an unknown key and a value out of range (`radio.tx` above 22).
- [ ] The config-reply parser round-trips the .NET fixture JSON.
- [ ] No insecure TLS setting anywhere in the tree (the grep test).
- [ ] The README's provisioning session is what the console actually accepts, line for line.

## Not verified here (Thomas, with the board)

Flash, provision, watch the console reach *config ok* against meshmaster.dk
with a token issued at `/exit-nodes/new`; note the heap after the TLS
handshake (`status` prints it) — the number every later task budgets from.

## Out of scope

The mesh loop, heartbeat, observations (02); a captive portal (not
planned); OTA.
