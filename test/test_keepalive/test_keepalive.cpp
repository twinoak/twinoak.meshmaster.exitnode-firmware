// The uplink's kept connection (tasks/phase-1.8/30), driven on a host with a fake wire: reuse
// while alive, one handshake per connection and not per call, a kept connection found dead and
// remade once inside the call, a failure after that reported as a failure was before, a fresh
// connection's failure not retried, the root change forcing a new handshake, the server's own
// close, a drop on purpose, and an HTTP error that keeps the connection all the same.
//
// Nothing here opens a socket: `FakeWire` answers what the test scripts, and counts what the
// policy asked of it.

#include <gtest/gtest.h>

#include <deque>
#include <vector>

#include "agent/KeepAlive.h"

using namespace mm::uplink;

namespace {

Attempt answer(int code, int tlsError = 0) {
  Attempt a;
  a.code = code;
  a.tlsError = tlsError;
  return a;
}

// HTTPClient's own codes, as AgentClient.cpp sees them: no reply, all of them.
constexpr int kConnectionRefused = -1;
constexpr int kSendHeaderFailed = -2;
constexpr int kConnectionLost = -5;
constexpr int kReadTimeout = -11;
constexpr int kVerifyFailed = -0x2700;  // MBEDTLS_ERR_X509_CERT_VERIFY_FAILED

class FakeWire : public Transport {
 public:
  bool alive = false;            // what connected() answers; what a successful exchange leaves
  bool closeAfterReply = false;  // the server says `Connection: close`
  std::deque<Attempt> script;    // the answers, in order; empty is 200
  int opens = 0;
  int closes = 0;
  int exchanges = 0;
  std::vector<Root> roots;       // the root of every exchange
  std::vector<bool> opened;      // whether that exchange made a connection

  bool connected() override { return alive; }
  void close() override {
    if (alive) closes++;
    alive = false;
  }
  Attempt exchange(Root root) override {
    exchanges++;
    const bool fresh = !alive;
    if (fresh) {
      opens++;
      alive = true;
    }
    opened.push_back(fresh);
    roots.push_back(root);
    Attempt a = answer(200);
    if (!script.empty()) {
      a = script.front();
      script.pop_front();
    }
    // The client stops on any error (HTTPClient::returnError), and on the server's close.
    if (a.code <= 0 || closeAfterReply) alive = false;
    return a;
  }
};

constexpr uint32_t kEpoch = 1793016000;  // 2026-10-26T12:00:00Z

}  // namespace

TEST(KeepAlive, CallsReuseOneConnectionAndTheHandshakeCountIsOnePerConnection) {
  FakeWire wire;
  KeepAlive line;
  for (int i = 0; i < 5; i++) {
    const Attempt a = line.call(wire, Root::IsrgRootX1, kEpoch + i);
    EXPECT_EQ(200, a.code);
  }
  EXPECT_EQ(1, wire.opens);
  EXPECT_EQ(5, wire.exchanges);
  EXPECT_EQ(5u, line.counters().calls);
  EXPECT_EQ(1u, line.counters().handshakes);
  EXPECT_EQ(4u, line.counters().reused);
  EXPECT_EQ(0u, line.counters().reopened);
  EXPECT_TRUE(line.kept());
  EXPECT_EQ(kEpoch, line.keptSince()) << "the kept connection dates from its handshake, not the last call";
  EXPECT_TRUE(line.lastHandshook() == false);
}

TEST(KeepAlive, AConnectionTheServerClosedIsReplacedWithoutAFailure) {
  FakeWire wire;
  KeepAlive line;
  EXPECT_EQ(200, line.call(wire, Root::IsrgRootX1, kEpoch).code);
  wire.alive = false;  // the idle timeout: the client's probe sees the close before the call
  const Attempt a = line.call(wire, Root::IsrgRootX1, kEpoch + 130);
  EXPECT_EQ(200, a.code);
  EXPECT_EQ(2, wire.opens);
  EXPECT_EQ(2, wire.exchanges) << "found closed before the request: one exchange, not a retry";
  EXPECT_EQ(2u, line.counters().handshakes);
  EXPECT_EQ(0u, line.counters().reopened);
  EXPECT_TRUE(line.lastHandshook());
  EXPECT_TRUE(line.kept());
  EXPECT_EQ(kEpoch + 130, line.keptSince());
}

