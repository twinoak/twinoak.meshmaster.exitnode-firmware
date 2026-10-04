// The HTTPS client to meshmaster.dk: the three calls this firmware makes, with the certificate
// chain verified.
//
//   GET  /agent/v1/config           once per WiFi connection, for the budget and the clock
//   POST /agent/v1/heartbeat        every pollIntervalS, the self-description of EXIT-NODES §5
//   POST /agent/v1/observations     a batch of adverts heard, client id and all
//   POST /agent/v1/jobs/claim       when the heartbeat says jobs are waiting (task 03)
//   POST /agent/v1/jobs/{id}/result one per job, once, with its client id
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

#pragma once

#include <cstddef>
#include <cstdint>

#include "ConfigReply.h"
#include "Envelopes.h"
#include "JobEnvelopes.h"
#include "RateLimit.h"
#include "net/Clock.h"
#include "settings/Store.h"

namespace mm {

class AgentClient {
 public:
  enum class Root : uint8_t { IsrgRootX1, CoreBundle };

  // What one request came back with, whatever it asked for.
  struct Call {
    bool reachable = false;   // a reply arrived, whatever its status
    bool ok = false;          // 2xx and the body parsed
    int httpStatus = 0;
    uint32_t retryAfterS = 0;  // the 429's Retry-After, in seconds
    int rateLimitRemaining = -1;
    char error[128] = {0};
    uint32_t atEpoch = 0;
    uint32_t atMillis = 0;
    Root root = Root::IsrgRootX1;
  };

  // The config call keeps a little more, because `status` reports it.
  struct Outcome {
    bool attempted = false;
    bool ok = false;
    int httpStatus = 0;
    char error[128] = {0};
    uint32_t atMillis = 0;
    uint32_t atEpoch = 0;
    Root root = Root::IsrgRootX1;
    uint32_t heapAfter = 0;  // free heap right after the call, the number later tasks budget from
  };

  void begin(SettingsStore& settings, AgentClock& clock, const char* agentVersion);

  // After `set meshmaster.*`: re-read the URL and forget the last outcome.
  void reconfigure();

  bool configured() const { return url_[0] != '\0' && tokenSet_; }

  // GET /agent/v1/config, once. Blocking. The reply is kept and describe() reports it.
  bool fetchConfig();

  // POST /agent/v1/heartbeat. True when the server answered 200 and the reply parsed.
  bool postHeartbeat(const Heartbeat& hb, HeartbeatReply& reply);

  // POST /agent/v1/observations. `body` is what writeObservationBatch() made.
  bool postObservations(const char* body, size_t len, int& accepted, bool& duplicate);

  // POST /agent/v1/jobs/claim `{"max":N}`. True when the server answered 200 and the reply
  // parsed; `count` is how many jobs it handed over, which may be none.
  bool claimJobs(ClaimedJob* jobs, int max, int& count);

  // POST /agent/v1/jobs/{id}/result. `body` is what writeJobResult() made. A late result is
  // taken all the same and a duplicate clientId stores nothing twice, so this is safe to retry.
  bool postJobResult(long long jobId, const char* body, size_t len);

  bool hasConfig() const { return hasConfig_; }
  const AgentConfig& config() const { return config_; }
  const Outcome& last() const { return last_; }
  const Call& lastCall() const { return lastCall_; }
  const RateLimiter& limiter() const { return limiter_; }
  const char* url() const { return url_; }
  static const char* rootName(Root r);

  // For `status`, e.g. "https://meshmaster.dk: config ok at 12:03:14, agent 17 \"Window\", ...".
  void describe(char* out, size_t cap) const;

 private:
  // One request. `method` is "GET" or "POST"; `requestBody` is nullptr for a GET. The reply's
  // bytes land in `body` (bodyCap), NUL-terminated and cut if longer.
  int request(Root root, const char* method, const char* path, const char* requestBody, size_t requestLen,
              char* body, size_t bodyCap, size_t& bodyLen, int& tlsError, Call& call);

  // request(), with the one retry against the other root that a verification failure earns.
  bool call(const char* method, const char* endpoint, const char* requestBody, size_t requestLen, char* body,
            size_t bodyCap, size_t& bodyLen, Call& out);

  SettingsStore* settings_ = nullptr;
  AgentClock* clock_ = nullptr;
  const char* version_ = "0.0.0-dev";
  char url_[kMaxValueLen + 1] = {0};
  bool tokenSet_ = false;
  Root root_ = Root::IsrgRootX1;
  Outcome last_;
  Call lastCall_;
  RateLimiter limiter_;
  AgentConfig config_;
  bool hasConfig_ = false;
};

}  // namespace mm
