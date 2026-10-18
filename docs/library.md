# agent-core — the library, for a firmware that is not this one

This is the page for a second firmware. You have an ESP32 (or something like
one), a mesh you can reach somehow, a way of making an HTTPS call, and you want
your unit to be a MeshMaster agent on meshmaster.dk: registered, heartbeating,
answering the jobs the server hands out. All of that is written, tested and
proven on the air in this repository, and since pile 1.5 the portable half of it
is a library you can take at a tag: [`lib/agent-core/`](../lib/agent-core/library.json).

You write three classes. The library is everything else.

Nothing here requires reading this repository's `src/`. Where a sentence below
would be easier to believe with the Heltec's own implementation open beside it,
the file is named — but it is named as an example, not as a thing to copy.

## 1. What it is, and what it is not

**It is the agent.** Thirteen job types and their rules; the login sequence and
its four classes; the ACL restore that ends an admin session; the config read and
the config apply engines; the session cache; the airtime budget; the agent API's
envelopes and result shapes; the reply codec that reads what a stock MeshCore
repeater answers; the observation ring; the offline result buffer; the rate
limiter; the backoff and the policy that reads the server's status codes; the log's
formatting; and the loop that drives all of it — for ever (`loop()`) or one wake at
a time (`wake()`).

**It is not your hardware.** It has no uplink, no mesh, no storage, no clock, no
console and no main. It includes nothing of Arduino, the ESP-IDF, WiFi, NVS,
lwIP, FreeRTOS or MeshCore — `test/test_library` is that boundary, as an allow
list over every `#include` in the library — and its one dependency is
ArduinoJson 7.4.3.

Concretely, these are yours:

| Yours | Why it cannot be the library's |
|---|---|
| the mesh (`IMesh`) | in this repository it is MeshCore in the same process; on a repeater-v2 unit it is a RAK across a UART. Same calls, nothing else in common |
| the uplink (`IUplink`) | WiFi and `WiFiClientSecure` here, an LTE-M modem with its own TLS stack there |
| the platform (`IPlatform`, `IClock`) | `millis()`, a delay, random bits, the heap, the wall clock, and the one drastic thing a board can do to itself |
| where a log line goes | one function pointer, `mm::logSetSink` |
| storage | the library reads no setting and writes no file. Everything it needs comes in as a value: the token is your uplink's, the identity and the radio parameters are a `HeartbeatIdentity` you fill. The one thing it will *hand you* to store is the image a sleep keeps (`IRetained`, §5.1) — optional, and only a device that sleeps wants it |
| the console, the display, a watchdog, sleep | yours entirely. The library's console *reports* (`jobs`, `sessions`, `server`, `errors`) write to a `TextOut` you implement if you want them |

## 2. Taking it

A tag `vX.Y.Z` of this repository is the pin; the version in
`lib/agent-core/library.json`, `lib/agent-core/idf_component.yml` and
`agent/Version.h` is the same number, and `test/test_library` fails if one of
them moves alone. [`docs/upstream.md`](upstream.md)'s *This repository is an
upstream too* is the page; the short form is:

```yaml
# ESP-IDF: the component manager, in your idf_component.yml
dependencies:
  twinoak/agent-core:
    git: https://github.com/twinoak/twinoak.meshmaster.exitnode-firmware.git
    path: lib/agent-core
    version: v0.2.0
```

```ini
; PlatformIO
lib_deps = https://github.com/twinoak/twinoak.meshmaster.exitnode-firmware.git#v0.2.0
```

**Pin the commit the tag names, not the tag**, if your dependency manager lets
you say either: a tag is a pointer and a pointer can be moved, and a lock file
that records a commit is a lock file. `git rev-parse v0.2.0` is the commit, and
it is also what the fixtures' manifest and your own sync manifest should agree
with — one version in two places is how a pin drifts.

Under the ESP-IDF, `lib/agent-core/CMakeLists.txt` registers the library as a
component: one include directory (`include`), 23 sources, and an empty
`REQUIRES` — it needs no component of yours, which is what the three interfaces
are for. ArduinoJson is declared in `idf_component.yml` as a managed component
(`bblanchon/arduinojson: "==7.4.3"`) and the component manager adds it to this
component's requirements itself.

Every include is `<agent/X.h>`:

```cpp
#include <agent/AgentLoop.h>
#include <agent/JobRunner.h>
#include <agent/IMesh.h>
#include <agent/IUplink.h>
#include <agent/IPlatform.h>
```

**The ESP-IDF build of the component is unverified** at `v0.2.0` — there is no
ESP-IDF in this repository's toolchain. What has run is the CMake file parsed and
held against the sources on disk (`tools/check-idf-component.cmake`), and every
source compiled on a host with an IDF consumer's strictness,
`-Wall -Wextra -Werror -fno-exceptions -fno-rtti`, in the five configurations
below (`tools/check-library-host.py`). The first real IDF build is the first
consumer's, and the two things to expect there are in
[`docs/upstream.md`](upstream.md)'s last section.

The library is built with `-Wall -Wextra -Werror` wherever it is built: those
flags are in `library.json`'s `build.flags`, so a warning in it is your build's
error too. It is written to C++11 and is compiled here as `gnu++11` on the board
and `c++17` on the host; it carries no `-std=` of its own, so the standard is
yours to choose.

**Which dialects and levels are proven.** The whole library, source by source,
on CI's Linux runner — whose GCC is the version the job's log names, 13.3 today —
in five configurations, every one of them with
`-Wall -Wextra -Werror -fno-exceptions -fno-rtti` and nothing but
`lib/agent-core/include` and ArduinoJson on the include path:

| | `-O0` | `-O2` |
|---|---|---|
| `-std=c++17` | proven | proven |
| `-std=gnu++17` | proven | proven |
| `-std=c++17`, AddressSanitizer + UndefinedBehaviorSanitizer | — | proven |

and `gnu++11`, by the board build itself (that is the dialect the
Arduino-on-ESP32 framework compiles the whole image in). Both dialects are in
the list because a GNU one **predefines names a strict one does not** — `unix`
and `linux` on a Linux host — and an identifier of the library that collides
with one is not a warning but a file that stops being code; the first consumer
found a parameter named `unix` that way, and the check now holds every
identifier of the library against the compiler's own list of predefined macro
names. Both levels are in the list because about half of GCC's warnings are
raised by the middle end *after* inlining and are unreachable at `-O0`. The
sanitizer line is the configuration a consumer's host tests are built in, and it
is the only one of the five that raises ArduinoJson 7.4.3's own
`-Wmaybe-uninitialized`; `agent/Json.h` is where that one warning is turned off,
for the dependency's code and nothing else. Nothing in the library carries
`-Wno-error`: your build keeps every warning of ours.

## 3. `IMesh` — one repeater at a time

`agent/IMesh.h`. This is the public surface of this repository's `AgentMesh` and
nothing more: one peer at a time, one packet per send, one reply awaited. It is
the interface the repeater-v2 link protocol was designed against, so a link
client implements it request for request (§11 below).

**The whole of it is single-threaded and synchronous.** The library has one
thread of control: it sends, it waits, it reads one reply, it sends again. No
method of `IMesh` may be called from an interrupt or another task, and none of
them is called re-entrantly — except through the `pump` the library hands to the
two `await*` calls, which you must treat as *other people's code that may do
anything but touch this mesh*.

### 3.1 The constants you must honour

```cpp
constexpr size_t kMeshReplyDataCap = 184;  // MeshCore's MAX_PACKET_PAYLOAD at this pin
constexpr size_t kMeshPathCap = 64;        // MeshCore's MAX_PATH_SIZE, and the link's own cap
constexpr size_t kMaxCliReplyLen = 160;    // agent/ReplyCodec.h: what CommonCLI writes
```

They are MeshCore's numbers at this repository's pin (`companion-v1.17.1`,
`d9296435`), written as the library's own, and `MeshReply`'s buffers are sized by
them. **If your mesh is a different MeshCore build, hold its numbers against
these with a `static_assert` of your own** — this repository does it in
`src/mesh/AgentMesh.h` — because a reply that does not fit is an overrun and not
a truncation. A build that *shrinks* either is fine; one that grows either must
be caught at compile time.

