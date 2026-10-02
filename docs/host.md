# The host harness — the agent loop on a workstation

**This proves the protocol, not the radio.** Everything below runs the firmware's
own agent loop against a *fake* repeater: the claim, every handler, the credential
buffer, the session cache, the airtime budget, the result envelopes and the
console all do exactly what they do on the board, and nothing at all touches an
SX1262. Timing on the air, SNR, interference, the real region loader's behaviour
and the first second of a cold radio are not here and cannot be. The bench list
of each task's report is still the bench list.

What it is for: the Pi agent has had an end-to-end harness since phase 2 (its
`FakeCompanion`, the compose stack, `AgentEndToEndTests`), and this firmware's
handlers had only ever been exercised one function at a time by the native tests.
The board was in the post. This is what the wait was for — and it earned its keep
on the first run: see *What it found*, below.

## 1. Build and run it

```
pio run -e host                     # .pio/build/host/program
.pio/build/host/program --help
```

```
program --url http://127.0.0.1:8080 --token mma_... [--board heltec_v3] [--key <64 hex>]
        [--scenario tests/fixtures/host/two-repeaters.json] [--journal out/journal.json]
        [--idle-exits 2] [--max-seconds 1800] [--log-level debug] [--frames] [--console]
```

| | |
|---|---|
| `--url` | the server. **Plain HTTP, and only to a loopback or RFC 1918 address**; `https://` is refused, because there is no TLS stack in this build (§5) |
| `--token` | the agent token the exit node was registered with. Read for the one call that needs it and wiped after, as on the board; the host's settings store is RAM and gone with the process |
| `--board` | what the heartbeat reports as `board`, so the exit node's page says *onboard · Heltec V3* for the board that is in the post |
| `--key` | this exit node's companion public key. The default is a plainly-a-harness key; a scenario's ACL is about whatever this is |
| `--scenario` | the fake mesh (§2). Without one it is a single repeater with sensible values |
| `--journal` | where to write the fake's transaction journal as JSON (§3), which is what the two scripts assert on |
| `--idle-exits` | stop after this many claims in a row that brought no work. 2 by default |
| `--max-seconds` | stop with exit code 2 after this long with work still going |
| `--console` | read the firmware's own console from stdin: `status`, `jobs`, `sessions`, `mesh`, `server`, `errors`, `dump`, `get`, `set` — the same parser, the same replies |

Exit codes: **0** the cycle finished, **1** a bad argument or an endpoint it may not
open, **2** `--max-seconds` ran out, **70** the agent loop asked to restart (twelve
unanswered calls with the link up, `src/Health.h`) — a harness run that reboots has
found something.

On Windows there is no `g++`: build and run it in a container, which is what
`tasks/RUNBOOK.md` §2 says for `pio test -e native` too.

```bash
docker run --rm -v "C:/path/to/meshmaster-agent-firmware:/w" -w /w python:3.11-slim \
  bash -c "apt-get update -qq && apt-get install -y -qq g++ git && pip install -q platformio \
           && pio run -e host && python tools/host-stub.py"
```

## 2. The scenario file

One JSON file says which repeaters exist, what they answer, and what they will
*not* answer. The shape is `src/host/HostScenario.h`; the one the harness ships
with is [`tests/fixtures/host/two-repeaters.json`](../tests/fixtures/host/two-repeaters.json).

