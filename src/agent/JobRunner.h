// One job at a time: claim, run, report (EXIT-NODES §5, §6, §7).
//
// The .NET agent's JobRunner has a companion on a cable and a handler per type; here the board
// **is** the companion, so each handler is a direct call into MeshCore's mesh layer - the same
// functions the companion firmware runs when a phone asks - and the reply arrives in the
// callbacks src/mesh/AgentMesh.h already has. Simpler on the wire, and the same result shapes on
// the server: what is posted is held to the .NET agent's bytes by test/test_job_results.
//
// Four types, and their whole repertoire is a login that carries no password:
//
//   status      REQ_TYPE_GET_STATUS, and at class `acl` the owner-info request too, for the
//               firmware version the association's verify step records
//   telemetry   REQ_TYPE_GET_TELEMETRY_DATA, Cayenne LPP decoded with the channel-0 rule
//   neighbours  REQ_TYPE_GET_NEIGHBOURS, nine a page, eight-byte prefixes, paged to the count
//   trace       a trace packet along the round trip; `unsupported` at no transaction when the
//               path is built of three-byte hashes, which a trace packet cannot express
//
// The pacing is the budget the heartbeat last carried (agent/Airtime.h): one transaction at a
// time, the gap between two, the ceiling in a minute. A board has one thread, so a job's waiting
// is the loop's waiting - `pump` is what keeps the console and the OLED alive through it.

#pragma once

#include <Stream.h>

#include <cstddef>
#include <cstdint>

#include "Airtime.h"
#include "AgentClient.h"
#include "JobEnvelopes.h"
#include "Sessions.h"
#include "mesh/AgentMesh.h"
#include "net/Clock.h"

namespace mm {

class JobRunner {
 public:
  // What `jobs` prints: the last ten, newest first.
  struct Record {
    long long id = 0;
    char type[kMaxJobTypeLen + 1] = {0};
    Outcome outcome = Outcome::Failed;
    int transactions = 0;
    uint32_t atEpoch = 0;
    bool posted = false;
  };
  static constexpr int kRemembered = 10;

  void begin(AgentMesh& mesh, AgentClient& server, AgentClock& clock, void (*pump)());
  void setIdentity(const char* publicKeyHex);
  void setBudget(const AgentBudget& budget) { gate_.setBudget(budget); }

  // One claim and everything it hands over, each result posted as the job ends. Returns how many
  // jobs ran, or -1 when the claim itself did not come back - the caller's backoff then applies.
  int claimAndRun();

  // A result whose POST did not get through is kept and retried; this is how many are waiting,
  // which is the heartbeat's `queueDepth`.
  int queueDepth() const { return pendingLen_ > 0 ? 1 : 0; }

  const AirtimeGate& gate() const { return gate_; }
  uint32_t claims() const { return claims_; }
  uint32_t ran() const { return ran_; }
  uint32_t postFailures() const { return postFailures_; }

  // `jobs` on the console: the last ten with their outcome and their transactions.
  void report(Stream& out) const;

  // One line for `status`.
  void describe(char* out, size_t cap) const;

 private:
  // What one job is while it runs.
  struct Context {
    const ClaimedJob* job = nullptr;
    int transactions = 0;
    bool hasRtt = false;
    int rttMs = 0;
    bool sessionWasFresh = false;
    bool hasSession = false;
    uint8_t sessionRole = 0;
    bool grantedBelowClass = false;
    uint8_t grantedRole = 0;
  };

  Outcome runJob(const ClaimedJob& job, Context& ctx, size_t& dataLen);
  Outcome runStatus(const ClaimedJob& job, Context& ctx, size_t& dataLen);
  Outcome runTelemetry(const ClaimedJob& job, Context& ctx, size_t& dataLen);
  Outcome runNeighbours(const ClaimedJob& job, Context& ctx, size_t& dataLen);
  Outcome runTrace(const ClaimedJob& job, Context& ctx, size_t& dataLen);

  // A login at the job's class if none is live. Returns Ok when there is a session to use.
  Outcome ensureSession(const ClaimedJob& job, Context& ctx);
  // One request, with the bounded retry of EXIT-NODES §5: no reply once is retried once, and on
  // a reused session the retry logs in again first. Silence twice is `unreachable`.
  Outcome request(const ClaimedJob& job, Context& ctx, const char* what, uint8_t reqType, const uint8_t* data,
                  uint8_t len);
  // The budget's turn to speak, and the lease's. False when the lease ran out first.
  bool spend(const ClaimedJob& job, Context& ctx);
  void idle(uint32_t ms);
  uint32_t requestWaitMs(const ClaimedJob& job) const;
  uint32_t loginWaitMs(const ClaimedJob& job) const;
  bool leaseRanOut(const ClaimedJob& job) const;
  void randomBytes(uint8_t* out, size_t len);
  void remember(const ClaimedJob& job, Outcome outcome, int transactions, bool posted);
  bool post(long long jobId, const char* body, size_t len);
  void flushPending();

  AgentMesh* mesh_ = nullptr;
  AgentClient* server_ = nullptr;
  AgentClock* clock_ = nullptr;
  void (*pump_)() = nullptr;
  const char* publicKey_ = nullptr;

  AirtimeGate gate_;
  SessionStore sessions_;

  Record recent_[kRemembered];
  int recentCount_ = 0;

  uint32_t claims_ = 0;
  uint32_t ran_ = 0;
  uint32_t postFailures_ = 0;

  // The one result whose POST has not got through yet. Its client id never changes, so the
  // server de-duplicates it however many times it arrives (API §3).
  long long pendingJobId_ = 0;
  size_t pendingLen_ = 0;
};

}  // namespace mm