`MeshReply` itself is a plain struct the library owns and reuses. You fill it;
`reply()` hands back a const reference to it. It is valid from the moment an
`await*` returned true until the next send on the same mesh, and no longer.

```cpp
struct MeshReply {
  enum class Kind : uint8_t { None, Response, Trace, Text };
  Kind kind;  uint32_t tag;
  uint8_t data[kMeshReplyDataCap];  uint8_t len;   // a RESPONSE, after its four-byte tag
  char text[kMaxCliReplyLen + 1];                  // a TXT_MSG of CLI data, untrimmed
  uint8_t traceFlags, traceHashes[kMeshPathCap], traceHashBytes,
          traceSnrs[kMeshPathCap], traceSnrCount;  float traceFinalSnr;
};
```

`kind` is what the library branches on, and getting it wrong is the one way to
make a correct reply unreadable: a `Response` for a request or a login, a `Text`
for a CLI reply, a `Trace` for a trace. `data` is the repeater's reply bytes
**after** the four-byte tag; the codec in `agent/ReplyCodec.h` parses them, and
it is the library's job and not yours.

Of this struct the library reads exactly `kind`, `data`, `len`, `text` and the
six `trace*` fields — `traceFinalSnr` is the SNR you measured on the trace coming
home, and it becomes the result's last hop, this unit's own. `tag` is for your own
matching inside `awaitReply`; the library never reads it back. **Every field is
read by something**, since `v0.3.0`: `viaPath` and `snr`, which every mesh filled
and nothing read, are gone (the `discover` job asks `hasPath()` after a flooded
login, and a status's or a neighbour's SNR is the repeater's own figure out of its
reply bytes). A mesh that assigned them stops compiling at that version, and the
fix is to delete the two lines — the link's `via_path` and `snr_q4` are yours to
print and have nowhere to go in the envelope.

`SendResult` is MeshCore's `MSG_SEND_*` under another name: `Failed` (the packet
could not be built or could not be handed to the radio at all), `Flood` (it went
flooded) and `Direct` (it went along the path). The library treats `Flood` and
`Direct` alike except in one log line; `Failed` is a job-ending failure.

### 3.2 The methods

**Setup and counters.** None of these blocks.

| Method | Contract |
|---|---|
| `setRing(ObservationRing*)` | Where a heard advert goes. The loop calls it once, in `begin()`, with its own ring. Until it is set, count adverts and drop them. Every advert you hear becomes one `HeardAdvert` (`agent/Observations.h`) pushed into the ring: the key, the path and hop count it arrived by, SNR and RSSI, and the name, position and role the advert carried — `heardAtMillis` from the same `millis()` the platform returns, because the loop turns it into a timestamp at the moment the batch is built. Never a message's text. The ring drops its own oldest when full and counts it; you do not have to |
| `counters()` | A `MeshCounters` reference. The library reads **one** field of it: `advertsDropped`, the adverts whose data did not decode, for the `errors` report. `rx`, `tx`, `adverts` and `rxByType[16]` are for *your* console; leave them zero if you have no such counts. `tx` is worth keeping honest all the same — one per transaction and nothing else is the rail that catches a firmware sending twice |
| `overheard()` | The heartbeat's `overheard` error counter: frames of the mesh's own chat you counted and never read. On this board it is `rxByType[GRP_TXT] + rxByType[GRP_DATA] + rxByType[TXT_MSG]`. If your mesh cannot tell you a by-type count, report what it can and say so in your own docs — a number that is too low is a lie to the server's page, and 0 says *this transport cannot see chat*, which for a link client may be the truth |
| `loop()` | The pump. The library calls it whenever it idles — waiting for the airtime budget, waiting for a reply — so that what the radio hears is taken in while a job waits. In-process that is MeshCore's own loop; for a link client it is "read the link, dispatch its events". It must return promptly and must not block |
| `supports(jobType)` | **What your mesh cannot do** (since `v0.3.0`). Asked for each of the thirteen core types when the heartbeat is built, and once more before a job of one of them is run. Answer `false` and the type is **not advertised** — routing requires the type advertised, so the server sends none of it here — and a job of it that arrives all the same is reported `unsupported` at no transaction, with nothing sent and `beginJob` never called. The extension's types are not asked; they are yours. **It has a default, `true`**: a mesh that does not override it carries everything, which is what every mesh was before and what this board's is. A link client answers from what its link carries — the Walter's UART link has no request for a trace, so it returns `false` for `"trace"`. `jobType` is one of `kJobTypes`' static strings; a `strcmp` is the whole of it, and it must be cheap and never block |

**A job's exchanges.** A job is bracketed by `beginJob` / `endJob`, and between
them there is one peer and at most one awaited reply.

| Method | Blocking | Contract |
|---|---|---|
| `beginJob(publicKeyHex, pathHashes, hopCount, hashSize)` | no | The repeater this job is for, and the path the server knew to it: `hopCount` hashes of `hashSize` bytes each, or `nullptr`/0 for *no path*, which means **flood**. End any job in progress first. Return `false` when `publicKeyHex` is not 64 hex characters — the library reports the job `failed` with *the repeater's public key is not 64 hex characters* and sends nothing. A key you cannot address is not an error of this call: address it and let the sends go unanswered |
| `endJob()` | no | No job in flight: forget the peer, wipe the shared secret, and stop accepting a reply from anybody. The library calls it at the end of every job, whatever the outcome — and your `beginJob` must end one still in flight itself, because the library does not call this first |
| `setDirectPath()` | no | The server knows this repeater as a direct neighbour: a path of **zero hops**, sent direct with an empty path rather than flooded. Called right after `beginJob` when the claim carried a path of length zero |
| `forgetPath()` | no | The path is stale — a direct login along it got nothing back. Forget it so the next send floods and its reply teaches a fresh one |
| `hasPath()` | no | Whether a path is held. The library reads it to choose a reply timeout and to write the result's `pathUsed`, and the `discover` job reads it after a login to learn whether the reply taught one |
| `pathHopCount()` | no | The hops in that path, 0 for a direct neighbour. **This is the reply timeout's multiplier** (§3.3) |
| `describePath(char hops[][8], int max)` | no | The path now held, as hex hops of `hashSize` bytes each, in the order they go into a packet — which for a server-supplied path is the order the claim carried — into the caller's array; return how many were written. It becomes the result's `pathUsed` |
| `sendLogin(password, tag&)` | no — returns when the packet is away | A login as *this unit* (EXIT-NODES §7). `password` empty or `nullptr` is the **ACL login**: our own key and no secret at all. With a password it is the guest or admin login, and **at most 15 bytes of it go** (`kMaxPasswordLen`), because that is what the firmware at the other end keeps; wipe the plaintext as soon as the packet is encrypted. Set `tag = 0`: a login reply carries the peer's timestamp and not ours, so the await that follows is "the next reply from this peer" |
| `sendRequest(reqType, data, len, tag&)` | no | One request datagram on the session the login opened — the four `kReqTypeGet*` constants of `agent/IMesh.h`, and `kReqTypeGetAccessList` beside its parser in `agent/ReplyCodec.h`. `tag` out is the timestamp you drew, which the reply will carry back |
| `sendCli(text, sensitive, timestamp&)` | no | One CLI line as a text datagram of CLI type, on the admin session. **You draw the sender timestamp**, put it in the packet and hand it back in `timestamp` — the repeater judges replays by it and will answer an *equal* one with nothing at all, so the series must be strictly increasing per peer. A retry calls again and gets a fresh one. `sensitive` is a line made from a password: its frame, and the reply awaited for it, must be printed with their bodies redacted if you print frames at all |
| `sendDiscovery(tag&)` | no | The path-discovery request as a companion's `CMD_SEND_PATH_DISCOVERY_REQ` makes it — a base-telemetry request, **always flooded**, whose reply comes back as a returned path. The last resort of a `discover` job |
| `sendTrace(roundTripHashes, hashBytes, hashSize, tag&)` | no | A trace along the round trip: the way out, the repeater, and the way back. `hashSize` is 1, 2, 4 or 8 — a trace packet cannot express three-byte hashes, and the library refuses such a path before it ever reaches you (`unsupported`, at no transaction). Needs no session and no login |
| `awaitReply(tag, timeoutMs, pump)` | **yes** | Wait up to `timeoutMs` for the `Response` (or `Trace`) with this tag; `tag == 0` means *the next reply from the peer*. Pump your own mesh and call `pump` (when it is not null) while waiting. Return `true` with `reply()` filled, or `false` on silence. A reply with the wrong tag is not this one: drop it as a late reply, do not return it |
| `awaitText(timeoutMs, pump)` | **yes** | The same for a CLI reply: the first text datagram of CLI data from the peer, which carries **no tag** at all |
| `reply()` | no | The last reply an `await*` returned true for |

