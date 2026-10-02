// One job at a time: claim, run, report (EXIT-NODES §5, §6, §7).
//
// The .NET agent's JobRunner has a companion on a cable and a handler per type; here the board
// **is** the companion, so each handler is a direct call into MeshCore's mesh layer - the same
// primitives the companion firmware runs when a phone asks - and the reply arrives in the
// callbacks src/mesh/MeshPort.h names. Simpler on the wire, and the same result shapes on the
// server: what is posted is held to the .NET agent's bytes by test/test_job_results and
// test/test_config_read.
//
// Since task p1.1-08 this file talks to two interfaces and not to two classes: `MeshPort` is the
// board's mesh (src/mesh/AgentMesh.h) or the fake repeater of the host harness
// (src/host/HostMesh.h), and `ServerPort` is the board's verified-TLS client
// (src/agent/AgentClient.h) or the harness's plain-HTTP one (src/host/HostClient.h). Everything
// below is the same code in both, which is the point: what `pio run -e host` runs against a fake
// mesh is this file and not a port of it (docs/host.md).
//
// Thirteen types. Four of them log in as this board and nothing else (task 03):
//
//   status      REQ_TYPE_GET_STATUS, and at class `acl` the owner-info request too, for the
//               firmware version the association's verify step records
//   telemetry   REQ_TYPE_GET_TELEMETRY_DATA, Cayenne LPP decoded with the channel-0 rule
//   neighbours  REQ_TYPE_GET_NEIGHBOURS, nine a page, eight-byte prefixes, paged to the count
//   trace       a trace packet along the round trip; `unsupported` at no transaction when the
//               path is built of three-byte hashes, which a trace packet cannot express
//
// and three carry the repeater's admin password for the length of the job (task 04; where it
// lives and what may see it is agent/Credentials.h):
//
//   discover    an admin login - direct along a known path, which verifies it; flooded when
//               none is known, and the path its reply came back along - and the path-discovery
//               request last, only when neither login was answered (tasks p3.5-39, 40, 41)
//   acl.set     `setperm <key> <level>` on the admin session, the reply captured; on our own
//               key the command is the session's level and the cache follows it (task p4.1-60)
//   config.read the `get` list in order, `region` and `region default`, `ver` and `board`, and
//               the access-list request last (admin only, 23 entries at most, its silence after
//               keys were read tolerated: tasks p3.5-43, p4-49, p4-57)
//
// and six change a repeater (task 05; tasks p4-53, 54, 55, 57), every one of them one send per
// command - a `set` sent twice is a change made twice - on an admin session, with the sender
// timestamps strictly increasing (the RTC's unique second, which every tag and every CLI line of
// this board come from):
//
//   config.apply  the plan's commands in order, each exactly once, every reply kept, stopped at
//                 the first reply that is not what the line expects and at the first silence
//                 (`not-sent` for the rest); a regions entry as its lines, the ones that expect
//                 no reply sent without waiting and the terminator sent even when the sequence
//                 dies; then the re-read of every touched key and `ver`. The policy is
//                 agent/ConfigApply.h, which runs on a host; this file puts its lines on the air
//   cli           one line as the server handed it, one reply, both recorded; the blocked list
//                 is the server's, and this firmware refuses nothing but a malformed line
//   advert.request  `advert` (the flood) or `advert.zerohop`, answered `OK - Advert sent`
//   clock.sync    `time <unix>` from this board's clock, the reply recorded, a refusal
//                 (`(ERR: clock cannot go backwards)`) recorded as the refusal it is
//   password.set  `password <new>` or `set guest.password <new>` with the value the claim
//                 carried, once; the admin echo compared with what was sent and recorded as
//                 `<set>`, never the value; the restore **after** the password line, and the
//                 session dropped whatever came back
//   reboot        `reboot`, which the firmware never answers: `ok` as sent on silence, no
//                 restore (there is no session after it, and before it a restore would make the
//                 command unanswerable), the session dropped
//
// **Every admin session on a repeater where this exit node holds a grant ends with the restore**
// (task p4.1-60): `setperm <own key> <granted level>` as the session's last command, because on
// stock firmware the login table *is* the ACL and the admin login has just rewritten our entry
// to admin. The level travels in the claim's `acl`; after the command the cache holds the
// session at class `acl`, reusable by the next acl-class job and invisible to an admin-class
// one. A blank login the repeater answered *above* the grant restores the same way, at once. An
// `acl.set` on our own key sends no second `setperm`: the command is the level. Two types cannot
// end that way (task 60 as built): `password.set` restores after its password line and only then
// drops the session, and `reboot` restores not at all.
//
// The pacing is the budget the heartbeat last carried (agent/Airtime.h): one transaction at a
// time, the gap between two, the ceiling in a minute. A board has one thread, so a job's waiting
// is the loop's waiting - `pump` is what keeps the console and the OLED alive through it.

