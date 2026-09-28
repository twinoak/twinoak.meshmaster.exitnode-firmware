// The HTTPS client to meshmaster.dk: one authenticated call, GET /agent/v1/config, with the
// certificate chain verified.
//
// How the server's certificate is checked, for every task after this one:
//   1. The chain is verified - the client is never told to accept an unverified one, and
//      test/test_tls_policy greps the tree for the ways of telling it so.
//   2. The root is ISRG Root X1 (src/agent/TlsRoots.h), which meshmaster.dk's Let's Encrypt
//      chain ends at, bundled in the image.
//   3. If a handshake fails on verification (the chain no longer ends there), the next attempt
//      uses the ESP32 core's own certificate bundle - the Mozilla roots the core shipped with -
//      and the choice is remembered and printed. Both are verification; neither is a bypass.
// Every request carries `Authorization: Bearer <token>` and `X-Agent-Version`, and a User-Agent
// of "meshmaster-agent/<version>" like the .NET agent's. The token is read from NVS for the call
// and wiped after; the call is blocking and takes a few seconds on a cold handshake.

#pragma once

#include <cstddef>
#include <cstdint>

#include "ConfigReply.h"
#include "net/Clock.h"
#include "settings/Store.h"

namespace mm {

class AgentClient {
 public:
  enum class Root : uint8_t { IsrgRootX1, CoreBundle };

  struct Outcome {
    bool attempted = false;
    bool ok = false;
    int httpStatus = 0;      // when a reply came
    char error[128] = {0};   // when none did, or the reply did not parse
    uint32_t atMillis = 0;
    uint32_t atEpoch = 0;    // 0 when there was no time
    Root root = Root::IsrgRootX1;
    uint32_t heapAfter = 0;  // free heap right after the call, the number later tasks budget from
  };

  void begin(SettingsStore& settings, AgentClock& clock, const char* agentVersion);

  // After `set meshmaster.*`: re-read the URL and forget the last outcome.
  void reconfigure();

  bool configured() const { return url_[0] != '\0' && tokenSet_; }

  // GET /agent/v1/config, once. Blocking. The reply is kept and describe() reports it.
  bool fetchConfig();

  bool hasConfig() const { return hasConfig_; }
  const AgentConfig& config() const { return config_; }
  const Outcome& last() const { return last_; }
  const char* url() const { return url_; }
  static const char* rootName(Root r);

  // For `status`, e.g. "https://meshmaster.dk: config ok at 12:03:14, agent 17 \"Window\", ...".
  void describe(char* out, size_t cap) const;

  // The OLED's third line: "config ok 12:03:14", "http 401", "tls failed", "no server".
  void oledLine(char* out, size_t cap) const;

 private:
  int request(Root root, char* body, size_t bodyCap, size_t& bodyLen, int& tlsError, char* err, size_t errCap);

  SettingsStore* settings_ = nullptr;
  AgentClock* clock_ = nullptr;
  const char* version_ = "0.0.0-dev";
  char url_[kMaxValueLen + 1] = {0};
  bool tokenSet_ = false;
  Root root_ = Root::IsrgRootX1;
  Outcome last_;
  AgentConfig config_;
  bool hasConfig_ = false;
};

}  // namespace mm