`uniqueTime()` is deliberately **not** on this interface. Every send draws its
own timestamp and hands it back, so nothing in the library ever asks for one;
keeping it would have made every implementation answer a question nobody asks.
`test_seams` fails if it comes back.

### 3.3 Timing, and who decides it

The library decides every timeout and hands it to you; you never choose one.

- a **request** waits `job.perHopMs × (pathHopCount() + 1)`, and a flood waits as a
  three-hop request would, because nobody knows how far it went. `perHopMs` is
  the claim's, 4 000 ms by default — so 16 s for a flood.
- a **login** waits the longer of that and `job.loginMs` (8 000 ms by default).
- between two sends there is the **airtime budget**: at the conservative default
  (EXIT-NODES §8) two transactions a minute, twenty seconds apart. The library
  waits that gap by calling `loop()` and `IPlatform::delayMs(1)` in a tight idle
  loop, which is why both must be cheap and neither may block.

The practical consequence for a device that sleeps: the pace dominates
everything. A `config.read` of the default key list is 21 transactions, which is
about eleven minutes of *waiting for the budget* and a few seconds of radio. One
wake's `Budget` is how you refuse such a job rather than be surprised by it (§6).

### 3.4 What the library does when your mesh fails

| What happens | What the library does |
|---|---|
| `supports(type)` false | the type is left out of the heartbeat's `jobTypes`; a job of it anyway is `unsupported` at no transaction, *this mesh cannot carry a 'trace' job* in the log, nothing sent, `beginJob` not called |
| `beginJob` false | the job is `failed`, one log line, nothing sent |
| `sendLogin`/`sendRequest`/`sendCli` → `Failed` | *the packet could not be built*: the job ends `failed` |
| `awaitReply` false after a login | silence. Stock firmware answers a login it will not take with **silence**, and that cannot be told from a repeater that is off the air: the sequence (`agent/LoginSequence.h`) tries **twice** and reports `unreachable` — or `unauthorized`, when a blank probe login between the two tries *was* answered and only the password's try was silent. A `discover` job escalates further on its own: `forgetPath()`, a flooded login, and the path-discovery request last |
| `awaitReply` false after a request on a reused session | one retry with a **fresh login**, then `unreachable` |
| `awaitText` false after a CLI line | **nothing is sent again.** One send per command is the rail: a `set` sent twice is a change made twice. The line is recorded as unanswered and the sequence stops |
| a reply the codec cannot read | a login reply that is not one is *refused*, which the sequence reports `unauthorized` and which drops the cached session; a request's reply that will not parse is `failed`. Either way the bytes never reach a log line |

## 4. `IUplink` — meshmaster.dk

`agent/IUplink.h`. Seven calls of [API §3](https://github.com/twinoak/meshmaster.dk/blob/master/spec/API.md#3-agent-api-agentv1),
each answering a `Call` record the library's `ServerPolicy` reads. **Every one of
them blocks** until the server answered or the attempt failed; the library makes
one at a time and never from inside another.

What you must put on the wire for all of them:

- `Authorization: Bearer <agentToken>` — the token is **yours** to store and
  yours to wipe. The library never sees it and has no setting for it.
- `X-Agent-Version: <your version>` — a missing header is a 400 that names it.
- `Accept: application/json`, and `Content-Type: application/json` on a POST.
- the body exactly as the library wrote it, bytes unchanged. Do not re-serialise
  it: the fixtures are the contract (§9).
- the reply parsed by the library's own parser — `mm::parseAgentConfig`,
  `mm::parseHeartbeatReply`, `mm::parseClaimResponse`,
  `mm::parseObservationsReply`, `mm::parseExtendReply`,
  `mm::parseProgressReply`. Do not write a second reader of these shapes.

**The reply buffer is yours**, and `mm::parseClaimResponse` takes a **mutable**
`char*` because it parses the claim in place — that is deliberate, and it is why
a credential never becomes a second string somebody has to wipe. The library
offers no size for these buffers; this board's figures are a reasonable place to
start, and they are: **2 kB** for the config, heartbeat, observations, extend and
progress replies, and **6 kB** for a claim, which is two jobs with their
credentials, paths and timeouts. A reply that does not fit should be cut and the
call reported `ok = false` with a reason, which is what the parser would say
anyway.

**There is no `Retry-After` parser in the library** — a thing worth knowing
before you go looking for one. `Call::retryAfterS` is a count of seconds you
parse yourself, and the rule is simple: the integer form as it stands, and
**0 when the header is absent, unparseable, or an HTTP date**.
`mm::retryAfterMs` turns a 0 into one minute and bounds anything above an hour,
so the date case costs you nothing to not handle. (This board's own parser does
exactly that, in `src/agent/AgentClient.cpp`, and caps at an hour itself.)

| Method | Returns true when | Notes |
|---|---|---|
| `configured() const` | a URL and a token are set, so a call can be made at all | the loop gates every call on it and says *no token* when it is false. Not blocking |
| `link() const` | — | `LinkState { up, connects, drops }`: the loop gates on `up`, reports `connects - 1` as the heartbeat's `wifiReconnects`, and reads `up` for the backoff's *proven link* rule. Not blocking |
| `fetchConfig()` | 200 and the body parsed | `GET /agent/v1/config`, once at startup and again after a new link. Keep the reply: `hasConfig()` and `config()` report it |
| `postHeartbeat(hb, reply&)` | 200 and the body parsed | `POST /agent/v1/heartbeat` with the bytes `writeHeartbeat` made. The reply carries the server time, the budget, the accepted poll interval and `jobsWaiting` — it is the single most important call in the agent, and the only one that carries a clock |
| `postObservations(body, len, accepted&, duplicate&)` | 200 and the body parsed | `POST /agent/v1/observations`. `accepted` is how many the server took; `duplicate` means this batch's client id had been posted before, which is **not** a failure — the ring consumes the batch either way |
| `claimJobs(jobs, max, count&)` | 200 and the body parsed | `POST /agent/v1/jobs/claim` with `{"max":N}` from `writeClaimRequest`. Parse into the caller's array with `parseClaimResponse`, which parses **in place** and puts the credential in the job's own buffer and nowhere else. `count` may be 0 |
| `postJobResult(jobId, body, len)` | 200 | `POST /agent/v1/jobs/{id}/result`. A late result is taken all the same and a duplicate `clientId` stores nothing twice, so this is safe to retry — which is what the offline buffer does |
| `extendLease(jobId, seconds, leaseUntil&)` | 200 | `POST /agent/v1/jobs/{id}/extend` with `{"seconds":N}` (1–1800). A **409 is "you are not the lease holder any more"** and is false. Nothing in the library calls this yet; it is here because the protocol has it and a long job of your own will want it |
| `postProgress(jobId, body, len)` | 200 | `POST /agent/v1/jobs/{id}/progress` with the bytes `writeProgressReport` made: an intermediate `data` and a note, kept on the job and replaced by the next. Not a result. Also uncalled by the library |
| `hasConfig()`, `config()`, `last()`, `lastCall()`, `limiter()` | — | reads of what you kept. `last()` is the config call's `Outcome` (the console's `server` line); `lastCall()` is the record of the most recent call of any kind; `limiter()` is the client-side 120-a-minute ceiling (§4.2) |

