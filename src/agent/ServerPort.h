// meshmaster.dk, as the agent loop and the job runner see it: the five calls of EXIT-NODES §6 and
// what one answer was, and nothing of TLS.
//
// `AgentLoop` and `JobRunner` used to hold an `AgentClient&`, which is an ESP32 with a verified
// certificate chain and a bundled root. This interface is what they actually used, so there are
// two implementations of it and no third:
//
//   src/agent/AgentClient.h   the board: HTTPS, the chain verified, the token read from NVS for
//                             the one call that needs it (`https://` only - the rail of task 01)
//   src/host/HostClient.h     the workstation: plain HTTP to a loopback or RFC 1918 address and
//                             nowhere else, which is what lets the whole agent loop run against
//                             a compose stack on a desk (docs/host.md)
//
// **The TLS rail is not weakened by this.** The plain scheme is refused by the board's URL rule
// (settings/Keys.cpp) and by the board's client, and the host client refuses every address that is
// not a loopback or RFC 1918 one. `test/test_tls_policy` greps `src/` for the scheme spelt out and
// allows it in `src/host/` alone, so the only place in this firmware that can name a plain-HTTP
// address is the host binding - and the only thing that can reach it is a build with no radio.
//
// Host-portable: no Arduino, no MeshCore, no store. The envelopes it carries are the ones the
// native tests already exercise.

#pragma once

#include <cstddef>
#include <cstdint>

#include "ConfigReply.h"
#include "Envelopes.h"
#include "JobEnvelopes.h"
#include "RateLimit.h"

namespace mm {

class ServerPort {
 public:
  // Which trust anchor a call was made against. `HostPlain` is the host harness and says what it
  // is: no chain at all, because there is no TLS on a loopback socket.
  enum class Root : uint8_t { IsrgRootX1, CoreBundle, HostPlain };
  static const char* rootName(Root r);

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

  // The config call keeps a little more, because `status` and the OLED report it.
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

  virtual ~ServerPort() = default;

  // After `set meshmaster.*`: re-read the URL and forget the last outcome.
  virtual void reconfigure() = 0;
  virtual bool configured() const = 0;

  // GET /agent/v1/config, once. Blocking. The reply is kept and describe() reports it.
  virtual bool fetchConfig() = 0;

  // POST /agent/v1/heartbeat. True when the server answered 200 and the reply parsed.
  virtual bool postHeartbeat(const Heartbeat& hb, HeartbeatReply& reply) = 0;

  // POST /agent/v1/observations. `body` is what writeObservationBatch() made.
  virtual bool postObservations(const char* body, size_t len, int& accepted, bool& duplicate) = 0;

  // POST /agent/v1/jobs/claim `{"max":N}`. True when the server answered 200 and the reply
  // parsed; `count` is how many jobs it handed over, which may be none.
  virtual bool claimJobs(ClaimedJob* jobs, int max, int& count) = 0;

  // POST /agent/v1/jobs/{id}/result. A late result is taken all the same and a duplicate clientId
  // stores nothing twice, so this is safe to retry.
  virtual bool postJobResult(long long jobId, const char* body, size_t len) = 0;

  virtual bool hasConfig() const = 0;
  virtual const AgentConfig& config() const = 0;
  virtual const Outcome& last() const = 0;
  virtual const Call& lastCall() const = 0;
  virtual const RateLimiter& limiter() const = 0;
  virtual const char* url() const = 0;

  // For `status`, e.g. "https://meshmaster.dk: config ok at 12:03:14, agent 17 \"Window\", ...".
  virtual void describe(char* out, size_t cap) const = 0;

  // The OLED's third line while nothing has heartbeated yet: "config ok 12:03:14", "http 401".
  virtual void oledLine(char* out, size_t cap) const = 0;
};

}  // namespace mm
