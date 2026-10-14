// The agent's loop: the part of EXIT-NODES §5 and §6 a board with no jobs does.
//
// Once the link is up: GET /agent/v1/config once, then POST /agent/v1/heartbeat every
// `pollIntervalS` the server last answered (30 when work waits, 120 when none does), and
// between heartbeats POST /agent/v1/observations with the adverts the radio heard - a batch when
// twenty are waiting or when the oldest has waited a minute, whichever comes first.
//
// Since task 03 a heartbeat that answers `jobsWaiting` is followed by a claim: up to two jobs,
// run one at a time at the budget's pace, each result posted as its job ends (JobRunner.h).
//
// Since task 06 the order inside one pass is the .NET agent's, in its own words
// (docs/ops/agent.md §4): the heartbeat, then **the offline buffer** - anything that waited is
// posted first, in order, with the timestamps it was made with - then the claim, then the
// observation batch. And the wait after a failure carries jitter (src/Backoff.h), so a hundred
// boards that lost the same server do not all come back at the same second; past
// `kRebootAfterFailures` consecutive unanswered calls **with the link up**, over
// `kRebootAfterFailingMs`, the board restarts itself and says why (IPlatform::rebootForHealth,
// which is src/Health.h on the Heltec), because a TLS stack that has failed for an hour and a
// half against a live link is not going to recover by being asked again - and the hour and a
// half is a condition of its own since task p1.2-10 lowered the proven link's backoff ceiling
// to two minutes.
//
// What the server's answers do here:
//   200   the budget, the poll interval and the clock are taken, and `jobsWaiting` makes a claim
//         due before the next heartbeat
//   401   everything stops. The token was revoked or is wrong, and no amount of retrying fixes
//         either; the console still works and the OLED's fourth line says so, and a
//         `set meshmaster.token`
//         (or a reboot) is what starts the loop again
//   429   the wait is `Retry-After`, to the second
//   502, 503, 504 with a Retry-After
//         the same: a deploy of meshmaster.dk says how long it will be, and that is believed
//         rather than doubled past it (task p1.2-10)
//   5xx, no reply
//         backoff, 30 s doubling to **two minutes** once this board's own link is proven - the
//         link up and a time off the network - and to ten minutes while it is not, reset by the
//         first answer
//
// `jobTypes` is the thirteen types JobRunner has a handler for - every type of API §3 since
// task 05 - and the types of the extension the board set, if any (IJobExtension.h); routing
// sends nothing else, because it requires the type advertised.
//
// Host-portable since task p1.5-15: the server, the platform and the mesh are the three seams
// (IUplink.h, IPlatform.h, IMesh.h), and what the heartbeat says about this board - the version,
// the board, the companion firmware, the radio - comes in as values (HeartbeatIdentity) that the
// board fills and refreshes, rather than being read from its settings here.

#pragma once

#include <cstddef>
#include <cstdint>

#include "Envelopes.h"
#include "IMesh.h"
#include "IPlatform.h"
#include "IUplink.h"
#include "JobRunner.h"
#include "Observations.h"
#include "ServerPolicy.h"
#include "TextOut.h"

namespace mm {

// What earns a reboot: consecutive server failures - with the link up, so the link is not the
// reason - for long enough that the stack and not the server is the suspect. Task 06 wrote the
// count alone, and twelve of them at a backoff doubling to ten minutes *was* about an hour and a
// half; task p1.2-10 lowered that ceiling to two minutes for a board whose link is proven, which
// would have brought twelve within about twenty - and twenty minutes of a server being down is
// an ordinary deploy, not a wedged stack. So both have to be true, and the sentence task 06
// wrote stays true of either backoff: a board reboots at most about once an hour and a half
// through a long outage, and once is usually all a wedged stack needs. What a reboot costs is
// the buffers (the advert ring and the results waiting to be posted late), which is exactly what
// a server outage fills. (Moved here from src/Health.h by task p1.5-15; the restart itself is
// the platform's, IPlatform::rebootForHealth.)
constexpr uint32_t kRebootAfterFailures = 12;
constexpr uint32_t kRebootAfterFailingMs = 90u * 60000u;

// What the heartbeat says about this board, as values the board fills (main.cpp) and refreshes
// when a `radio.*` setting changes: the library reads no setting of its own.
struct HeartbeatIdentity {
  const char* agentVersion = "";           // meshmaster_agent_version()
  const char* board = nullptr;             // boardName(); nullptr leaves the field out
  const char* companionFirmware = nullptr; // meshcoreTag() on the Heltec, whose companion firmware is the one it is built from
  RadioParameters radio;                   // what the radio is programmed with; sent while there is an identity
};

class AgentLoop {
 public:
  struct Counters {
    uint32_t heartbeats = 0;           // answered 200
    uint32_t httpFailures = 0;         // a call that did not come back 2xx, 401 and 429 apart
    uint32_t wifiReconnects = 0;       // links after the first
    uint32_t batches = 0;              // observation batches the server took
    uint32_t observationsPosted = 0;
    uint32_t observationsRefused = 0;  // a batch the server refused for good, and the ring dropped
    uint32_t claimFailures = 0;        // a claim that did not come back
    uint32_t resultsPosted = 0;        // results that went through from the offline buffer, late
    uint32_t consecutiveFailures = 0;  // calls that got no answer since the last one that did
  };

  // `identity` is read at every heartbeat, so the board keeps the object and refreshes it.
  void begin(IUplink& server, IPlatform& platform, IMesh& mesh, JobRunner& jobs, const HeartbeatIdentity& identity);