### 4.1 The `Call` record, which is how a failure is read

Every call must leave one behind, and the loop's whole behaviour against a server
that is unwell comes out of three of its fields:

```cpp
struct Call {
  bool reachable;      // a reply arrived, whatever its status. False for DNS, TCP, TLS, a timeout
  bool ok;             // 2xx and the body parsed
  int httpStatus;      // not read when !reachable
  uint32_t retryAfterS;      // the answer's Retry-After in seconds; 0 when it carried none
  int rateLimitRemaining;    // X-RateLimit-Remaining, or -1
  char error[128];     // one sentence for a log line and the console
  uint32_t atEpoch, atMillis;
  const char* via;     // one word for how it went; a **static** string, never a buffer of the call's
};
```

`mm::actionFor(reachable, httpStatus, retryAfterS)` turns that into what the loop
does (`agent/ServerPolicy.h`), and it is worth knowing before you write the
client, because it tells you which headers you must actually parse:

| Answer | Action |
|---|---|
| 2xx | take the reply, reset the backoff |
| **401** | **halt.** The token is revoked or wrong and retrying fixes neither. Nothing is sent again until a person acts; the state survives a sleep |
| 429 | wait exactly its `Retry-After` |
| 502, 503, 504 **with** `Retry-After` | the same: a deploy of meshmaster.dk says how long it will be, and that is believed rather than doubled past |
| another 4xx | discard what was being sent; do not retry it |
| any other 5xx, or no reply at all | back off: 30 s doubling with jitter, to **two minutes** once the link is proven (up, and a time off the network) and ten minutes while it is not |

So the only headers you must read are `Retry-After` (as above) and —
if you want the console line — `X-RateLimit-Remaining`. Everything else is the
status code. Note that `reachable` and `ok` are different questions: a 503 is
`reachable` and not `ok`, and a backoff is earned by both a 5xx *and* a TLS
failure, which is why the policy takes `reachable` as its first argument.

### 4.2 The ceiling on your own side

The server's `agent` policy is 120 requests a minute per token. The library keeps
the same ceiling on this side (`agent/RateLimit.h`) so that a call that would
break it is never made: hold a `RateLimiter`, `take()` before a call, and answer
`limiter()` with it. The ordinary loop never comes near it; a bug that polls in a
tight loop meets it instead of meeting the server's 429.

## 5. `IPlatform` and `IClock` — seven primitives and a wall clock

`agent/IPlatform.h`. This is everything the portable files used to take from
Arduino and the ESP-IDF directly, and nothing more.

| Method | Contract |
|---|---|
| `millis()` | Milliseconds since boot, monotonic, wrapping every 49.7 days. Every comparison in the library is a signed difference, so the wrap is handled — but it must not jump backwards *within* a boot. After a deep sleep it may start again at zero; that is what `WakeState` exists for (§6) |
| `delayMs(ms)` | Yield for at least this long. Called with 1 in the idle loop between pumps, and with longer waits nowhere. On a host fake this *is* the passage of time |
| `random()` | Thirty-two random bits: a result's `clientId`, the backoff's jitter, a batch's id. It does not have to be cryptographic; it does have to differ between two units that booted together |
| `clock()` | The wall clock, below. Called on every heartbeat, every result and every lease check |
| `freeHeap()`, `minFreeHeap()` | What is free now and the least that ever was. Reported, never decided on |
| `rebootForHealth(why)` | Restart the board on purpose, having noted why. **Does not return** — it is `[[noreturn]]`. Called by `loop()` after twelve consecutive unanswered calls over ninety minutes *with the link up*, because a TLS stack that has failed that long against a live link will not recover by being asked again. In one-wake mode it is **never called**: the report says `rebootWanted` and the reason, and your supervisor decides. A `wake()`-only firmware may implement this as a hard fault handler, or as an abort it never expects |
| `selfReboots()` | How many times this firmware has restarted itself that way since the last power cut: the heartbeat's `reboots` counter. Keep it where a restart does not clear it (RTC memory on an ESP32) |

`IClock`, reached as `platform.clock()`:

