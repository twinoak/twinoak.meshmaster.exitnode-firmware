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
