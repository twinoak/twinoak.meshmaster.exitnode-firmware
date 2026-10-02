// The agent's loop: the part of EXIT-NODES §5 and §6 a board with no jobs does.
//
// Once WiFi is up: GET /agent/v1/config once, then POST /agent/v1/heartbeat every
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
// `kRebootAfterFailures` consecutive unanswered calls **with the WiFi link up**, the board
// restarts itself and says why (src/Health.h), because a TLS stack that has failed for an hour
// and a half against a live link is not going to recover by being asked again.
//
// What the server's answers do here:
//   200   the budget, the poll interval and the clock are taken, and `jobsWaiting` makes a claim
//         due before the next heartbeat
//   401   everything stops. The token was revoked or is wrong, and no amount of retrying fixes
//         either; the console still works and the OLED says so, and a `set meshmaster.token`
//         (or a reboot) is what starts the loop again
//   429   the wait is `Retry-After`, to the second
//   5xx, no reply
//         backoff, 30 s doubling to ten minutes, reset by the first answer
//
// `jobTypes` is the thirteen types JobRunner has a handler for - every type of API §3 since
// task 05 - and routing sends nothing else, because it requires the type advertised.

#pragma once

#include <Stream.h>

#include <cstddef>
#include <cstdint>

#include "ServerPort.h"
#include "JobRunner.h"
#include "Observations.h"
#include "ServerPolicy.h"
#include "mesh/MeshPort.h"
#include "net/Clock.h"
#include "net/WifiLink.h"
#include "settings/Store.h"

namespace mm {

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

  void begin(SettingsStore& settings, ServerPort& server, AgentClock& clock, WifiLink& wifi, MeshPort& mesh,
             JobRunner& jobs);

  // The board's own identity, as 64 lowercase hex, or nullptr while the radio has made none.
  void setIdentity(const char* publicKeyHex);

  void loop();

  // One heartbeat, from inside a job (task 06). A claim is run to the end before `claimAndRun`
  // returns and a job is paced by the airtime budget, so a config.read of twenty transactions at
  // two a minute is ten minutes and a region load is thirteen - during which, before this, no
  // heartbeat could go and the exit node's page went *quiet* and then *offline* while the board
  // was working perfectly (the note tasks 03, 04 and 05 each left for this one). The job's own
  // waits are where the time is, so the pump that keeps the console and the OLED alive through
  // them sends the heartbeat that is due as well. Nothing else of the loop runs there: no claim,
  // no batch, one blocking call inside a wait that was going to be spent waiting.
  void heartbeatIfOverdue();

  // A new WiFi link: the config call is due again, and the heartbeat schedule starts over.
  void onWifiUp();

  // After `set meshmaster.*` or `erase confirm`: forget a 401 and start again.
  void reconfigure();

  ObservationRing& ring() { return ring_; }
  const Counters& counters() const { return counters_; }
  bool halted() const { return halted_; }

  // `server` on the console: the last heartbeat, the next, the last batch, the counters.
  void report(Stream& out) const;

  // `errors` on the console: every counter the heartbeat carries, and the ones beside them.
  void reportErrors(Stream& out) const;

  // One line for `status`.
  void describe(char* out, size_t cap) const;

  // The OLED's third line: "hb 12:03 · 17 adverts" once one has been answered.
  void oledLine(char* out, size_t cap) const;

 private:
  void doConfig();
  void doHeartbeat();
  void doPendingResults();
  void doObservations();
  void doJobs();
  void buildHeartbeat(Heartbeat& hb) const;
  // Acts on what the answer means (ServerPolicy.h) and says which action that was.
  ServerAction noteFailure(const ServerPort::Call& call);
  void succeeded();
  size_t buildBatch(uint32_t nowMs, char* out, size_t cap);
  uint32_t epochOf(uint32_t heardAtMillis, uint32_t nowMs) const;

  SettingsStore* settings_ = nullptr;
  ServerPort* server_ = nullptr;
  AgentClock* clock_ = nullptr;
  WifiLink* wifi_ = nullptr;
  MeshPort* mesh_ = nullptr;
  JobRunner* jobs_ = nullptr;

  ObservationRing ring_;
  Counters counters_;
  const char* publicKey_ = nullptr;

  bool halted_ = false;         // a 401: nothing but the console from here
  bool inHeartbeat_ = false;    // a heartbeat is on the wire: the job pump may not start another
  bool configDue_ = false;
  uint32_t configAt_ = 0;
  bool heartbeatScheduled_ = false;
  uint32_t nextHeartbeatAt_ = 0;
  uint32_t holdUntil_ = 0;      // a 429's Retry-After, or the backoff after a failure
  uint32_t backoffMs_ = 0;

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