  // The board's own identity, as 64 lowercase hex, or nullptr while the radio has made none.
  void setIdentity(const char* publicKeyHex);

  void loop();

  // One heartbeat, from inside a job (task 06). A claim is run to the end before `claimAndRun`
  // returns and a job is paced by the airtime budget, so a config.read of twenty-one transactions
  // at two a minute is eleven minutes and a region load is thirteen - during which, before this, no
  // heartbeat could go and the exit node's page went *quiet* and then *offline* while the board
  // was working perfectly (the note tasks 03, 04 and 05 each left for this one). The job's own
  // waits are where the time is, so the pump that keeps the console and the OLED alive through
  // them sends the heartbeat that is due as well. Nothing else of the loop runs there: no claim,
  // no batch, one blocking call inside a wait that was going to be spent waiting.
  void heartbeatIfOverdue();

  // A new link (WiFi associated, on the Heltec): the config call is due again, and the heartbeat
  // schedule starts over.
  void onLinkUp();

  // After `set meshmaster.*` or `erase confirm`: forget a 401 and start again.
  void reconfigure();

  ObservationRing& ring() { return ring_; }
  const Counters& counters() const { return counters_; }
  bool halted() const { return halted_; }

  // What the OLED's four lines need of this loop (src/ui/DisplayLines.h). Every one of them is
  // a plain read of state this object already keeps, safe to call before begin() has run - the
  // first frame is drawn from main.cpp's setup() and the panel has no reason to know.
  bool everHeartbeated() const { return everHeartbeated_; }
  uint32_t lastHeartbeatAtEpoch() const { return lastHeartbeatAtEpoch_; }
  // *Online* is the last call to MeshMaster having been answered and nothing standing between
  // this board and the next call: a heartbeat has happened, nothing has gone unanswered since,
  // the token has not been refused, and there is no wait running. The last of those is what a
  // deploy's 503 makes: a believed `Retry-After` is deliberately **not** a consecutive failure
  // (task p1.2-10, and the heartbeat's counters are unchanged by this), but a server that has
  // said *come back in 45 s* has not answered either, so the panel must not read *Online: yes*
  // through it (task p1.3-12).
  bool online() const;
  // Seconds until the next attempt while backing off (or holding a 429's Retry-After); 0 when
  // the wait is over or there is none.
  uint32_t retrySeconds() const;

  // `server` on the console: the last heartbeat, the next, the last batch, the counters.
  void report(TextOut& out) const;

  // `errors` on the console: every counter the heartbeat carries, and the ones beside them.
  void reportErrors(TextOut& out) const;

  // One line for `status`.
  void describe(char* out, size_t cap) const;

 private:
  void doConfig();
  void doHeartbeat();
  void doPendingResults();
  void doObservations();
  void doJobs();
  void buildHeartbeat(Heartbeat& hb);
  // Acts on what the answer means (ServerPolicy.h) and says which action that was.
  ServerAction noteFailure(const IUplink::Call& call);
  // The link up and a time that came off the network: the ceiling the backoff doubles to
  // (ServerPolicy.h's maxBackoffMs) is the lower one when this is true.
  bool linkProven() const;
  void succeeded();
  size_t buildBatch(uint32_t nowMs, char* out, size_t cap);
  uint32_t epochOf(uint32_t heardAtMillis, uint32_t nowMs) const;
  // The platform's millisecond clock, under the name every line of AgentLoop.cpp already used
  // when it was Arduino's.
  uint32_t millis() const { return platform_->millis(); }

  IUplink* server_ = nullptr;
  IPlatform* platform_ = nullptr;
  IClock* clock_ = nullptr;  // platform_->clock(): read for the timestamps, offered the server's time
  IMesh* mesh_ = nullptr;
  JobRunner* jobs_ = nullptr;
  const HeartbeatIdentity* identity_ = nullptr;

  ObservationRing ring_;
  Counters counters_;
  const char* publicKey_ = nullptr;
  // The heartbeat's `jobTypes`: the thirteen, then the extension's (IJobExtension.h).
  const char* jobTypes_[kJobTypeCount + kMaxExtensionTypes] = {};

  bool halted_ = false;         // a 401: nothing but the console from here
  bool inHeartbeat_ = false;    // a heartbeat is on the wire: the job pump may not start another
  bool configDue_ = false;
  uint32_t configAt_ = 0;
  bool heartbeatScheduled_ = false;
  uint32_t nextHeartbeatAt_ = 0;
  uint32_t holdUntil_ = 0;      // a Retry-After the answer named, or the backoff after a failure
  uint32_t backoffMs_ = 0;
  uint32_t firstFailureAtMillis_ = 0;  // the start of the present run of unanswered calls

  AgentBudget budget_;          // AgentBudget.Default until the server says otherwise (begin())
  int pollIntervalS_ = 30;
  bool lastJobsWaiting_ = false;
  bool claimDue_ = false;

  uint32_t lastHeartbeatAtMillis_ = 0;
  uint32_t lastHeartbeatAtEpoch_ = 0;
  bool everHeartbeated_ = false;
  uint32_t lastBatchAtEpoch_ = 0;
  int lastBatchCount_ = 0;

  // A batch keeps its client id across a retry: that id is what the server de-duplicates on.
  char batchId_[40] = {0};
  size_t batchCount_ = 0;
};

}  // namespace mm
