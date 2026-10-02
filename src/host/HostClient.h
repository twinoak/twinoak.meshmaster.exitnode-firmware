// meshmaster.dk over plain HTTP, for the host build only (docs/host.md).
//
// The board's `AgentClient` is HTTPS with the chain verified against a bundled root, and it refuses
// `http://` outright - a rail of task 01 that `test/test_tls_policy` greps the tree for. This is the
// other implementation of `ServerPort`, and it is **plain HTTP on purpose**: the thing it talks to
// is a compose stack on the workstation's loopback (`http://127.0.0.1:8080`) or a recorded stub in
// CI, neither of which has a certificate and neither of which should.
//
// Three rails of its own, so that nothing here can reach anything real:
//
//   1. `http://` is allowed **only** to a loopback or RFC 1918 address. A public host, a name that
//      is not `localhost`, anything else: refused before a socket is opened, with the reason on the
//      console. `meshmaster.dk` over `http://` cannot be typed into this program.
//   2. `https://` is refused too, and says why: there is no TLS stack in this build, so a harness
//      that *looked* like it was talking to production would be talking to nothing.
//   3. The URL is **not** a setting. It arrives as `--url` and lives in this object; the key table's
//      `meshmaster.url` still means "https://, verified" and the console still refuses anything
//      else, on the board and here. There is one scheme rule in the firmware and this file does not
//      touch it.
//
// The token is read from the settings store for the call that needs it and wiped after, exactly as
// the board does it - the host store is RAM and gone with the process (src/host/arduino/
// Preferences.h), so a harness run leaves no token anywhere.

#pragma once

#include <cstddef>
#include <cstdint>

#include "agent/ServerPort.h"
#include "net/Clock.h"
#include "settings/Store.h"

namespace mm {

class HostClient : public ServerPort {
 public:
  void begin(SettingsStore& settings, AgentClock& clock, const char* agentVersion);

  // `--url`. False (with `why` saying so) when the scheme or the host is not one this build may
  // talk to: the three rails above.
  bool setEndpoint(const char* url, char* why, size_t whyCap);

  void reconfigure() override;
  bool configured() const override;
  bool fetchConfig() override;
  bool postHeartbeat(const Heartbeat& hb, HeartbeatReply& reply) override;
  bool postObservations(const char* body, size_t len, int& accepted, bool& duplicate) override;
  bool claimJobs(ClaimedJob* jobs, int max, int& count) override;
  bool postJobResult(long long jobId, const char* body, size_t len) override;

  bool hasConfig() const override { return hasConfig_; }
  const AgentConfig& config() const override { return config_; }
  const Outcome& last() const override { return last_; }
  const Call& lastCall() const override { return lastCall_; }
  const RateLimiter& limiter() const override { return limiter_; }
  const char* url() const override { return url_; }
  void describe(char* out, size_t cap) const override;
  void oledLine(char* out, size_t cap) const override;

  // How many requests this run made, which tools/host-run.ps1 prints beside the fake's journal.
  uint32_t requests() const { return requests_; }

 private:
  // One request over a fresh TCP connection, as a blocking call: the board opens one TLS session
  // per call too, so the shape of a run is the same.
  bool call(const char* method, const char* path, const char* requestBody, size_t requestLen, char* body,
            size_t bodyCap, size_t& bodyLen, Call& out);

  SettingsStore* settings_ = nullptr;
  AgentClock* clock_ = nullptr;
  const char* version_ = "0.0.0-dev";

  char url_[200] = {0};    // as given, for the log and for `status`
  char host_[128] = {0};   // a private address or 'localhost': never longer than this
  char prefix_[64] = {0};  // the path the URL carried, if any
  int port_ = 80;
  bool endpointOk_ = false;

  Outcome last_;
  Call lastCall_;
  RateLimiter limiter_;
  AgentConfig config_;
  bool hasConfig_ = false;
  uint32_t requests_ = 0;
};

}  // namespace mm
