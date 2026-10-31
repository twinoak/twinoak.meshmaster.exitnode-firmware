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
// One connection, kept (task p1.8-30). The WiFiClientSecure and the HTTPClient over it live
// across calls: the handshake happens at the first call and again only when the connection is
// gone - the server closed it (its idle timeout is about two minutes, measured), a call failed,
// the WiFi link was remade, the root to verify against changed. The rules are KeepAlive.h's and
// are tested on the host; what every call costs the heap is measured here and printed by
// `status` (describeUplink), because a board whose memory is the constraint wants the number.
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

#include "KeepAlive.h"
#include "net/Clock.h"
#include "net/WifiLink.h"
#include "settings/Store.h"

class WiFiClientSecure;
class HTTPClient;

namespace mm {

class AgentClient : public IUplink, private uplink::Transport {
 public:
  using Root = uplink::Root;

  // What the last call cost the heap: the free heap before it, the lowest free heap seen during
  // it, and the free heap after. The lowest is exact for mbedtls's own allocations - every one
  // of them goes through a wrapper of its allocator while a call is in flight (AgentClient.cpp)
  // - and sampled at four points for everything else; and if the board's minimum-ever moved
  // during the call, the lowest is that, which is exact for everything.
  struct HeapCost {
    bool any = false;
    uint32_t freeBefore = 0;
    uint32_t lowest = 0;
    uint32_t freeAfter = 0;
    uint32_t atEpoch = 0;
  };

  void begin(SettingsStore& settings, AgentClock& clock, WifiLink& wifi, const char* agentVersion);

  // After `set meshmaster.*`: re-read the URL, forget the last outcome, drop the kept connection.
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

  // For `status`, the line after the heap's: the kept connection and since when (or why not),
  // handshakes against calls, and what the last call cost (task p1.8-30).
  void describeUplink(char* out, size_t cap) const;
  const uplink::KeepAlive& line() const { return line_; }
  const HeapCost& lastCost() const { return cost_; }

  // Calls that got no reply for want of memory: mbedtls said an allocation failed, or the
  // largest free block after the failed call could not hold one TLS content buffer. The observer
  // watches this count move and gives up its highest broker connection for ten minutes when it
  // does (src/observer/Connection.h: the radio first, the uplink second, the observer last).
  uint32_t memoryFailures() const { return memoryFailures_; }

 private:
  // ---- uplink::Transport: the wire under KeepAlive, which is a WiFiClientSecure and an
  // HTTPClient that outlive the call. The request exchange() sends is `pending_`, bound by
  // request() just before.
  bool connected() override;
  void close() override;
  uplink::Attempt exchange(Root root) override;

  struct Pending {
    const char* method = "GET";
    const char* path = "";
    const char* requestBody = nullptr;
    size_t requestLen = 0;
    char* body = nullptr;
    size_t bodyCap = 0;
    size_t bodyLen = 0;
    uint32_t retryAfterS = 0;
    int rateLimitRemaining = -1;
    char error[128] = {0};
  };

  // One request, under KeepAlive's rules. `method` is "GET" or "POST"; `requestBody` is nullptr
  // for a GET. The reply's bytes land in `body` (bodyCap), NUL-terminated and cut if longer.
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

  // The kept connection: the client is made at a handshake and deleted when the connection is
  // gone, so that every handshake starts from a client with no history (its last error, the root
  // it was given); the HTTPClient is made once in begin() and drives whichever client is current.
  WiFiClientSecure* client_ = nullptr;
  HTTPClient* http_ = nullptr;
  uplink::KeepAlive line_;
  Pending pending_;
  HeapCost cost_;
  uint32_t linkConnectsSeen_ = 0;  // WifiLink::connects() at the last call: a change means the link was remade
};

}  // namespace mm