#pragma once

#include <Stream.h>

#include <cstddef>
#include <cstdint>

#include "Airtime.h"
#include "ConfigApply.h"
#include "ConfigRead.h"
#include "JobEnvelopes.h"
#include "ResultBuffer.h"
#include "ServerPort.h"
#include "Sessions.h"
#include "mesh/MeshPort.h"
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

  void begin(MeshPort& mesh, ServerPort& server, AgentClock& clock, void (*pump)());
  void setIdentity(const char* publicKeyHex);
  void setBudget(const AgentBudget& budget) { gate_.setBudget(budget); }

  // One claim and everything it hands over, each result posted as the job ends. Returns how many
  // jobs ran, or -1 when the claim itself did not come back - the caller's backoff then applies.
  // Every job's credential is wiped when the job ends, whatever it came to.
  int claimAndRun();

  // The offline buffer (task 06): results the server did not take are kept and posted later,
  // oldest first, with the timestamps and the client ids they were made with. Called by the
  // agent loop after every heartbeat the server answered - "the offline buffer is posted first,
  // in order" (EXIT-NODES §5, docs/ops/agent.md §4). Safe to call with nothing waiting.
  void flushResults();

  // How many results are waiting to be posted, which is the heartbeat's `queueDepth`.
  int queueDepth() const { return static_cast<int>(results_.count()); }

  const AirtimeGate& gate() const { return gate_; }
  const ResultBuffer& results() const { return results_; }
  uint32_t claims() const { return claims_; }
  uint32_t ran() const { return ran_; }
  uint32_t postFailures() const { return postFailures_; }
  uint32_t resultsDropped() const { return results_.dropped(); }

  // `jobs` on the console: the last ten with their outcome and their transactions.
  void report(Stream& out) const;

  // `sessions` on the console: per repeater, the class, the level and the age. No secret is in
  // a session - a key, a class, a role byte and a time.
  void reportSessions(Stream& out) const;

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
    // The restore (task p4.1-60): sent once per job, by the handler that needs its verdict in
    // the result (config.read) or by the runner after the handler.
    bool aclRestoreDone = false;
  };

  // What one login came to. `Refused` is a reply that was not a login-ok, and a login answered
  // below the class it was for (task p3.5-41); `NoReply` is what stock firmware answers a key
  // and password it does not accept.
  enum class LoginResult : uint8_t { Ok, Refused, NoReply, Failed, Timeout };

  Outcome runJob(const ClaimedJob& job, Context& ctx, size_t& dataLen);
  Outcome runStatus(const ClaimedJob& job, Context& ctx, size_t& dataLen);
  Outcome runTelemetry(const ClaimedJob& job, Context& ctx, size_t& dataLen);
  Outcome runNeighbours(const ClaimedJob& job, Context& ctx, size_t& dataLen);
  Outcome runTrace(const ClaimedJob& job, Context& ctx, size_t& dataLen);
  Outcome runDiscover(const ClaimedJob& job, Context& ctx, size_t& dataLen);
  Outcome runAclSet(const ClaimedJob& job, Context& ctx, size_t& dataLen);
  Outcome runConfigRead(const ClaimedJob& job, Context& ctx, size_t& dataLen);
  // The writes (task 05).
  Outcome runConfigApply(const ClaimedJob& job, Context& ctx, size_t& dataLen);
  Outcome runCli(const ClaimedJob& job, Context& ctx, size_t& dataLen);
  Outcome runAdvertRequest(const ClaimedJob& job, Context& ctx, size_t& dataLen);
  Outcome runClockSync(const ClaimedJob& job, Context& ctx, size_t& dataLen);
  Outcome runPasswordSet(const ClaimedJob& job, Context& ctx, size_t& dataLen);
  Outcome runReboot(const ClaimedJob& job, Context& ctx, size_t& dataLen);
  // The admin session every write needs: a login answered below admin is `unauthorized` at the
  // login, and nothing is sent (a CLI line is answered to admin only on this firmware).
  Outcome adminSession(const ClaimedJob& job, Context& ctx);
  // The apply engine's lines on the air: agent/ConfigApply.h's exchange over this runner.
  class ApplyOnTheAir;
  friend class ApplyOnTheAir;

  // The pieces of a config read.
  Outcome readRegions(const ClaimedJob& job, Context& ctx, ConfigSnapshot& snapshot);
  Outcome readAccessList(const ClaimedJob& job, Context& ctx, ConfigSnapshot& snapshot);
  // The discover's result, from the path the mesh holds now.
  Outcome learned(const ClaimedJob& job, Context& ctx, LoginResult from, const char* source, size_t& dataLen);

  // The login material of the job's class: the password the claim carried, "" for the ACL login
  // (class acl and none), nullptr when the class needs one and the claim carried none.
  const char* loginMaterial(const ClaimedJob& job) const;
  // One login: counted, logged, the session bookkeeping a reply implies, and no policy beyond
  // that. ensureSession() is this twice; a discover is this once, for the path.
  LoginResult loginOnce(const ClaimedJob& job, Context& ctx, const char* material, const char* what);
  // A login at the job's class if none is live. Returns Ok when there is a session to use.
  Outcome ensureSession(const ClaimedJob& job, Context& ctx);
  // One request, with the bounded retry of EXIT-NODES §5: no reply once is retried once, and on
  // a reused session the retry logs in again first. Silence twice is `unreachable`.
  Outcome request(const ClaimedJob& job, Context& ctx, const char* what, uint8_t reqType, const uint8_t* data,
                  uint8_t len);
  // One CLI line, the same way; the reply's text lands in the runner's own buffer. With `retry`
  // false it is sent once whatever comes back, which is what a write needs. `timestamp` is the
  // sender timestamp of the first attempt (0: the RTC's unique second, which a second attempt
  // always takes); `sensitive` stars the frame's body in the frame log (a `password` line);
  // `escape` is the one line that goes out past the lease, the region loader's terminator.
  Outcome cliLine(const ClaimedJob& job, Context& ctx, const char* what, const char* command, bool retry,
                  uint32_t timestamp = 0, bool sensitive = false, bool escape = false);
  // The common shape of the two: the send, the wait, the retry and the re-login.
  template <typename Send>
  Outcome exchange(const ClaimedJob& job, Context& ctx, const char* what, bool retry, bool text, Send send,
                   bool escape = false);
  // The session's last command on a pair with a grant (task p4.1-60). Returns 1 when the
  // repeater confirmed the restore, 0 when it was sent and not confirmed (or could not be), -1
  // when there was nothing to restore.
  int restoreAcl(const ClaimedJob& job, Context& ctx, Outcome outcome);
  // The budget's turn to speak, and the lease's. False when the lease ran out first - unless
  // `escape`: the region loader's terminator waits for the budget and ignores the lease, because
  // a repeater left in load mode reads every later CLI command as a region.
  bool spend(const ClaimedJob& job, Context& ctx, bool escape = false);
  void idle(uint32_t ms);
  uint32_t requestWaitMs(const ClaimedJob& job) const;
  uint32_t loginWaitMs(const ClaimedJob& job) const;
  bool leaseRanOut(const ClaimedJob& job) const;
  void randomBytes(uint8_t* out, size_t len);
  void remember(const ClaimedJob& job, Outcome outcome, int transactions, bool posted);
  bool post(long long jobId, const char* body, size_t len);
  // Every remembered job with this id is marked posted, once its result has gone through.
  void markPosted(long long jobId);

  MeshPort* mesh_ = nullptr;
  ServerPort* server_ = nullptr;
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

  // The results whose POST has not got through yet, oldest first. Each keeps the client id it
  // was written with, so the server de-duplicates it however many times it arrives (API §3), and
  // the buffer is bounded and drops from the oldest end (ResultBuffer.h).
  ResultBuffer results_;
};

}  // namespace mm
