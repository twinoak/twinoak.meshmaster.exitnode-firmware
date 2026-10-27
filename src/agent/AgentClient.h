// The HTTPS client to meshmaster.dk: the WiFi implementation of the uplink seam (IUplink.h), with
// the certificate chain verified.
//
//   GET  /agent/v1/config             once per WiFi connection, for the budget and the clock
//   POST /agent/v1/heartbeat          every pollIntervalS, the self-description of EXIT-NODES §5
//   POST /agent/v1/observations       a batch of adverts heard, client id and all
//   POST /agent/v1/jobs/claim         when the heartbeat says jobs are waiting (task 03)
//   POST /agent/v1/jobs/{id}/result   one per job, once, with its client id
//   POST /agent/v1/jobs/{id}/extend   a longer lease for a job that runs past its own (task p1.5-15;
//   POST /agent/v1/jobs/{id}/progress   nothing on the Heltec calls these two yet)
//
// How the server's certificate is checked, for every task after task 01:
//   1. The chain is verified - the client is never told to accept an unverified one, and
//      test/test_tls_policy greps the tree for the ways of telling it so.
//   2. The root is ISRG Root X1 (src/agent/TlsRoots.h), which meshmaster.dk's Let's Encrypt
//      chain ends at, bundled in the image.
//   3. If a handshake fails on verification (the chain no longer ends there), the next attempt
//      uses the ESP32 core's own certificate bundle - the Mozilla roots the core shipped with -
//      and the choice is remembered and printed. Both are verification; neither is a bypass.
// Every request carries `Authorization: Bearer <token>` and `X-Agent-Version`, and a User-Agent
// of "meshmaster-agent/<version>" like the .NET agent's. The token is read from NVS for the call
// and wiped after; a call is blocking and takes a few seconds on a cold handshake.
//
// The 120-a-minute ceiling of the server's `agent` rate-limit policy is kept on this side too
// (RateLimit.h): a call that would break it is not made.
//
// Board-only: the URL and the token are NVS's (settings/Store.h), the link is WiFi's
// (net/WifiLink.h), and the root the chain verified against is this file's business - the
// library sees it as the one word `via` of a Call.

#pragma once

#include <cstddef>
#include <cstdint>

#include <agent/IUplink.h>

#include "net/Clock.h"
#include "net/WifiLink.h"
#include "settings/Store.h"

namespace mm {

class AgentClient : public IUplink {
 public:
  enum class Root : uint8_t { IsrgRootX1, CoreBundle };

  void begin(SettingsStore& settings, AgentClock& clock, WifiLink& wifi, const char* agentVersion);

  // After `set meshmaster.*`: re-read the URL and forget the last outcome.
  void reconfigure();

  // ------------------------------------------------------------------ IUplink

  bool configured() const override { return url_[0] != '\0' && tokenSet_; }
  LinkState link() const override;

  bool fetchConfig() override;
  bool postHeartbeat(const Heartbeat& hb, HeartbeatReply& reply) override;
  bool postObservations(const char* body, size_t len, int& accepted, bool& duplicate) override;
  bool claimJobs(ClaimedJob* jobs, int max, int& count) override;
  bool postJobResult(long long jobId, const char* body, size_t len) override;
  bool extendLease(long long jobId, int seconds, uint32_t& leaseUntil) override;
  bool postProgress(long long jobId, const char* body, size_t len) override;

  bool hasConfig() const override { return hasConfig_; }
  const AgentConfig& config() const override { return config_; }
  const Outcome& last() const override { return last_; }
  const Call& lastCall() const override { return lastCall_; }
  const RateLimiter& limiter() const override { return limiter_; }

  // ------------------------------------------------------------------ the board's own

  const char* url() const { return url_; }
  static const char* rootName(Root r);

  // For `status`, e.g. "https://meshmaster.dk: config ok at 12:03:14, agent 17 \"Window\", ...".
  void describe(char* out, size_t cap) const;

  // Calls that got no reply for want of memory: mbedtls said an allocation failed, or the
  // largest free block after the failed call could not hold one TLS content buffer. The observer
  // watches this count move and gives up its highest broker connection for ten minutes when it
  // does (src/observer/Connection.h: the radio first, the uplink second, the observer last).
  uint32_t memoryFailures() const { return memoryFailures_; }

 private:
  // One request. `method` is "GET" or "POST"; `requestBody` is nullptr for a GET. The reply's
  // bytes land in `body` (bodyCap), NUL-terminated and cut if longer.
  int request(Root root, const char* method, const char* path, const char* requestBody, size_t requestLen,
              char* body, size_t bodyCap, size_t& bodyLen, int& tlsError, Call& call);

  // request(), with the one retry against the other root that a verification failure earns.
  bool call(const char* method, const char* endpoint, const char* requestBody, size_t requestLen, char* body,
            size_t bodyCap, size_t& bodyLen, Call& out);

  // A POST to /agent/v1/jobs/{id}/<action> whose reply is short: the result, the extend and the
  // progress. `what` names the call in the log. True when the server answered 200; the reply's
  // bytes are in `body` (bodyCap), for the caller that reads them.
  bool postToJob(long long jobId, const char* action, const char* what, const char* requestBody, size_t len,
                 char* body, size_t bodyCap, size_t& bodyLen);

  SettingsStore* settings_ = nullptr;
  AgentClock* clock_ = nullptr;
  WifiLink* wifi_ = nullptr;
  const char* version_ = "0.0.0-dev";
  char url_[kMaxValueLen + 1] = {0};
  bool tokenSet_ = false;
  Root root_ = Root::IsrgRootX1;
  Outcome last_;
  Call lastCall_;
  RateLimiter limiter_;
  AgentConfig config_;
  bool hasConfig_ = false;
  uint32_t memoryFailures_ = 0;
};

}  // namespace mm