| Method | Contract |
|---|---|
| `hasTime()` | False while nothing has set the clock. Then `now()` is 0 and **no lease has run out** — a job is not refused for want of a clock |
| `now()` | UNIX seconds, 0 while `!hasTime()` |
| `fromNetwork()` | The time came off the network (SNTP, or the server's own) rather than being kept across a reset. With the link up, that is what proves the link, which lowers the backoff ceiling to two minutes |
| `sourceName()` | One word for a job's log: `"sntp"`, `"server"`, `"rtc"`, `"none"` |
| `takeServerTime(serverEpoch, difference&)` | Offered the server's time by every config reply, heartbeat reply and claim. Return true when you took it; `difference` is server minus local *before* any change, for the log. A time that already came off the network should win over it — that is this board's rule and a sensible one |

**The log is the third seam of this header and is not a method.** `mm::logf`
(`agent/Log.h`) is the one entry point every file uses; it formats the line —
level letter, `[HH:MM:SS]` once the clock is known and `[+123s]` before that, CR
LF, truncation at 288 bytes — and hands it to the sink you set:

```cpp
mm::logBegin(&epochNowOrZero, &uptimeMs);   // two function pointers
mm::logSetSink(&myLogSink);                 // void(const char* line, size_t len)
mm::logSetLevel(mm::LogLevel::Info);
```

With no sink set a line is formatted and **dropped**: a library owns no port. Set
it before anything logs.

### 5.1 `IRetained` — what outlives a sleep (optional)

`agent/IRetained.h`. A fourth interface, and the only optional one: a device
that drives the loop with `loop()` never needs it, and the Heltec passes none.

A result the server would not take, or one a wake kept for its budget, waits in
the offline buffer — which is RAM. On a device that deep-sleeps between wakes
the RAM goes with the sleep, so until pile 1.6 the result was lost and its job
stayed leased until the server's lease lapsed; the adverts a wake could not post
went the same way. `WakeState` cannot carry them — twelve kilobytes of results
against an RTC memory that is nearly full — so this seam carries them instead,
into whatever you have: a flash region, an NVS blob, a file. The library neither
knows nor includes any of them.

| Method | Contract |
|---|---|
| `capacity()` | The most bytes an image may be. Constant for the life of the store |
| `write(image, len)` | Store `len` bytes as the one image, replacing whatever was there; true when stored whole. A torn write is not yours to detect — the image carries its length and a CRC-32, and a torn one is refused whole at the next read |
| `read(out, cap)` | The image last stored, into `out`: how many bytes were put there, or 0 when the store holds none. Handing back more than the image is long (a whole flash region) is fine; the image says its own length |

**The rule the loop keeps, in one sentence:** *kept results first, oldest first,
then the waiting adverts with the moment each was heard, whole or not at all,
one write a wake and none when nothing changed.* What the store cannot hold is
dropped from RAM as well and counted in the two figures the heartbeat already
carries, `resultsDropped` and `observationsDropped`, which the image carries
across the sleep too — so a store that is too small is a number on the exit
node's page, not a silent loss. Results go first because a lost result leaves a
job leased; a lost advert is heard again.

**The image may hold nothing a result does not.** Its layout is written down in
`agent/KeptImage.h` — a 32-byte header, then result bodies exactly as
`postJobResult` would be handed them, then adverts exactly as the ring holds
them — and the runner's rule already keeps every credential out of a result.
`test_loop`'s `TheImageHoldsNothingAResultDoesNot` greps the image's own bytes
for the fixtures' sample passwords after a rotation, a guest login and a `cli`
job. A person with your flash in hand learns what the server would have.

**Wear.** At most one write a wake, and none when the image would be what the
store already holds — in particular none when nothing is kept and the previous
image was already empty. A unit whose wakes post everything writes nothing; one
on a metered plan writes a few images a day. Size a flash region for that.

## 6. The lifecycle

Construct, wire, drive. The objects are yours to own: the library has no globals
of its own and allocates nothing of its own on a heap — its buffers are members
of the two objects below. (It is not heap-free: the reply parsers each hold an
ArduinoJson `JsonDocument`, which allocates while it parses and frees when the
call returns. The largest of them reads a claim, and this board's heap figures
with it are in its `status` command.)

```cpp
MyMesh      mesh;        // : mm::IMesh
MyUplink    uplink;      // : mm::IUplink
MyPlatform  platform;    // : mm::IPlatform  (and a mm::IClock behind clock())

mm::JobRunner jobs;      // tens of kilobytes of buffers: namespace scope, never a local
mm::AgentLoop agent;
mm::HeartbeatIdentity identity;   // kept alive as long as the loop: it is read at every heartbeat
```

`JobRunner` is large on purpose — one scratch area every job shares, rather than
a stack frame: `sizeof(mm::JobRunner)` is 66 kB on a 64-bit host and somewhat
less on a 32-bit target, of which 13 kB is the two claimed jobs and 18 kB the
result's data and body. It must be a namespace-scope or static object; a
neighbour list alone is more than the 8 kB of stack the Arduino loop task has.
`AgentLoop` is 7.6 kB, most of it the 64-entry observation ring.

1. **`mm::logBegin` / `logSetSink`**, before anything else logs.
2. **Fill `identity`** — `agentVersion`, `transport`, `board`, `companionFirmware`
   and the `RadioParameters` the radio is actually programmed with — and refresh
   it whenever one of those changes. The library reads no setting; this struct is
   how it knows what to say about you. `transport` is meshmaster.dk's word for how
   you reach your radio (EXIT-NODES §2): `onboard` is the default and the Heltec's,
   a unit that *is* its repeater says `native`, and the library validates nothing
   — the vocabulary is the server's.
3. **`jobs.begin(mesh, uplink, platform, pump)`.** `pump` is a plain function
   pointer, or null: it is called in every idle loop inside a job, which is where
   all the time is. The Heltec's keeps the console, the display and the watchdog
   alive and calls `agent.heartbeatIfOverdue()` — see below.
4. **`agent.begin(uplink, platform, mesh, jobs, identity)`**, then
   **`agent.setIdentity(publicKeyHex)`** with this unit's own 64 lowercase hex
   characters, or `nullptr` while it has none — it forwards to the runner, which
   is why there is one call and not two. `begin()` is also where the loop hands
   its observation ring to the mesh and the airtime budget to the runner.
5. **Drive it**, one of two ways:
   - **`agent.loop()`** — call it for ever, as often as you like, with the link
     up. It does at most one blocking call per pass, in the .NET agent's order:
     the config call once, the heartbeat every accepted interval, then the
     offline buffer, then the claim, then one observation batch. This is what the
     Heltec does from the Arduino loop. Tell it about a new link with
     `onLinkUp()`, and about a changed token with `reconfigure()` (which also
     forgets a 401).
   - **`agent.wake(intent, budget)`** — one bounded pass, for a device that
     sleeps. Returns a `WakeReport`; then you sleep for
     `report.acceptedIntervalS`, but never less than `report.sleepAtLeastS`,
     and call it again.
6. **Across a sleep**: `const mm::WakeState s = agent.state();` into retained
   storage as bytes before you sleep, and `agent.restore(s)` after the next boot,
   before the wake. It is a trivially copyable POD of 76 bytes; every duration in
   it is a *duration* and not a deadline, because `millis()` restarts at zero,
   and `restore()` re-bases them against the clock it finds. It refuses a blob of
   another `version` and changes nothing. What it carries: the backoff and how
   long the present run of failures has lasted, the accepted interval, the
   airtime budget, a 401, and the counters the heartbeat reports.
7. **What the sleep keeps** (optional, §5.1): `agent.setRetained(&store,
   scratch, sizeof scratch)` once after `begin()`, with a store of yours and a
   scratch the image is built in — the library allocates nothing, so the RAM is
   yours; a full image is about 19.5 kB (`ResultBuffer::kCapacityBytes` of
   results, 64 adverts at `kKeptAdvertLen`, and the headers), and the image is
   never larger than the smaller of the scratch and the store's `capacity()`.
   Then, once a boot and before the first wake, **`agent.restoreKept()`**
   after `restore(s)`: it reads the image back into the offline buffer and the
   ring, true when one was read. The end of every `wake()` writes the next
   image by itself; `report.keptImageWritten` says whether this wake did, and
   `report.resultsNotKept` / `observationsNotKept` what the store was too small
   for. A torn, short, foreign-version or bad-checksum image is refused whole —
   never half-read — counted in `keptImagesRefused()` (a count the next image
   carries across the sleep as well) and logged with the reason, and the next
   wake writes a fresh image over it.

```cpp
static uint8_t keptScratch[20480];        // yours: the image is built here
MyRetained kept;                          // : mm::IRetained, over a flash region

agent.setRetained(&kept, keptScratch, sizeof keptScratch);
agent.restore(savedState);                // the WakeState, from retained memory
agent.restoreKept();                      // the results and the adverts, from the store
const mm::WakeReport r = agent.wake(intent, budget);   // ... and the image is written here
```

**One wake, in detail.**

```cpp
mm::WakeIntent intent;
intent.nextCheckInS = 3600;            // what the heartbeat promises the server (60..86400)
intent.maxTransactionsThisWake = 12;   // and what this wake has the energy for

mm::Budget budget;
budget.wakeSeconds = 240;              // the whole wake; 0 is "no bound"
budget.uplinkSeconds = 60;             // of that, what the server calls together may take
budget.postBytes = 16384;              // every body this wake posts, for a metered plan

const mm::WakeReport r = agent.wake(intent, budget);
```

**A wake's byte and uplink budgets cover every call it makes.** Not only the
loop's — the claim and every result a job posts go through the same ledger
(`agent/UplinkMeter.h`), so `r.bytesPosted` is every byte of body the wake
posted from every step and `r.uplinkMs` is what all its calls took together.
What the budget has no room for is **kept**, never dropped and never posted out
of turn: the oldest result goes first, a result that does not fit waits in the
offline buffer behind the queue, and `r.resultsKept` counts it (*kept: budget*).
That is a bound of yours and not a failure of the server's — nothing backs off
for one, and the next wake posts it with the timestamps it was made with. The
one thing the byte cap does not bound is the config call, the heartbeat and the
claim: their bodies are written inside your `IUplink` and are not countable from
here, though their seconds are counted like any other call's.

The six steps run in this order, each recording its own outcome in the report —
`Skipped`, `Done`, `Failed`, `NoTime` or `NoBudget`: **config** (only when this
uplink has none, which after a deep sleep is once a boot), **heartbeat** (always,
and it is where the clock and the accepted interval come from — a heartbeat that
did not come back stops the wake there), **results** (the offline buffer, oldest
first, with the timestamps they were made with), **claim** (only when the
heartbeat said work waits; `max` sized from what the transaction cap can hold),
**jobs** (each priced by `mm::transactionsOf` and refused *before* it goes on the
air if it will not fit), **observations** (as many batches as the byte cap
allows).

Three guarantees worth relying on: **no job a claim handed over is ever left
leased and silent** — one the wake has no room for is reported `failed` with
*wake budget* in its log, at no transaction and with nothing sent, and one whose
result the byte cap or the uplink seconds cannot carry is kept in the offline
buffer for the next wake rather than posted past the budget (*offline is normal*:
a late result is still taken, and de-duplicated on the client id it was written
with); **a backoff does not spin**, it comes back as `sleepAtLeastS`; and **the
reboot guard does not fire**, it sets `rebootWanted` and the reason. With a
store (§5.1) the first of those holds **across the sleep** too: a kept result is
in the image, the next boot reads it back, and the wake after it posts it under
the client id it was made with. Without a store the promise holds within one
boot, which is what it was before pile 1.6 and is the documented behaviour of a
board that does not sleep (EXIT-NODES §5).

The transaction cap is the *intent's* `maxTransactionsThisWake` and not a field
of `Budget`, deliberately: it is stated to the server in the heartbeat and
enforced locally against the same arithmetic, and two fields holding one number
can disagree.

**`heartbeatIfOverdue()`** is the one re-entrant call in the library, and it
exists for `loop()`: a claim is run to the end before `loop()` gets its turn
back, and a `config.read` is eleven minutes, so without it the exit node's page
would read *quiet* while the board was working perfectly. Call it from your
`pump` if you drive with `loop()`. It is harmless inside a wake (the wake sends
its own heartbeat first and sets the schedule), and a wake short enough to
matter never reaches it.

**The console reports** are optional and free: `agent.report(out)`,
`agent.reportErrors(out)`, `jobs.report(out)`, `jobs.reportSessions(out)`, and
`describe(buf, cap)` on either, where `out` is a `mm::TextOut` of yours (one
`write(text, len)`, and `print`/`println`/`printf` come with it). They print what
this firmware's `server`, `errors`, `jobs` and `sessions` commands print.

## 7. `IJobExtension` — your own job types

`agent/IJobExtension.h`. A claimed job whose `type` is none of the thirteen is
offered to the extension you set (`jobs.setExtension(&ext)`) **before** it is
answered `unsupported`, and the heartbeat advertises your types after the
thirteen — which matters, because the server routes a type only to an agent that
advertised it. At most `kMaxExtensionTypes` (8).

```cpp
class MyJobs : public mm::IJobExtension {
  int typeCount() const override { return 1; }
  const char* type(int) const override { return "unit.telemetry"; }  // a static string
  bool run(const mm::ClaimedJob& job, mm::JobContext& ctx, mm::ResultWriter& out) override;
};
```

**The runner still does everything around the job**: the claim and the lease
check, the pacing against the airtime budget, the result envelope, its log, the
post, the offline buffer if the post failed, and wiping the credential when the
job ends. What is yours is the exchange and the verdict.

`JobContext` lends you, for the length of one job: `mesh()` with the job already
begun on it (the peer set, the server's path in place), `platform()`, `spend()` —
**call it before every send**: it waits for the airtime budget, counts one
transaction, and returns false when the lease ran out first, after which nothing
more may go on the air — `replyWaitMs()`, the wait to use for this job's path,
and `log(line)`, one line into the job's log, which is the result's `log`. Never
a password.

`ResultWriter` takes `data(json, len)` — the result's `data` object as JSON you
wrote, false when it does not fit — and `outcome(o)`, one of `ok`, `failed`,
`unreachable`, `unauthorized`, `unsupported`, `timeout`. Return `true` from
`run()` when you ran the job and set an outcome; return `false` to decline it,
and the runner reports `unsupported` as if you were not there.

You get no session: a login is your own business, through `IMesh`. Note also
that `canRun()`'s class rules are the thirteen's and are **skipped** for a job
offered to the extension — the type is yours, so the rules about which class may
run it are yours too.

**`JobContext` lends you no uplink**, deliberately: a job type of yours that
wants to post progress or ask for a longer lease calls your own `IUplink`
directly — you built that object, and `job.id` is right there in the claim. The
airtime gate is for the *radio*, so `spend()` is not what you call before a
server call, and the library will neither make those calls for you nor stop you
from making them. A long job — a firmware download in ranges across several
wakes — is exactly what `postProgress` and `extendLease` are in the interface
for.

### 7.1 What you cannot do from behind these interfaces

Two limits, stated plainly because each one is easier to find here than in a
test that fails for an unrelated-looking reason. Neither is a decision anybody
defends; each is what the one consumer this library has had did not need yet,
and the line that would change it is short:

- **You cannot change what one of the thirteen does.** Those handlers are the
  library's: their requests, their order, their retries, their result shapes and
  their rails. `IJobExtension` adds a fourteenth type; it does not replace a
  first. A different job type, through the extension, is the way.
- **You cannot report a drop that happened before the ring.** The heartbeat's
  `observationsDropped` is `ObservationRing::dropped()` — what *the ring* dropped
  when it was full, and since `v0.3.0` what the kept image could not hold
  (§5.1), which is also a drop of the ring's. Adverts your radio or your link
  lost before they reached `push()` have nowhere to go in the envelope.

Three limits that stood here at `v0.2.0` are gone at `v0.3.0`, and this is where
a reader of the older page finds out: you **can** say what your transport is
(`HeartbeatIdentity::transport`, §6 step 2), you **can** advertise fewer than the
thirteen and have the runner answer `unsupported` for a type your mesh cannot
carry (`IMesh::supports`, §3.2 — asked at the heartbeat and before the job, so
nothing is sent for it), and `MeshReply` no longer asks you for `viaPath` and
`snr` (§3.1). The fourth of the older list — all thirteen always advertised — was
the same limit as the second, seen from the heartbeat.

## 8. The rails you inherit, and must keep

These are this repository's promises — `README.md`'s *Rails that hold in every
task* — and a consumer of the library inherits all six. Each has a test here;
your half of each needs one of yours.

| The rail | What the library does, and the test that holds it here | Your half |
|---|---|---|
| **Nothing the mesh says is logged as text** | The library has no way to see a frame it did not ask for: the only way in is `reply()` after an `await*`, and nothing it logs carries a reply body — `Credential.ThePasswordIsNeverInTheJobsPrintableFields` and `RunnerTest.APasswordSetSendsTheLineOnceAndRecordsNoValueAnywhere`, which greps every captured log line and the whole result body for the value. A repeater's CLI reply *to our own command* is a job's result and is carried on purpose; the mesh's own chat is not | **The dropping is the mesh implementation's.** On this board `AgentMesh::onRecvPacket` counts a chat frame and releases it before anything is decrypted, and the only trace of it that travels is the `overheard` counter (`LoopTest.TheHeartbeatsBodyIsTheContractsShapeAndThisBoardsIdentity`). Yours must do the same and must not print one either |
| **A password is in RAM for the job and gone** | The claim is parsed in place and the credential lands in the job's own fixed buffer and nowhere else; it is wiped when the claim ends, whatever happened. `Credential.ThePasswordIsHeldInAFixedBufferAndWipedToZeros`, `Credential.TheRunnerWipesTheCredentialAfterEveryJob`, `Credential.NoModuleThePasswordPassesThroughNamesTheStoreOrAFileSystem` (a grep of every module it passes through for an NVS or file-system API), and `RunnerTest.TheCredentialBytesAreZeroAfterTheJob`, which reads the bytes back through the pointer the fake server was handed | Your `sendLogin` must wipe the plaintext as soon as the packet is encrypted and **keep no copy** — not in a link buffer, not in a retry cache, not in a log. At most 15 bytes of it are ever sent; a stored password longer than that is a lock-out, which the library says in a log line and reports `unreachable` |
| **One send per command** | A write is sent once, whatever comes back: an apply's commands in order with strictly increasing sender timestamps, stopped at the first refusal or silence, and a CLI line never retried. `RunnerTest.ACliLineIsSentOnceAndNeverRetried`, `RunnerTest.AConfigApplySendsEachCommandOnceAndReadsBackWhatItTouched`, and `RunnerTest.OneSendPerCommandTheMinimumGapAndThePerMinuteCapAreHeldByTheClock`, which reads every send out of the fake with the clock's time against it | **The library's pacing assumes your mesh sends exactly what it was asked, once.** A transport that retries a datagram by itself turns one `set` into two changes. If your link has a retry, it must be a retry of the *link* frame and not of the mesh packet |
| **TLS is verified** | **The library cannot check this and does not try.** It has no socket. `test_tls_policy` greps this whole tree — `src/`, `test/`, `tools/`, `docs/`, `lib/`, the workflows, `platformio.ini` and `README.md` — for every way of skipping certificate verification, and the pinned root is held to ISRG Root X1 (`TlsPolicy.NothingInTheTreeSkipsCertificateVerification`, `TlsPolicy.ThePinnedRootIsIsrgRootX1`) | Your `IUplink` verifies the chain, against a root you ship. Write the same grep over your own tree; it is twenty lines and it is the only thing standing between a field unit and a man in the middle |
| **Path hashes are two bytes or more, never one** | The library never chooses a width for a flood — it passes through the width the claim carried — and it refuses a three-byte path for a trace before a packet is built (`Trace.ThePathHashSizesATracePacketCanCarryAreThePowersOfTwo`, `RunnerTest.ATraceAlongThreeByteHashesIsUnsupportedAtNoTransaction`) | Every flood **your** mesh originates uses two-byte path hashes or wider (`AgentMesh::kFloodPathHashSize` is this board's). One-byte hashes let two repeaters that share a first byte both repeat a direct packet; the first bench showed a five-hop path of them (D-33) |
| **The contract is the .NET agent's** | Every envelope is held byte for byte against `MeshMaster.Contracts`' own fixtures — `test_contract_fixtures`, `test_envelopes`, `test_job_results`, `test_config_read`, `test_claim`. There is one dialect and a second one is a bug however well it works | Post the bytes the library wrote, unchanged (§4) |

## 9. The fixtures are the wire contract

Two sets, both copied and **never edited here**:

- [`tests/fixtures/contracts/`](../tests/fixtures/contracts/README.md) — the
  agent API's envelopes, generated by `MeshMaster.Contracts`' own round-trip
  tests in `twinoak/meshmaster.dk` and copied byte for byte.
  `MANIFEST.txt` names the commit they came from, with a sha256 and a size per
  file; at `v0.2.0` that commit is **`0595dc2`**, 47 files.
- [`tests/fixtures/companion/`](../tests/fixtures/companion/README.md) — 46
  transcribed MeshCore frames, which is what `agent/ReplyCodec.h` is held to.
  **It has a manifest and a commit of its own** (at `v0.2.0`, `f518ebd`): the two
  sets move for different reasons — the envelopes when the agent API changes, the
  frames when MeshCore does — and `tools/sync-fixtures.ps1 -Set contracts` or
  `-Set companion` moves one without the other.

A consumer that posts to the same server is bound by the same contract, and the
honest way to stay bound is the one this repository uses: run the same bytes
through the same writers and compare with the fixture. `test_envelopes`,
`test_job_results` and `test_claim` are the pattern — and `test/support/Fixtures.h`
is a reader for both sets, including `followsShapeOf`, which holds one object's
members against a fixture's *in the fixture's order*.

Syncing is one command, and it is a sync in one direction only:

```powershell
.\tools\sync-fixtures.ps1 -Commit master          # bring both sets across at that commit
.\tools\sync-fixtures.ps1 -Verify                 # what CI runs: the manifest against the files
```

A fixture sync is the moment the dialect moved, so it belongs in your upstream
watch (§10). If a fixture and a real server disagree, the server is the
reference and the fix starts in `twinoak/meshmaster.dk` — never in a local
edit here.

## 10. Versioning, pinning, and watching this upstream

**A tag `vX.Y.Z` of this repository is the library's version.** There is nothing
published anywhere — not the PlatformIO registry, not `components.espressif.com`
— because a tag is already a pin and a registry entry would be a second thing to
keep in step. `library.json`, `idf_component.yml` and `agent/Version.h` carry the
same number; a release moves them together and `test_library` fails if one of
them moves alone. Report both versions from your firmware: yours, and
`AGENT_CORE_VERSION`.

**What a bump may change**, by interface, and what it means for you:

| A release may | Consequence in your tree |
|---|---|
| add, remove or re-signature a method of `IMesh`, `IUplink`, `IPlatform`, `IClock`, `IJobExtension`, `JobContext`, `ResultWriter` or `TextOut` | a **compile error**, which is the whole point of them being a contract. A minor version does this; this library does not promise source compatibility across a minor bump before 1.0 |
| move `kMeshReplyDataCap` or `kMeshPathCap` (MeshCore moved) | your `static_assert` fires, or it does not exist and you have an overrun. Write the `static_assert` |
| change what the library *writes* — an envelope, a result shape | the fixtures' manifest commit moves. Nothing of yours stops compiling, and that is the dangerous one |
| change what the library *reads* of a repeater's reply | the companion fixtures move, and the pinned MeshCore tag in `platformio.ini`'s `[meshcore]` with them |
| add a job type, a log line, a console report | nothing; new behaviour behind the same interfaces |
| change `WakeState`'s layout | `WakeState::kVersion` goes up, and `restore()` refuses your stored blob rather than misreading it. You lose a backoff across one sleep, which is the right failure |
| change the kept image's layout (§5.1) | `kKeptImageVersion` goes up, and `restoreKept()` refuses the image in your store whole, counts it, and the next wake writes a fresh one. You lose what one sleep kept, once, across the update — and the counter says so |

**The changelog is [`CHANGELOG.md`](../CHANGELOG.md)** at the root of this
repository, and it is written for exactly this page's reader: one section per
tag, naming any change to the three interfaces, the `MeshReply` constants, the
fixtures' manifest commit and the library's version. It is a deliberate choice
over the GitHub release notes, which are generated by
`.github/workflows/release.yml` and are about flashing a Heltec, and over
`docs/upstream.md`, which moves when MeshCore moves and would make this watch
noisy.

**So a consumer's upstream watch is four things**, and nothing else in this
repository needs watching:

| Watch | Because |
|---|---|
| `CHANGELOG.md` | it says what moved, in these terms |
| `lib/agent-core/include/agent/IMesh.h`, `IUplink.h`, `IPlatform.h` (and `IJobExtension.h`) | they are your code's contract |
| `lib/agent-core/library.json` | the version, and ArduinoJson's exact version — the one dependency you also have to resolve |
| `tests/fixtures/contracts/MANIFEST.txt` | the dialect's commit |

A diff of those between two tags is the whole of what a bump costs you.

## 11. What the first consumer implements

The first consumer is the Walter of a repeater-v2 unit
(`twinoak.meshcore.walter-firmware`): an ESP32-S3 with an LTE-M modem and a
solar panel, whose mesh is a RAK on a serial link and whose platform is the
ESP-IDF. Its phase 4 is three implementations and a wiring, and this is the whole
of it, method by method.

**`IMesh` → one request of `LINK-PROTOCOL.md` §6** (and §7 for one of them). The
link was designed against this list (`SYSTEM.md` S-4), which is why the mapping is
this flat:

| `IMesh` | On the link |
|---|---|
| `setRing(ring)` | **§7 `OBS_PULL`** — the Walter pulls the RAK's advert ring into this one between jobs. Not §6 |
| `counters()` | §6.4 `MESH_COUNTERS`: `rx` ↔ `rx`, `tx` ↔ `client_tx` (one per send; the link's own `tx` counts forwards too, which an exit node on a repeater must not report as its own), `adverts` ↔ `adverts`, `advertsDropped` ↔ `adverts_dropped`. `client_tx` and the runner's transaction count must be **equal, exactly** — that equality is the one-send-per-command rail made checkable, and it is worth an assertion in every scenario |
| `overheard()` | **nothing carries it.** §6.4 has neither an `overheard` figure nor a count by type — the one open gap, in this pile's `FOLLOWUPS.md` for the design repository. One `u32` in §6.4 closes it |
| `loop()` | no request: read the link, dispatch its events |
| `supports(type)` | no request: the Walter answers from what the link carries. §6.2 has no `SEND_TRACE` the UART link can complete for the unit's own radio, so `"trace"` is `false` and the unit never advertises it — which is its p4-02 *local door*, settled this way: `unsupported` at no transaction with nothing sent, not `failed` on the air. Every other type `true` |
| `beginJob(key, hashes, hops, hashSize)` | §6.1 `JOB_BEGIN` — `pubkey` 32 bytes from the hex, then a path: `path_mode` 2 with `hash_size`/`hop_count`/`hashes` when the claim carried one, 0 when it did not. The link ends any job in progress first, as this interface requires |
| `endJob()` | §6.1 `JOB_END` |
| `setDirectPath()` / `forgetPath()` | §6.1 `PATH_SET`, `path_mode` 1 / 0 |
| `hasPath()`, `pathHopCount()`, `describePath()` | §6.1 `PATH_GET` — and a reply that came back as a returned path changes it, which is how the Walter reads the route a flood discovered |
| `sendLogin(password, tag&)` | §6.2 `SEND_LOGIN`, `password` 0–15 bytes, blank for the ACL login. `tag` out is 0 |
| `sendRequest(type, data, len, tag&)` | §6.2 `SEND_REQUEST`; `tag` is the timestamp the RAK drew |
| `sendCli(text, sensitive, timestamp&)` | §6.2 `SEND_CLI`, `flags` bit 0 = `sensitive`; the response's `sender_ts` is what `timestamp` receives — **this is why the interface returns it** |
| `sendDiscovery(tag&)` | §6.2 `SEND_DISCOVERY`, always flooded |
| `sendTrace(hashes, bytes, hashSize, tag&)` | §6.2 `SEND_TRACE`, `hash_size` 1, 2, 4 or 8. The link's `ERR_RANGE` for 3 is this interface's `Failed`, and the library refuses a three-byte path before either |
| `awaitReply(tag, timeoutMs, pump)` | §6.2's `timeout_ms` at the send, then §6.3 `EV_MESH_REPLY` with `REPLY_GET` as the fallback for a lost event. **The one shape difference**: the link takes the timeout *at the send* and this interface takes it at the await. Pass a ceiling at the send — the longest the library ever waits, `perHopMs × 4` or `loginMs` — and run the real timer over the event here, dropping a late reply as the next send would. Worth a sentence in §6.2 (same `FOLLOWUPS.md` item) |
| `awaitText(timeoutMs, pump)` | the same, state 4 |
| `reply()` | §6.3's reply record: `kind` ↔ `state` 3 `Response`, 4 `Text`, 5 `Trace` (state 2, timed out, is `None` and a false return from the await), `tag`, `data`/`len` ↔ state 3's `data`, `text` ↔ state 4's `text`, the six `trace*` fields ↔ state 5's. The link carries four things `MeshReply` has no field for since `v0.3.0` — `via_path`, `snr_q4`, `elapsed_ms` and `rssi` — because nothing in the envelope takes them; print them if you want them, and the library times its own round trip |

**`IUplink` → an HTTPS call through the modem**, with `Authorization: Bearer` and
`X-Agent-Version` on every one (§4), and the chain verified against a root the
image ships:

| `IUplink` | The call |
|---|---|
| `fetchConfig()` | `GET /agent/v1/config` → `mm::parseAgentConfig` |
| `postHeartbeat(hb, reply&)` | `POST /agent/v1/heartbeat`, body from `mm::writeHeartbeat` → `mm::parseHeartbeatReply` |
| `postObservations(body, len, …)` | `POST /agent/v1/observations`, body from `mm::writeObservationBatch` → `mm::parseObservationsReply` |
| `claimJobs(jobs, max, count&)` | `POST /agent/v1/jobs/claim`, body from `mm::writeClaimRequest` → `mm::parseClaimResponse` |
| `postJobResult(id, body, len)` | `POST /agent/v1/jobs/{id}/result`, body from `mm::writeJobResult`; 200 or keep it for later |
| `extendLease(id, seconds, until&)` | `POST /agent/v1/jobs/{id}/extend`, body from `mm::writeExtendRequest` → `mm::parseExtendReply`; 409 is false |
| `postProgress(id, body, len)` | `POST /agent/v1/jobs/{id}/progress`, body from `mm::writeProgressReport` → `mm::parseProgressReply` |
| `configured()`, `link()`, `limiter()`, `last()`, `lastCall()` | the modem's own state, a `mm::RateLimiter` of 120 a minute, and the `Call` record of §4.1 |

**`IPlatform` → the ESP-IDF**, and in the Walter's case mostly onto interfaces it
already has (`components/interfaces/`, `components/platform/`):

| `IPlatform` | Its source |
|---|---|
| `millis()` | `esp_timer_get_time() / 1000`, which is `interfaces::Clock::nowMs()` already. It restarts at zero after a deep sleep — that is what `WakeState` is for |
| `delayMs(ms)` | `vTaskDelay(pdMS_TO_TICKS(ms))`, which is `interfaces::Clock::delayMs()` |
| `random()` | `esp_random()` |
| `clock()` | `interfaces::WallClock` behind `mm::IClock`: `now(WallTime*)` gives the epoch and its source, `fromNetwork()` is source 1 (the cellular network) or 2 (the server), `sourceName()` is that as a word, and `takeServerTime()` is the heartbeat reply offering a time — which for a unit with no RTC fitted is often the first time it has |
| `freeHeap()`, `minFreeHeap()` | `esp_get_free_heap_size()`, `esp_get_minimum_free_heap_size()` |
| `rebootForHealth(why)` | `interfaces::Restart` (`esp_restart()`) — but in one-wake mode this is **never called**; the supervisor reads `report.rebootWanted` instead |
| `selfReboots()` | the health record in retained storage (`interfaces::Retained`, `RetainedSlot::kHealth`), which already counts boots and keeps the last reasons |

**`IRetained` → a flash region of the Walter's own** (§5.1), not a retained
slot: its RTC SLOW memory had 36 bytes free after phase 3 (its report 05, *What
RTC memory costs, again*), and a full image is about 19.5 kB. The three methods
map onto a partition the size of one image or a little more: `capacity()` is the
region's size, `write()` is an erase and a write of the image's bytes, `read()`
is a read of the whole region (the image says its own length and carries its own
CRC-32 — `esp_rom_crc32_le` computes the same one, should the Walter want to
check it itself). An NVS blob does as well at a few writes a day. The scratch is
the Walter's RAM, 20 kB at namespace scope; `restoreKept()` goes right after
`restore()` in step 8's set-up, and the write at the end of `wake()` needs
nothing of the Walter's. What the image never holds: the token, a password, a
message — nothing a result does not.

**And the wiring**: `AgentLoop::wake()` is step 8 of the Walter's `WakeCycle`
(`tasks/phase-3/05-the-wake-cycle.md`, `docs/ARCHITECTURE.md` §10) — the step that
task left as a `Backend` seam with a `NullBackend` behind it, deliberately,
because the server side was three proposals and not a decision. `WakeIntent`
comes from the interval policy of step 5 and the band; `Budget` from what is left
of the wake's seconds and the LTE plan; `WakeState` goes in a retained slot of
its own (76 bytes, inside the 128 a record may be) and comes back before the
wake.

What that mapping does *not* close, on the link's side: `overheard()` and the
awaited-reply timeout, both noted above and both items for the design
repository rather than library changes. Anything else the agent needs that §6
cannot carry is a specification gap for that repository too — the library is not
where it gets worked around. On *this* library's side the open list is §7.1's
two, and the same `FOLLOWUPS.md` holds both.

## 12. Testing yours

[`docs/host.md`](host.md) is the page, and the short version is that you do not
have to write a repeater, a server or a clock: `test/support/` holds all three
behind the same three interfaces — `FakeMesh` (a stock repeater at the MeshCore
pin, every answer cited to a fixture or to a firmware function with its line
range), `FakeUplink` (a scripted meshmaster.dk with faults), `FakePlatform` (a
clock that only moves when the test says, so a wake's bounds are asserted in
milliseconds nobody waited for), and since pile 1.6 `FakeRetained` (a store over
a vector, whose image a test can tear, flip a bit of, or count the writes to).
They are header-only and include nothing but the library, GoogleTest and the
standard library, and they are written to be copied into another tree.

Two things to know before you do: the two fixture directories come in as the
defines `FIXTURES_DIR` and `COMPANION_DIR`, which you point at your own synced
copy; and `FakeMesh` answers as the *in-process* mesh does, so a firmware whose
mesh is a link client has one more layer to fake — the link — with `FakeMesh` on
the far side of it rather than behind `IMesh`. The scenarios still apply; the
object under test changes.

The suites to read first are `test/test_runner` (46 cases: all thirteen job
types, one send per command, the credential wiped, the lease, the offline buffer,
a type the mesh cannot carry) and `test/test_loop` (47: the loop, the heartbeat's
bytes and its `transport`, the advertised list less what the mesh declines, the
backoff, the reboot guard, every wake case, and what a sleep keeps — a sleep
there is the loop and the runner destroyed and built again from the state and
the image).