TEST(KeepAlive, AKeptConnectionDeadUnderTheRequestIsRemadeOnceInsideTheCall) {
  FakeWire wire;
  KeepAlive line;
  EXPECT_EQ(200, line.call(wire, Root::IsrgRootX1, kEpoch).code);
  // The probe says alive, the write fails: a router dropped the session in silence.
  wire.script.push_back(answer(kSendHeaderFailed));
  const Attempt a = line.call(wire, Root::IsrgRootX1, kEpoch + 60);
  EXPECT_EQ(200, a.code) << "the call is answered on the new connection";
  EXPECT_EQ(3, wire.exchanges);
  EXPECT_EQ(2, wire.opens);
  EXPECT_EQ(2u, line.counters().calls) << "a reopen inside a call is still one call";
  EXPECT_EQ(2u, line.counters().handshakes);
  EXPECT_EQ(1u, line.counters().reopened);
  EXPECT_EQ(0u, line.counters().reused);
  EXPECT_TRUE(line.lastReopened());
  EXPECT_TRUE(line.kept());
  EXPECT_EQ(kEpoch + 60, line.keptSince());
}

TEST(KeepAlive, ARequestThatTimedOutOnAKeptConnectionIsRemadeToo) {
  FakeWire wire;
  KeepAlive line;
  EXPECT_EQ(200, line.call(wire, Root::IsrgRootX1, kEpoch).code);
  wire.script.push_back(answer(kReadTimeout));
  EXPECT_EQ(200, line.call(wire, Root::IsrgRootX1, kEpoch + 60).code);
  EXPECT_EQ(1u, line.counters().reopened);
  EXPECT_EQ(2u, line.counters().handshakes);
}

TEST(KeepAlive, AFailureAfterTheReopenIsTheCallsFailureAsBefore) {
  FakeWire wire;
  KeepAlive line;
  EXPECT_EQ(200, line.call(wire, Root::IsrgRootX1, kEpoch).code);
  wire.script.push_back(answer(kConnectionLost));
  wire.script.push_back(answer(kConnectionRefused));
  const Attempt a = line.call(wire, Root::IsrgRootX1, kEpoch + 60);
  EXPECT_EQ(kConnectionRefused, a.code) << "what the new connection came back with, not the dead one's code";
  EXPECT_EQ(3, wire.exchanges) << "once, and not again";
  EXPECT_FALSE(line.kept());
  EXPECT_EQ(Why::CallFailed, line.why());
  EXPECT_EQ(1u, line.counters().reopened);
  EXPECT_EQ(2u, line.counters().handshakes);
  // The next call starts from nothing and, failing on a fresh connection, is not retried here.
  wire.script.push_back(answer(kConnectionRefused));
  EXPECT_EQ(kConnectionRefused, line.call(wire, Root::IsrgRootX1, kEpoch + 120).code);
  EXPECT_EQ(4, wire.exchanges);
  EXPECT_EQ(3u, line.counters().handshakes);
  EXPECT_EQ(1u, line.counters().reopened);
}

TEST(KeepAlive, AFreshConnectionThatFailsIsNotRetriedHere) {
  FakeWire wire;
  KeepAlive line;
  wire.script.push_back(answer(kConnectionRefused));
  const Attempt a = line.call(wire, Root::IsrgRootX1, kEpoch);
  EXPECT_EQ(kConnectionRefused, a.code);
  EXPECT_EQ(1, wire.exchanges) << "the loop's backoff owns the retry";
  EXPECT_EQ(1u, line.counters().handshakes);
  EXPECT_EQ(0u, line.counters().reopened);
  EXPECT_FALSE(line.kept());
  EXPECT_EQ(Why::CallFailed, line.why());
}

TEST(KeepAlive, AVerificationFailureIsNeverRetriedHere) {
  // The other root is AgentClient::call's business, and it is tried on a fresh connection.
  FakeWire wire;
  KeepAlive line;
  wire.script.push_back(answer(kConnectionRefused, kVerifyFailed));
  const Attempt a = line.call(wire, Root::IsrgRootX1, kEpoch);
  EXPECT_EQ(kVerifyFailed, a.tlsError);
  EXPECT_EQ(1, wire.exchanges);
  EXPECT_FALSE(line.kept());
}