| Key | What it is |
|---|---|
| `exitNode.publicKey` | whose ACL entries the repeaters hold |
| `publicKey`, `name`, `ownerInfo`, `firmware`, `build`, `board`, `role` | what `ver`, `board`, `get name`, `get role` and the owner-info request answer |
| `adminPassword`, `guestPassword` | the two the login compares against. **The guest password is empty by default**, because that is what `NodePrefs()` leaves it as — and that is a behaviour, not an omission (§4) |
| `hops`, `latencyMs`, `online`, `clockOffsetS` | how far away it is, how long a round trip takes, whether it is on the air at all, and how far its own clock is from this host's (`clock.sync` exists because a repeater's clock drifts) |
| `acl` | the entries, as `{key, permissions}`; the low two bits of `permissions` are the role |
| `aclEntries` | pad the ACL to this many. **24 or more is silence**: a reply of 4 + 24 × 7 bytes is one `Mesh::createDatagram` refuses, so the request is answered with nothing and a short list is never sent |
| `prefs` | what each `get` answers and each `set` changes |
| `neighbours`, `neighbourCount` | the list, and a knob to pad it past the nine a page carries |
| `telemetry` | the Cayenne-LPP readings, by name |
| `status` | the `RepeaterStats` the status request answers; `legacy: true` is the 48-byte one |
| `regions.listing`, `.default`, `.home` | what `region`, `region default` and `region home` answer. A listing over 160 characters is cut there with no marker, which is what the firmware's reply buffer does |
| `drop` | reply names this repeater answers with **silence**: `login`, `status`, `owner-info`, `telemetry`, `neighbours`, `neighbours@<offset>`, `acl`, `trace`, `discover`, or `cli:<the whole line>` |
| `refuse` | `set` keys this repeater refuses, in the firmware's own words for that key |

## 3. The journal

Every frame the exit node sent, in order, with the sender timestamp it carried,
whether it went direct or flooded, and whether anything came back. It is how
*"every command was sent once"* and *"the timestamps strictly increase"* become
assertions rather than hopes, and it is what the console's `mesh` prints.

A line made from a password is journalled with its value replaced by one `*` per
byte — the same redaction the frame log uses (`src/agent/Credentials.h`). The
stub greps the whole run's output and the journal for every password in the
scenario and the recording, and fails if one is there.

## 4. The fake repeater

`src/host/HostMesh.cpp` is stock MeshCore firmware at the pin, read a second
time: it is written from `examples/simple_repeater/MyMesh.cpp` and
`src/helpers/CommonCLI.cpp` and **not** from meshmaster.dk's
`src/MeshMaster.MeshCore/Fake/FakeCompanion.cs`, deliberately, so the two fakes
can disagree with each other and one of them be wrong. Every reply names the
function it was read from, in the comment beside it.

It answers: the login (including the fall-through a blank password takes when the
ACL does not hold the sender, which on a repeater with no guest password is
answered **guest**); the five binary requests, each zero-padded to a cipher block
as `Utils::encrypt` pads them; the CLI, with `setperm`, the `get`/`set` table's
ranges and reply spellings, `region` and its load mode with the blank-line
terminator and the *equal timestamp is a retry* rule, `password`, `time`,
`advert` and `reboot` as silence; and a trace with a per-hop SNR. The floats of a
`get` go through a port of `StrHelper::ftoa`, so `get lat` on a repeater at 56.41
answers `> 56.4099998` and a snapshot that says `56.41` is a snapshot of
something else.

## 5. Plain HTTP, and why that is not a hole

The board's client is HTTPS with the chain verified against a bundled root, and
its URL rule refuses any other scheme (`src/settings/Keys.cpp`). The host build's
client is plain HTTP, because what it talks to is a compose stack on a loopback
port or a stub in CI, neither of which has a certificate. Three rails keep the
two apart:

1. `http://` is allowed **only** to a loopback or RFC 1918 address, checked before
   a socket is opened. A public address or a name that is not `localhost` is
   refused with the reason on the console.
2. `https://` is refused too, and says why: there is no TLS stack in this build,
   so a run that *looked* like it was talking to production would be talking to
   nothing.
3. The URL is **not** a setting. It arrives as `--url` and lives in the client;
   `meshmaster.url` still means "https://, verified" on the board and here, and
   the console still refuses anything else.

`test/test_tls_policy` greps `src/` for the scheme spelt out and allows it in
`src/host/` alone, so a line of the board's own code that named a plain-HTTP
address is a red test.

