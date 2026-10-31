// The one connection the uplink keeps to meshmaster.dk (task p1.8-30), as a policy with no socket
// in it.
//
// Before this task every call built a TLS connection and tore it down: a handshake per call, and
// the bench of 28 October (tasks/phase-1.8/README.md) saw the free heap dip about 80 KB at the
// call's peak, which is what left no room for the observer. A connection that is kept pays its
// handshake once and then costs a steady, smaller amount. This class decides when the kept
// connection is used and when a new one is made; AgentClient.cpp owns the WiFiClientSecure and
// the HTTPClient it drives through `Transport`, and test/test_keepalive drives it with a fake.
//
// The rules:
//   - a call reuses the kept connection while the client still sees it alive and it was made
//     against the root the call wants; otherwise it is closed and a handshake is made;
//   - a kept connection that turns out dead when the request goes over it - the write failed,
//     the reply never came, no TLS error - is closed and the call is made again, once, on a new
//     connection, inside the same call; a failure after that is the call's failure, reported
//     exactly as a failure was before;
//   - a call on a fresh connection that fails is not retried here: that is the loop's backoff;
//   - a call that fails leaves nothing kept; a call that succeeds keeps what the client still
//     has, which is nothing if the server said `Connection: close`.
//
// Nothing of what the loop sees changes: the Call record, the backoff, the reboot guard and the
// 401 halt are AgentClient's and the library's as before. What this adds is `counters()` - how
// many handshakes for how many calls - which `status` prints.
//
// Host-portable: no Arduino, no socket, nothing of the board. The native build compiles it
// (platformio.ini, [env:native] build_src_filter), like observer/Connection.cpp before it.

#pragma once

#include <cstdint>

namespace mm {
namespace uplink {

// The root the chain is verified against (src/agent/TlsRoots.h, the core's bundle). The policy
// compares them and never names them; AgentClient::rootName does.
enum class Root : uint8_t { IsrgRootX1, CoreBundle };

// What one exchange over the wire came back with.
struct Attempt {
  int code = 0;      // the HTTP status (> 0), or the client's own error (<= 0): no reply
  int tlsError = 0;  // mbedtls's code when the handshake failed, 0 otherwise
};

// The wire, as the policy sees it: the board's WiFiClientSecure and HTTPClient (AgentClient.cpp),
// a fake on the host. The request itself - method, path, body, where the reply goes - is bound
// by the owner before `call()`, and `exchange()` sends it.
class Transport {
 public:
  virtual ~Transport() = default;
  // Whether a connection is kept from the last exchange and still looks alive to the client. The
  // client's own probe: a connection the server closed shows here, a connection a router dropped
  // in silence does not.
  virtual bool connected() = 0;
  // Close whatever is kept, so that the next exchange starts from nothing. Idempotent.
  virtual void close() = 0;
  // One exchange: a handshake against `root` when nothing is kept, then the request and its reply.
  virtual Attempt exchange(Root root) = 0;
};

struct Counters {
  uint32_t calls = 0;       // exchanges asked for; a reconnect inside a call is still one call
  uint32_t handshakes = 0;  // connections opened, i.e. TLS handshakes attempted, since boot
  uint32_t reused = 0;      // calls that went over a kept connection and were answered on it
  uint32_t reopened = 0;    // kept connections found dead under the request and remade inside the call
};

// Why there is no kept connection right now, for `status`.
enum class Why : uint8_t {
  NotYet,        // nothing called yet
  ServerClosed,  // the client saw it closed before the call (the server's idle timeout, or the link)
  CallFailed,    // the last call failed, and a failed call keeps nothing
  RootChanged,   // the call wanted the other root
  Dropped,       // drop(): the URL or the token changed, or the WiFi link was remade
  NotKept        // the server answered and closed (`Connection: close`)
};

class KeepAlive {
 public:
  // One call, under the rules above. `nowEpoch` is what `keptSince()` remembers.
  Attempt call(Transport& t, Root root, uint32_t nowEpoch);

  // Drop the kept connection on purpose: the URL or the token changed, or the WiFi link was
  // remade under it.
  void drop(Transport& t);

  bool kept() const { return kept_; }
  Root keptRoot() const { return keptRoot_; }
  uint32_t keptSince() const { return keptSince_; }  // the epoch the kept connection was made at
  Why why() const { return why_; }                   // meaningful while !kept()
  const Counters& counters() const { return counters_; }

  // What the last call did, for the log: a handshake, a reuse, a reopen.
  bool lastHandshook() const { return lastHandshook_; }
  bool lastReopened() const { return lastReopened_; }

  static const char* whyText(Why w);

 private:
  bool kept_ = false;
  Root keptRoot_ = Root::IsrgRootX1;
  uint32_t keptSince_ = 0;
  Why why_ = Why::NotYet;
  Counters counters_;
  bool lastHandshook_ = false;
  bool lastReopened_ = false;
};

}  // namespace uplink
}  // namespace mm