TEST(KeepAlive, TheRootChangeForcesANewHandshake) {
  FakeWire wire;
  KeepAlive line;
  EXPECT_EQ(200, line.call(wire, Root::IsrgRootX1, kEpoch).code);
  EXPECT_EQ(200, line.call(wire, Root::IsrgRootX1, kEpoch + 30).code);
  EXPECT_EQ(1, wire.opens);
  const Attempt a = line.call(wire, Root::CoreBundle, kEpoch + 60);
  EXPECT_EQ(200, a.code);
  EXPECT_EQ(2, wire.opens) << "a kept connection verified against one root is not reused for the other";
  EXPECT_EQ(1, wire.closes);
  EXPECT_EQ(Root::CoreBundle, wire.roots.back());
  EXPECT_TRUE(wire.opened.back());
  EXPECT_TRUE(line.kept());
  EXPECT_EQ(Root::CoreBundle, line.keptRoot());
  EXPECT_EQ(3u, line.counters().calls);
  EXPECT_EQ(2u, line.counters().handshakes);
  EXPECT_EQ(1u, line.counters().reused);
  // And back on the first root, the same again.
  EXPECT_EQ(200, line.call(wire, Root::IsrgRootX1, kEpoch + 90).code);
  EXPECT_EQ(3u, line.counters().handshakes);
  EXPECT_EQ(Root::IsrgRootX1, line.keptRoot());
}

TEST(KeepAlive, AServerThatSaysCloseIsBelievedAndTheNextCallHandshakes) {
  FakeWire wire;
  KeepAlive line;
  wire.closeAfterReply = true;
  EXPECT_EQ(200, line.call(wire, Root::IsrgRootX1, kEpoch).code);
  EXPECT_FALSE(line.kept());
  EXPECT_EQ(Why::NotKept, line.why());
  EXPECT_EQ(200, line.call(wire, Root::IsrgRootX1, kEpoch + 30).code);
  EXPECT_EQ(2u, line.counters().handshakes) << "one per connection, which here is one per call";
  EXPECT_EQ(0u, line.counters().reused);
  EXPECT_EQ(0u, line.counters().reopened);
}

TEST(KeepAlive, ADropOnPurposeClosesAndTheNextCallHandshakes) {
  FakeWire wire;
  KeepAlive line;
  EXPECT_EQ(200, line.call(wire, Root::IsrgRootX1, kEpoch).code);
  line.drop(wire);
  EXPECT_FALSE(line.kept());
  EXPECT_EQ(Why::Dropped, line.why());
  EXPECT_EQ(1, wire.closes);
  EXPECT_FALSE(wire.alive);
  EXPECT_EQ(200, line.call(wire, Root::IsrgRootX1, kEpoch + 5).code);
  EXPECT_EQ(2u, line.counters().handshakes);
  EXPECT_TRUE(line.kept());
  // A drop with nothing kept is nothing.
  KeepAlive fresh;
  fresh.drop(wire);
  EXPECT_EQ(Why::NotYet, fresh.why());
}

TEST(KeepAlive, AnHttpErrorIsAReplyAndKeepsTheConnection) {
  // A 401 halts the loop and a 503 backs it off - the library's business; the connection the
  // answer came over is as good as any.
  FakeWire wire;
  KeepAlive line;
  wire.script.push_back(answer(401));
  EXPECT_EQ(401, line.call(wire, Root::IsrgRootX1, kEpoch).code);
  EXPECT_TRUE(line.kept());
  wire.script.push_back(answer(503));
  EXPECT_EQ(503, line.call(wire, Root::IsrgRootX1, kEpoch + 30).code);
  EXPECT_TRUE(line.kept());
  EXPECT_EQ(1u, line.counters().handshakes);
  EXPECT_EQ(1u, line.counters().reused);
}

TEST(KeepAlive, WhyHasAWordForEveryState) {
  EXPECT_STREQ("the first call", KeepAlive::whyText(Why::NotYet));
  EXPECT_STREQ("the server closed it", KeepAlive::whyText(Why::ServerClosed));
  EXPECT_STREQ("the last call failed", KeepAlive::whyText(Why::CallFailed));
  EXPECT_STREQ("the root changed", KeepAlive::whyText(Why::RootChanged));
  EXPECT_STREQ("dropped on purpose", KeepAlive::whyText(Why::Dropped));
  EXPECT_STREQ("the server answered and closed", KeepAlive::whyText(Why::NotKept));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