## 6. Against the recorded stub (CI)

CI cannot run meshmaster.dk: no image of it is published and its own tests need
MySQL. So [`tools/host-stub.py`](../tools/host-stub.py) replays the server from
[`tests/fixtures/server/one-cycle.json`](../tests/fixtures/server/one-cycle.json),
runs the program against it, and asserts — on the results the agent posted *and*
on the fake's journal.

```
pio run -e host
python tools/host-stub.py            # exits 0 when every assertion held
```

It asserts: every job ran and came back as the recording says (outcome, and the
transaction count where the recording names one, which is EXIT-NODES §7's table
job by job); every result's fields are its contract fixture's, so a field the
firmware invented or misspelled is a failure; every command was sent once within
its job; the sender timestamps strictly increase; the restore is the last command
of every admin session that owes one; and no password reached the console or the
journal. A stale recording fails loudly, naming the job, the field and both
values.

**Refreshing the recording** is `tools/host-run.ps1 -Record` against the compose
stack. The one thing in it that is deliberately *not* the server's is the budget:
60 a minute with no gap, so a cycle of eighty-odd transactions is a minute and
not seven hours. The pacing arithmetic itself is exercised against the stack,
where the budget is the server's own.

## 7. Against the compose stack (the workstation)

[`tools/host-run.ps1`](../tools/host-run.ps1) is the desk twin of the bench walk
of [`console.md`](console.md): it uses meshmaster.dk's compose stack beside this
repository, associates a repeater, queues a cycle of jobs, runs the host program
and then asserts on the server's side — the jobs page, the snapshots, the change
rows, the association still `active`, `aclRestored: true` — and on the fake's
side. It says so, in its own first lines, and it says what it cannot do.

**What it cannot do, and why.** meshmaster.dk's API is deliberately closed to a
script for two of the steps. By its own
[`spec/API.md` §2](https://github.com/twinoak/meshmaster.dk/blob/master/spec/API.md)
and decision D-26: a personal access token **cannot re-authenticate**, so every
*gated* action — a config apply, a CLI job, a credential rotation, issuing a
token — answers `reauth-required` to a token and can only be done in the web UI
with a fresh second factor; and `POST /api/v1/me/tokens` is an endpoint that only
ever says so (501). So the script does everything the agent API and an ungated
`config`-scoped token can do, and the apply and the rotation are walked in the
browser, with the script printing what to click and then asserting on the
result. That is not a gap in the harness; it is the server's trust boundary
working, and the harness says where it is rather than going round it.

## 8. What it found

The first run of this harness found two faults in this firmware that no native
test could have, because both live in the wiring between files:

1. **Every login reply was dropped.** `MyMesh::handleRequest` reflects the
   sender's timestamp back as a reply's first four bytes — its own comment calls
   them "kind of like a 'tag'" — but `handleLoginReq` writes the *repeater's* own
   clock there instead. `AgentMesh::sendLogin` returned its own timestamp as the
   tag to match on, so `takeResponse` dropped every login reply for having
   "another tag", and **every job that needed a session would have been
   `unreachable` on the bench**. A login is now matched on nothing, which is also
   how the .NET agent matches `PUSH_CODE_LOGIN_SUCCESS`.
2. **The server's budget never reached the airtime gate.** `AgentLoop::doConfig`
   took the budget from the config reply into its own field but never passed it
   on, and `doHeartbeat` only passes one on when the reply's *differs from that
   field* — so a server whose heartbeat agreed with its own config call left the
   gate at its constructor default of two a minute and twenty seconds. Invisible
   in production, where that default is also the server's; plain the moment an
   owner raises an exit node's budget.

Both are fixed, with the finding written beside the fix. The report of
`tasks/phase-1.1/reports/08.md` has the rest, including the table of places where
this fake and `FakeCompanion.cs` would have answered differently.
