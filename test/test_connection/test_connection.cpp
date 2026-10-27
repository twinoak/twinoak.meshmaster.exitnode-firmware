// The broker connection's state machine (tasks/phase-1.7/27), driven on a host with a fake
// client and a clock the test moves: connect and publish, a refused sign-in and its backoff, the
// clock missing, the heap short, the uplink's memory failure taking the highest slot down, a lost
// connection and the reconnect, the token's renewal before it expires, the queue's bound, the
// chain that ends elsewhere, WiFi going away, a setting changed under a live slot - and a read of
// the log ring after all of it, which must hold counts and reasons and never a token, a password
// or a payload.
//
// Nothing here opens a socket: the client is `FakeClient` below, the heap two numbers, the clock
// two counters. The signer is MeshCore's ed25519 with a fixed seed (as test_observer's), so the
// tokens handed to the fake are real tokens and the renewal test can see them change.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include <ed_25519.h>

#include <agent/Log.h>
#include <agent/LogRing.h>

#include "observer/Config.h"
#include "observer/Connection.h"
#include "observer/Dialect.h"
#include "observer/Token.h"

using namespace mm::observer;

namespace {

// ---- the board, as two counters and two numbers ---------------------------------------------

uint32_t g_millis = 1000;
uint32_t g_epoch = 1793016000;  // 2026-10-26T12:00:00Z
uint32_t g_free = 200 * 1024;
uint32_t g_largest = 100 * 1024;

uint32_t fakeMillis() { return g_millis; }
uint32_t fakeEpoch() { return g_epoch; }
uint32_t fakeFree() { return g_free; }
uint32_t fakeLargest() { return g_largest; }
uint32_t fakeRandom() { return 0; }  // withJitter(ms, 0) is ms less a quarter: the earliest the step allows

void advance(uint32_t ms) {
  g_millis += ms;
  if (g_epoch != 0) g_epoch += ms / 1000;
}

// 5 s with random 0 is 3 750 ms; the view's waitMs says the same.
uint32_t jittered(uint32_t ms) { return ms - ms * 25 / 100; }

// ---- the fake client ----------------------------------------------------------------------------

struct Opened {
  std::string uri, username, password, willTopic, willPayload;
  bool willRetain = false;
  bool bundle = false;  // caPem was nullptr: the core's bundle
  uint16_t keepalive = 0;
};

struct Published {
  std::string topic, payload;
  uint8_t qos = 0;
  bool retain = false;
};

struct Event {
  ClientEvent event;
  ErrorKind kind;
  std::string reason;
};

class FakeClient : public Client {
 public:
  std::vector<Opened> opens;
  std::vector<Published> published;
  std::deque<Event> events;
  int closes = 0;
  bool isOpen = false;
  bool acceptPublish = true;
  size_t outbox = 0;
  bool failOpen = false;
  std::string failReason;

  bool open(const ClientConfig& cfg, char* reason, size_t cap) override {
    if (failOpen) {
      std::snprintf(reason, cap, "%s", failReason.c_str());
      return false;
    }
    Opened o;
    o.uri = cfg.uri;
    o.username = cfg.username != nullptr ? cfg.username : "";
    o.password = cfg.password != nullptr ? cfg.password : "";
    o.willTopic = cfg.willTopic != nullptr ? cfg.willTopic : "";
    o.willPayload = cfg.willPayload != nullptr ? std::string(cfg.willPayload, cfg.willLen) : "";
    o.willRetain = cfg.willRetain;
    o.bundle = cfg.caPem == nullptr;
    o.keepalive = cfg.keepaliveS;
    opens.push_back(o);
    isOpen = true;
    return true;
  }
  void close() override {
    if (isOpen) closes++;
    isOpen = false;
    events.clear();
  }
  bool publish(const char* topic, const char* payload, size_t len, uint8_t qos, bool retain) override {
    if (!isOpen || !acceptPublish) return false;
    published.push_back({topic, std::string(payload, len), qos, retain});
    return true;
  }
  ClientEvent takeEvent(ErrorKind& kind, char* reason, size_t cap) override {
    if (events.empty()) {
      kind = ErrorKind::Other;
      reason[0] = '\0';
      return ClientEvent::None;
    }
    const Event e = events.front();
    events.pop_front();
    kind = e.kind;
    std::snprintf(reason, cap, "%s", e.reason.c_str());
    return e.event;
  }
  size_t outboxBytes() override { return outbox; }

  // What the broker does, as the test says it:
  void accept() { events.push_back({ClientEvent::Connected, ErrorKind::Other, ""}); }
  void refuse(const char* why) { events.push_back({ClientEvent::Refused, ErrorKind::Other, why}); }
  void fail(ErrorKind kind, const char* why) { events.push_back({ClientEvent::Error, kind, why}); }
  void drop() { events.push_back({ClientEvent::Disconnected, ErrorKind::Other, ""}); }
  const Opened& lastOpen() const { return opens.back(); }
  int statusPublished() const {
    int n = 0;
    for (const Published& p : published) {
      if (p.topic.find("/status") != std::string::npos) n++;
    }
    return n;
  }
  int packetsPublished() const {
    int n = 0;
    for (const Published& p : published) {
      if (p.topic.find("/packets") != std::string::npos) n++;
    }
    return n;
  }
};

// ---- the signer, the status, the log -----------------------------------------------------------

struct FixedIdentity : public Signer {
  uint8_t pub[32];
  uint8_t prv[64];
  FixedIdentity() {
    uint8_t seed[32];
    for (int i = 0; i < 32; i++) seed[i] = static_cast<uint8_t>(0x5A ^ (i * 7));
    ed25519_create_keypair(pub, prv, seed);
  }
  bool sign(const uint8_t* msg, size_t len, uint8_t sig[kSignatureSize]) override {
    ed25519_sign(sig, msg, len, pub, prv);
    return true;
  }
  std::string pubHex() const {
    char hex[65];
    hexUpper(pub, 32, hex, sizeof hex);
    return hex;
  }
};

// The status message a board would make: Dialect.h's formatStatus over fixed fields.
struct FixedStatus : public StatusSource {
  std::string originId;
  size_t formatStatus(const char* status, char* out, size_t cap) override {
    StatusFields f;
    f.status = status;
    f.origin = "MeshMaster-TEST";
    f.originId = originId.c_str();
    f.model = "Heltec V3";
    f.firmwareVersion = "meshmaster-agent 0.0.0-dev on MeshCore companion-v1.17.1";
    f.radio = "869.617981,62.5,8,8";
    f.clientVersion = "meshcore/meshmaster-agent 0.0.0-dev on MeshCore companion-v1.17.1";
    f.atEpoch = g_epoch;
    f.uptimeSecs = static_cast<int>(g_millis / 1000);
    return mm::observer::formatStatus(f, out, cap);
  }
};

// Every line the connections report, through mm::logf as the board does (Observer::reportLine),
// into the ring `dump` prints and into a vector the assertions read.
std::vector<std::string> g_log;
void logSink(const char* line, size_t len) {
  g_log.emplace_back(line, len);
  mm::logRing().add(line, len);
}
void reportLine(void*, mm::LogLevel level, const char* line) { mm::logf(level, "%s", line); }

std::string logText() {
  std::string all;
  for (const std::string& l : g_log) all += l;
  return all;
}

// ---- the rig ---------------------------------------------------------------------------------------

const char* const kIata = "AAR";
const char* const kOwner = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
const char* const kEmail = "owner@example.dk";

struct Rig {
  FakeClient client[kSlots];
  FixedIdentity identity;
  FixedStatus status;
  Connections link;
  ObserverSettings settings;
  std::string pubHex;

  Rig() {
    g_millis = 1000;
    g_epoch = 1793016000;
    g_free = 200 * 1024;
    g_largest = 100 * 1024;
    g_log.clear();
    mm::logRing().clear();
    mm::logBegin(&fakeEpoch, &fakeMillis);
    mm::logSetLevel(mm::LogLevel::Debug);
    mm::logSetSink(&logSink);

    pubHex = identity.pubHex();
    status.originId = pubHex;
    Client* const clients[kSlots] = {&client[0], &client[1]};
    Environment env;
    env.millis = &fakeMillis;
    env.epoch = &fakeEpoch;
    env.freeHeap = &fakeFree;
    env.largestBlock = &fakeLargest;
    env.random = &fakeRandom;
    env.caPem = "-----BEGIN CERTIFICATE-----\nTEST\n-----END CERTIFICATE-----\n";
    link.begin(clients, env, identity, status, &reportLine, nullptr);

    settings.iata = kIata;
    settings.owner = kOwner;
    settings.email = kEmail;
    settings.origin = "MeshMaster-TEST";
    settings.slot[0].preset = "meshview";
    settings.slot[1].preset = "none";
  }

  void configure() { link.configure(settings, observerEnabled(settings, nullptr, 0), pubHex.c_str(), "meshcore/test"); }

  // Loop once, then move the clock.
  void tick(uint32_t ms = 0) {
    link.loop();
    if (ms > 0) advance(ms);
  }

  SlotState state(int i) const { return link.view(i).state; }

  // The usual start: slot 1 configured, WiFi up, one pass - the slot is connecting.
  void startSlot1() {
    configure();
    link.setLinkUp(true);
    tick();
    ASSERT_EQ(SlotState::Connecting, state(0));
  }
  // ...and the broker accepts: connected, status published.
  void connectSlot1() {
    startSlot1();
    client[0].accept();
    tick();
    ASSERT_EQ(SlotState::Connected, state(0));
  }

  std::string packetMessage(int n = 0) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "{\"type\":\"PACKET\",\"n\":\"%d\",\"raw\":\"DEADBEEF%04X\"}", n, n);
    return buf;
  }
};

}  // namespace

// ---- connect and publish -----------------------------------------------------------------------

TEST(Connect, ASlotWaitsForWifiThenOpensWithTheTokenTheRootsAndTheWill) {
  Rig r;
  r.configure();
  EXPECT_EQ(SlotState::WaitingForLink, r.state(0));
  EXPECT_EQ(SlotState::Off, r.state(1));
  r.tick();
  EXPECT_EQ(SlotState::WaitingForLink, r.state(0)) << "no wifi, nothing tried";
  EXPECT_EQ(0u, r.client[0].opens.size());

  r.link.setLinkUp(true);
  r.tick();
  ASSERT_EQ(SlotState::Connecting, r.state(0));
  ASSERT_EQ(1u, r.client[0].opens.size());
  const Opened& o = r.client[0].lastOpen();
  EXPECT_EQ("wss://mqtt.meshview.dk:443/mqtt", o.uri);
  EXPECT_EQ("v1_" + r.pubHex, o.username);
  EXPECT_FALSE(o.bundle) << "the carried roots first";
  EXPECT_EQ(kKeepaliveS, o.keepalive);
  // The password is the token: three base64url/hex parts, the claims naming the audience.
  EXPECT_EQ(2, static_cast<int>(std::count(o.password.begin(), o.password.end(), '.')));
  EXPECT_EQ(std::string::npos, o.password.find(' '));
  // The will: a retained-where-allowed "offline" status on the status topic. meshview takes no retain.
  EXPECT_EQ("meshcore/AAR/" + r.pubHex + "/status", o.willTopic);
  EXPECT_EQ(0u, o.willPayload.find("{\"status\":\"offline\""));
  EXPECT_FALSE(o.willRetain);

  r.client[0].accept();
  r.tick();
  EXPECT_EQ(SlotState::Connected, r.state(0));
  EXPECT_EQ(1u, r.link.view(0).stats.connects);
  ASSERT_EQ(1, r.client[0].statusPublished()) << "a status at connect";
  EXPECT_EQ(1, r.client[0].published[0].qos);
  EXPECT_FALSE(r.client[0].published[0].retain) << "meshview is a custom slot's settings: no retain";
  EXPECT_EQ(0u, r.client[0].published[0].payload.find("{\"status\":\"online\""));
  EXPECT_EQ(1, r.link.connectedSlots());
  EXPECT_EQ(1, r.link.configuredSlots());
}

TEST(Connect, MeshmapperRetainsItsStatusAndItsWill) {
  Rig r;
  r.settings.slot[0].preset = "meshmapper";
  r.connectSlot1();
  EXPECT_TRUE(r.client[0].lastOpen().willRetain);
  EXPECT_EQ("wss://mqtt.meshmapper.net:443/mqtt", r.client[0].lastOpen().uri);
  ASSERT_EQ(1, r.client[0].statusPublished());
  EXPECT_TRUE(r.client[0].published[0].retain);
}

TEST(Publish, EveryPacketMessageGoesToThePacketsTopicQosZeroAndIsCounted) {
  Rig r;
  r.connectSlot1();
  const std::string m = r.packetMessage(1);
  r.link.onPacketMessage(m.c_str(), m.size());
  r.tick();
  ASSERT_EQ(1, r.client[0].packetsPublished());
  const Published& p = r.client[0].published.back();
  EXPECT_EQ("meshcore/AAR/" + r.pubHex + "/packets", p.topic);
  EXPECT_EQ(m, p.payload);
  EXPECT_EQ(0, p.qos);
  EXPECT_FALSE(p.retain);
  EXPECT_EQ(2u, r.link.view(0).stats.published) << "the status and the packet";
  EXPECT_EQ(0u, r.link.view(0).stats.dropped);
}

TEST(Publish, TheStatusIsRepeatedEveryFiveMinutes) {
  Rig r;
  r.connectSlot1();
  EXPECT_EQ(1, r.client[0].statusPublished());
  r.tick(kStatusIntervalMs - 1000);
  r.tick();
  EXPECT_EQ(1, r.client[0].statusPublished()) << "not yet";
  r.tick(1000);
  r.tick();
  EXPECT_EQ(2, r.client[0].statusPublished());
}

TEST(Publish, TheQueueIsBoundedInBytesOldestDroppedAndCounted) {
  Rig r;
  r.connectSlot1();
  r.client[0].acceptPublish = false;  // the client takes nothing: everything queues
  std::string m[6];
  for (int i = 0; i < 6; i++) {
    m[i] = r.packetMessage(i);
    m[i].resize(600, 'x');  // 600 bytes each: three fit in kQueueBytes, the fourth evicts the first
    r.link.onPacketMessage(m[i].c_str(), m[i].size());
  }
  SlotView v = r.link.view(0);
  EXPECT_EQ(3u * 602u, v.queued);
  EXPECT_EQ(3u, v.stats.dropped);
  r.client[0].acceptPublish = true;
  r.tick();
  ASSERT_EQ(3, r.client[0].packetsPublished());
  EXPECT_EQ(m[3], r.client[0].published[1].payload) << "the oldest kept goes first";
  EXPECT_EQ(m[5], r.client[0].published[3].payload);
  EXPECT_EQ(0u, r.link.view(0).queued);
}

TEST(Publish, AFullOutboxHoldsTheQueueAndASlotNotConnectedDrops) {
  Rig r;
  r.connectSlot1();
  r.client[0].outbox = kOutboxBytes;  // the client's own outbox is full
  const std::string m = r.packetMessage(7);
  r.link.onPacketMessage(m.c_str(), m.size());
  r.tick();
  EXPECT_EQ(0, r.client[0].packetsPublished());
  EXPECT_EQ(m.size() + 2, r.link.view(0).queued);
  r.client[0].outbox = 0;
  r.tick();
  EXPECT_EQ(1, r.client[0].packetsPublished());

  // Down: a message is dropped at once, not kept for later.
  r.client[0].drop();
  r.tick();
  ASSERT_EQ(SlotState::BackingOff, r.state(0));
  r.link.onPacketMessage(m.c_str(), m.size());
  EXPECT_EQ(1u, r.link.view(0).stats.dropped);
  EXPECT_EQ(0u, r.link.view(0).queued);
}

// ---- refused, lost, backing off -----------------------------------------------------------------

TEST(Refused, ASignInRefusedBacksOffFiveSecondsDoublingToTenMinutesWithTheReason) {
  Rig r;
  r.startSlot1();
  r.client[0].refuse("bad username or password (CONNACK 4)");
  r.tick();
  SlotView v = r.link.view(0);
  EXPECT_EQ(SlotState::BackingOff, v.state);
  EXPECT_EQ(1u, v.stats.refusals);
  EXPECT_EQ(jittered(5000), v.waitMs);
  EXPECT_STREQ("bad username or password (CONNACK 4)", v.lastError);
  EXPECT_EQ(1, r.client[0].closes);
  EXPECT_NE(std::string::npos, logText().find("sign-in refused: bad username or password (CONNACK 4)"));

  // The ladder: 5, 10, 20, 40 ... 600 s, each attempt a new open with a new token.
  uint32_t expected = 5000;
  for (int attempt = 2; attempt <= 12; attempt++) {
    r.tick(jittered(expected));
    r.tick();
    ASSERT_EQ(SlotState::Connecting, r.state(0)) << "attempt " << attempt;
    ASSERT_EQ(static_cast<size_t>(attempt), r.client[0].opens.size());
    r.client[0].refuse("not authorized (CONNACK 5)");
    r.tick();
    expected = expected * 2 > kMaxBackoffMs ? kMaxBackoffMs : expected * 2;
    EXPECT_EQ(jittered(expected), r.link.view(0).waitMs) << "attempt " << attempt;
  }
  EXPECT_EQ(jittered(kMaxBackoffMs), r.link.view(0).waitMs) << "ten minutes is the ceiling";
  EXPECT_EQ(12u, r.link.view(0).stats.refusals);
}

TEST(Lost, AConnectionLostReconnectsAfterTheBackoffAndAStableSessionClearsIt) {
  Rig r;
  r.connectSlot1();
  r.client[0].drop();
  r.tick();
  SlotView v = r.link.view(0);
  EXPECT_EQ(SlotState::BackingOff, v.state);
  EXPECT_EQ(1u, v.stats.errors);
  EXPECT_STREQ("connection lost", v.lastError);
  r.tick(jittered(5000));
  r.tick();
  ASSERT_EQ(SlotState::Connecting, r.state(0));
  r.client[0].accept();
  r.tick();
  EXPECT_EQ(SlotState::Connected, r.state(0));
  EXPECT_EQ(2u, r.link.view(0).stats.connects);
  EXPECT_EQ(2, r.client[0].statusPublished()) << "a status on every connect";

  // Dropped again at once: the step doubles (the session was not up for two minutes)...
  r.client[0].drop();
  r.tick();
  EXPECT_EQ(jittered(10000), r.link.view(0).waitMs);
  r.tick(jittered(10000));
  r.tick();
  r.client[0].accept();
  r.tick();
  ASSERT_EQ(SlotState::Connected, r.state(0));
  // ...and after two minutes up it is forgotten: the next loss starts at 5 s again.
  r.tick(kStableMs);
  r.tick();
  r.client[0].drop();
  r.tick();
  EXPECT_EQ(jittered(5000), r.link.view(0).waitMs);
}

TEST(Lost, NoAnswerInSixtySecondsIsAFailure) {
  Rig r;
  r.startSlot1();
  r.tick(kConnectTimeoutMs + 1);
  r.tick();
  EXPECT_EQ(SlotState::BackingOff, r.state(0));
  EXPECT_STREQ("no answer from the broker in 60 s", r.link.view(0).lastError);
}

TEST(Lost, AClientThatCannotStartBacksOffWithItsReason) {
  Rig r;
  r.client[0].failOpen = true;
  r.client[0].failReason = "no memory for the client";
  r.configure();
  r.link.setLinkUp(true);
  r.tick();
  EXPECT_EQ(SlotState::BackingOff, r.state(0));
  EXPECT_STREQ("no memory for the client", r.link.view(0).lastError);
  EXPECT_EQ(1u, r.link.view(0).stats.errors);
}

// ---- the clock -----------------------------------------------------------------------------------

TEST(Clock, NoClockNoTokenNoConnection) {
  Rig r;
  g_epoch = 0;
  r.configure();
  r.link.setLinkUp(true);
  r.tick();
  EXPECT_EQ(SlotState::WaitingForClock, r.state(0));
  EXPECT_EQ(0u, r.client[0].opens.size());
  r.tick(60000);
  r.tick();
  EXPECT_EQ(SlotState::WaitingForClock, r.state(0)) << "a minute later, still no clock, still nothing";
  g_epoch = 1793016000;
  r.tick();
  EXPECT_EQ(SlotState::Connecting, r.state(0));
  ASSERT_EQ(1u, r.client[0].opens.size());
  // The token was made with the clock that just arrived: its claims decode to that iat.
  const std::string token = r.client[0].lastOpen().password;
  const size_t dot1 = token.find('.');
  const size_t dot2 = token.find('.', dot1 + 1);
  const std::string claims64 = token.substr(dot1 + 1, dot2 - dot1 - 1);
  std::vector<uint8_t> claims(claims64.size());
  const size_t n = base64UrlDecode(claims64.c_str(), claims64.size(), claims.data(), claims.size());
  const std::string json(reinterpret_cast<const char*>(claims.data()), n);
  EXPECT_NE(std::string::npos, json.find("\"iat\":1793016000"));
  EXPECT_NE(std::string::npos, json.find("\"exp\":" + std::to_string(1793016000 + kDefaultTokenLifetime)));
  EXPECT_NE(std::string::npos, json.find("\"aud\":\"mqtt.meshview.dk\""));
  EXPECT_NE(std::string::npos, json.find("\"owner\":\"0123456789ABCDEF"));
  EXPECT_NE(std::string::npos, json.find("\"email\":\"owner@example.dk\""));
  EXPECT_NE(std::string::npos, json.find("\"client\":\"meshcore/test\""));
}

TEST(Clock, TheTokenIsRenewedFiveMinutesBeforeItExpiresWithANewSession) {
  Rig r;
  r.connectSlot1();
  const std::string first = r.client[0].lastOpen().password;
  // Lifetime 86 400 for slot 1, renewal buffer 300: nothing until exp - 300.
  r.tick((kDefaultTokenLifetime - 301) * 1000);
  r.tick();
  EXPECT_EQ(SlotState::Connected, r.state(0));
  EXPECT_EQ(1u, r.client[0].opens.size());
  EXPECT_EQ(0u, r.link.view(0).stats.renewals);
  r.tick(1000);
  r.tick();
  EXPECT_EQ(SlotState::Connecting, r.state(0)) << "closed and opened again with a new token";
  ASSERT_EQ(2u, r.client[0].opens.size());
  EXPECT_NE(first, r.client[0].lastOpen().password);
  EXPECT_EQ(1u, r.link.view(0).stats.renewals);
  EXPECT_EQ(1, r.client[0].closes);
  r.client[0].accept();
  r.tick();
  EXPECT_EQ(SlotState::Connected, r.state(0));
  EXPECT_EQ(0u, r.link.view(0).stats.errors) << "a renewal is not a failure";
}

TEST(Clock, TheSecondSlotsTokenExpiresEarlierSoTheTwoDoNotRenewTogether) {
  Rig r;
  r.settings.slot[1].preset = "meshmapper";
  r.connectSlot1();
  r.tick(kHeapRecheckMs);
  r.tick();
  ASSERT_EQ(SlotState::Connecting, r.state(1));
  r.client[1].accept();
  r.tick();
  ASSERT_EQ(SlotState::Connected, r.state(1));
  // Slot 2's lifetime is 86 100: it renews 300 s before slot 1 does.
  const uint32_t slot2Lifetime = tokenLifetime(0, 1);
  EXPECT_EQ(86100u, slot2Lifetime);
  r.tick((slot2Lifetime - 300) * 1000);
  r.tick();
  EXPECT_EQ(1u, r.link.view(1).stats.renewals);
  EXPECT_EQ(0u, r.link.view(0).stats.renewals);
}

// ---- the heap -----------------------------------------------------------------------------------

TEST(Heap, ASlotOpensOnlyAboveTheReserveAndLooksAgainEveryFiveSeconds) {
  Rig r;
  g_free = kHeapReserveFree - 1;
  r.configure();
  r.link.setLinkUp(true);
  r.tick();
  SlotView v = r.link.view(0);
  EXPECT_EQ(SlotState::WaitingForHeap, v.state);
  EXPECT_EQ(0u, r.client[0].opens.size());
  EXPECT_NE(std::string::npos, std::string(v.lastError).find("heap "));
  EXPECT_NE(std::string::npos, logText().find("waiting for heap"));

  g_free = 200 * 1024;
  r.tick();
  EXPECT_EQ(SlotState::WaitingForHeap, r.state(0)) << "not before the recheck";
  r.tick(kHeapRecheckMs);
  r.tick();
  EXPECT_EQ(SlotState::Connecting, r.state(0));

  // The largest block alone can hold a slot back: free heap in fragments is no handshake.
  Rig f;
  g_largest = kHeapReserveLargest - 1;
  f.configure();
  f.link.setLinkUp(true);
  f.tick();
  EXPECT_EQ(SlotState::WaitingForHeap, f.state(0));
}

TEST(Heap, TheUplinksMemoryFailureTakesTheHighestSlotDownForTenMinutes) {
  Rig r;
  r.settings.slot[1].preset = "meshmapper";
  r.connectSlot1();
  r.tick(kHeapRecheckMs);
  r.tick();
  r.client[1].accept();
  r.tick();
  ASSERT_EQ(SlotState::Connected, r.state(0));
  ASSERT_EQ(SlotState::Connected, r.state(1));
  EXPECT_EQ(2, r.link.connectedSlots());

  r.link.onUplinkMemoryFailure();
  EXPECT_EQ(SlotState::Connected, r.state(0)) << "slot 1 keeps its session";
  SlotView v = r.link.view(1);
  EXPECT_EQ(SlotState::WaitingForHeap, v.state);
  EXPECT_EQ(1u, v.stats.heapYields);
  EXPECT_EQ(kHeapYieldMs, v.waitMs);
  EXPECT_EQ(1, r.client[1].closes);
  EXPECT_NE(std::string::npos, logText().find("slot 2 meshmapper: closed for 10 min"));

  // Nothing for ten minutes, however much heap there is...
  r.tick(kHeapYieldMs - 1000);
  r.tick();
  EXPECT_EQ(SlotState::WaitingForHeap, r.state(1));
  EXPECT_EQ(1u, r.client[1].opens.size());
  // ...then it opens again.
  r.tick(1000);
  r.tick();
  EXPECT_EQ(SlotState::Connecting, r.state(1));
  EXPECT_EQ(2u, r.client[1].opens.size());

  // A second failure with slot 2 down for another reason takes slot 1, the only one up.
  r.client[1].fail(ErrorKind::Other, "the host did not answer");
  r.tick();
  ASSERT_EQ(SlotState::BackingOff, r.state(1));
  r.link.onUplinkMemoryFailure();
  EXPECT_EQ(SlotState::WaitingForHeap, r.state(0));
  EXPECT_EQ(1u, r.link.view(0).stats.heapYields);
}

TEST(Heap, OneHandshakeAtATime) {
  Rig r;
  r.settings.slot[1].preset = "meshmapper";
  r.configure();
  r.link.setLinkUp(true);
  r.tick();
  EXPECT_EQ(SlotState::Connecting, r.state(0));
  EXPECT_EQ(SlotState::WaitingForHeap, r.state(1));
  EXPECT_STREQ("waiting for the other slot's handshake", r.link.view(1).lastError);
  EXPECT_EQ(0u, r.client[1].opens.size());
  r.tick(kHeapRecheckMs);
  r.tick();
  EXPECT_EQ(SlotState::WaitingForHeap, r.state(1)) << "slot 1 is still in its handshake";
  r.client[0].accept();
  r.tick();
  r.tick(kHeapRecheckMs);
  r.tick();
  EXPECT_EQ(SlotState::Connecting, r.state(1));
}

TEST(Heap, AClientThatRanOutOfMemoryWaitsForHeapRatherThanBackingOff) {
  Rig r;
  r.startSlot1();
  r.client[0].fail(ErrorKind::NoMemory, "no memory for the TLS session (-0x7f00)");
  r.tick();
  EXPECT_EQ(SlotState::WaitingForHeap, r.state(0));
  EXPECT_STREQ("no memory for the TLS session (-0x7f00)", r.link.view(0).lastError);
  EXPECT_EQ(1u, r.link.view(0).stats.errors);
}

// ---- the chain -------------------------------------------------------------------------------------

TEST(Chain, AChainThatEndsElsewhereIsTriedAgainstTheCoresBundleOnceThenRefused) {
  Rig r;
  r.settings.slot[0].preset = "custom";
  r.settings.slot[0].server = "wss://broker.example.net:443/mqtt";
  r.settings.slot[0].audience = "broker.example.net";
  r.startSlot1();
  EXPECT_FALSE(r.client[0].lastOpen().bundle);
  r.client[0].fail(ErrorKind::ChainNotVerified, "the certificate chain did not verify (flags 0x8)");
  r.tick();
  ASSERT_EQ(SlotState::Connecting, r.state(0)) << "tried again at once, against the bundle";
  ASSERT_EQ(2u, r.client[0].opens.size());
  EXPECT_TRUE(r.client[0].lastOpen().bundle);
  EXPECT_STREQ("the core's bundle", r.link.view(0).root);
  EXPECT_NE(std::string::npos, logText().find("did not end at ISRG Root X1/X2; trying the core's bundle"));

  r.client[0].fail(ErrorKind::ChainNotVerified, "the certificate chain did not verify (flags 0x8)");
  r.tick();
  EXPECT_EQ(SlotState::BackingOff, r.state(0));
  EXPECT_STREQ("refused: the certificate chain ends at no root this firmware carries nor the core's bundle", r.link.view(0).lastError);
  EXPECT_EQ(2u, r.link.view(0).stats.errors);
  // The choice is remembered: the next attempt is the bundle's.
  r.tick(jittered(5000));
  r.tick();
  ASSERT_EQ(3u, r.client[0].opens.size());
  EXPECT_TRUE(r.client[0].lastOpen().bundle);
}

TEST(Chain, ACustomSlotWithAUsernameSignsInWithItAndNoToken) {
  Rig r;
  r.settings.slot[0].preset = "custom";
  r.settings.slot[0].server = "mqtts://broker.example.net:8883";
  r.settings.slot[0].username = "window";
  r.settings.slot[0].password = "correct horse battery staple";
  r.settings.slot[0].topic = "mesh/{device}/{type}";
  r.startSlot1();
  const Opened& o = r.client[0].lastOpen();
  EXPECT_EQ("window", o.username);
  EXPECT_EQ("correct horse battery staple", o.password);
  EXPECT_EQ("mesh/" + r.pubHex + "/status", o.willTopic);
}

// ---- wifi, settings ------------------------------------------------------------------------------

TEST(Link, WifiGoingDownClosesTheSlotAndComingBackOpensIt) {
  Rig r;
  r.connectSlot1();
  r.link.setLinkUp(false);
  r.tick();
  EXPECT_EQ(SlotState::WaitingForLink, r.state(0));
  EXPECT_EQ(1, r.client[0].closes);
  EXPECT_NE(std::string::npos, logText().find("wifi is down, connection closed"));
  r.tick(60000);
  r.tick();
  EXPECT_EQ(1u, r.client[0].opens.size()) << "nothing while wifi is down";
  r.link.setLinkUp(true);
  r.tick();
  EXPECT_EQ(SlotState::Connecting, r.state(0));
  EXPECT_EQ(2u, r.client[0].opens.size());
}

TEST(Settings, AChangeToOneSlotRestartsThatSlotAloneAndTheObserverOffClosesBoth) {
  Rig r;
  r.settings.slot[1].preset = "meshmapper";
  r.connectSlot1();
  r.tick(kHeapRecheckMs);
  r.tick();
  r.client[1].accept();
  r.tick();
  ASSERT_EQ(2, r.link.connectedSlots());

  // mqtt.origin changes nothing about a connection; nothing moves.
  r.settings.origin = "Another";
  r.configure();
  EXPECT_EQ(2, r.link.connectedSlots());
  EXPECT_EQ(0, r.client[0].closes);
  EXPECT_EQ(0, r.client[1].closes);

  // Slot 2 becomes custom: slot 2 starts over, slot 1 keeps its session.
  r.settings.slot[1].preset = "custom";
  r.settings.slot[1].server = "wss://broker.example.net:443/mqtt";
  r.settings.slot[1].audience = "broker.example.net";
  r.configure();
  EXPECT_EQ(SlotState::Connected, r.state(0));
  EXPECT_EQ(SlotState::WaitingForLink, r.state(1));
  EXPECT_EQ(1, r.client[1].closes);
  r.tick();
  EXPECT_EQ(SlotState::Connecting, r.state(1));
  EXPECT_EQ("wss://broker.example.net:443/mqtt", r.client[1].lastOpen().uri);

  // The owner is in every token: both slots start over.
  r.settings.owner = "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210";
  r.configure();
  EXPECT_EQ(1, r.client[0].closes);
  EXPECT_EQ(2, r.client[1].closes);

  // mqtt.iata unset: the observer is off, every slot closes and is Off.
  r.tick();
  r.client[0].accept();
  r.tick();
  r.settings.iata = "";
  r.configure();
  EXPECT_EQ(SlotState::Off, r.state(0));
  EXPECT_EQ(SlotState::Off, r.state(1));
  EXPECT_EQ(0, r.link.configuredSlots());
  EXPECT_EQ(2, r.client[0].closes);
}

TEST(Settings, TheConsoleLineNamesTheSlotTheStateTheCountsAndTheLastError) {
  Rig r;
  char line[320];
  r.connectSlot1();
  r.link.describe(0, line, sizeof line);
  EXPECT_EQ(0u, std::string(line).find("slot 1 meshview (wss://mqtt.meshview.dk:443/mqtt): connected since 12:00:00, root ISRG Root X1/X2; 1 published, 0 dropped, 0 queued; 1 connects, 0 refusal(s), 0 error(s), 0 renewal(s), gave way 0 time(s); last error: none")) << line;
  r.client[0].refuse("not authorized (CONNACK 5)");
  r.tick();
  r.link.describe(0, line, sizeof line);
  EXPECT_NE(std::string::npos, std::string(line).find("backing off since 12:00:00, next attempt in 4 s")) << line;
  EXPECT_NE(std::string::npos, std::string(line).find("1 refusal(s)")) << line;
  EXPECT_NE(std::string::npos, std::string(line).find("last error: not authorized (CONNACK 5)")) << line;
  r.link.describe(1, line, sizeof line);
  EXPECT_STREQ("slot 2 none: off (preset is none)", line);
}

// The seven words a slot's state can be. They are a **contract** since task p1.7-28: the
// heartbeat carries each slot's state and meshmaster.dk spells the same seven in
// `AgentObserverStates` (its D-46, its task p4.17-140, and the fixtures' own README), which is
// why the enum's names and the words differ in one place - `WaitingForLink` is
// `waiting-for-wifi`, because WiFi is what the owner of a board can see. The server keeps a word
// it has not heard of rather than refusing it, so renaming one here would not break a heartbeat;
// it would quietly change what the exit node's Observer card says, which is worse. This test is
// the fence: a state renamed on this board fails here until meshmaster.dk has heard of it.
TEST(Settings, TheSevenStateWordsAreTheOnesMeshMasterSpells) {
  EXPECT_STREQ("off", slotStateName(SlotState::Off));
  EXPECT_STREQ("waiting-for-wifi", slotStateName(SlotState::WaitingForLink));
  EXPECT_STREQ("waiting-for-clock", slotStateName(SlotState::WaitingForClock));
  EXPECT_STREQ("waiting-for-heap", slotStateName(SlotState::WaitingForHeap));
  EXPECT_STREQ("connecting", slotStateName(SlotState::Connecting));
  EXPECT_STREQ("connected", slotStateName(SlotState::Connected));
  EXPECT_STREQ("backing-off", slotStateName(SlotState::BackingOff));
  // And every one of them is one word of the bound the server puts on the member (32 characters,
  // `AgentObserverRules.StateLength`), with no space in it: a state is a word and not a sentence.
  for (const SlotState state : {SlotState::Off, SlotState::WaitingForLink, SlotState::WaitingForClock,
                                SlotState::WaitingForHeap, SlotState::Connecting, SlotState::Connected,
                                SlotState::BackingOff}) {
    const std::string word = slotStateName(state);
    EXPECT_LE(word.size(), 32u) << word;
    EXPECT_EQ(std::string::npos, word.find(' ')) << word;
  }
}

// ---- the log --------------------------------------------------------------------------------------

TEST(Log, AfterARunTheRingHoldsCountsAndReasonsAndNeverATokenAPasswordOrAPayload) {
  Rig r;
  r.settings.slot[1].preset = "custom";
  r.settings.slot[1].server = "wss://broker.example.net:443/mqtt";
  r.settings.slot[1].username = "window";
  r.settings.slot[1].password = "correct horse battery staple";
  r.connectSlot1();
  const std::string token = r.client[0].lastOpen().password;
  r.tick(kHeapRecheckMs);
  r.tick();
  ASSERT_EQ(SlotState::Connecting, r.state(1));
  r.client[1].refuse("bad username or password (CONNACK 4)");
  r.tick();
  const std::string m = r.packetMessage(42);
  r.link.onPacketMessage(m.c_str(), m.size());
  r.tick();
  r.client[0].drop();
  r.tick();
  r.link.onUplinkMemoryFailure();
  r.link.setLinkUp(false);
  r.tick();

  char ring[mm::LogRing::kCapacity + 1];
  const size_t n = mm::logRing().copyTo(ring, sizeof ring);
  const std::string text(ring, n);
  ASSERT_GT(n, 100u);
  // What is there: the connects, the refusal and its reason, the loss, the wifi.
  EXPECT_NE(std::string::npos, text.find("slot 1 meshview: connected (root ISRG Root X1/X2; 1 connects)"));
  EXPECT_NE(std::string::npos, text.find("slot 2 custom: sign-in refused: bad username or password (CONNACK 4); next attempt in 4 s (1 refusal(s), 0 error(s))"));
  EXPECT_NE(std::string::npos, text.find("slot 1 meshview: connection lost"));
  // What is not: the token, the username it goes with, the password, the payload, the raw hex.
  EXPECT_EQ(std::string::npos, text.find(token));
  EXPECT_EQ(std::string::npos, text.find(token.substr(token.rfind('.') + 1)));  // the signature alone
  EXPECT_EQ(std::string::npos, text.find("v1_"));
  EXPECT_EQ(std::string::npos, text.find("correct horse"));
  EXPECT_EQ(std::string::npos, text.find("DEADBEEF"));
  EXPECT_EQ(std::string::npos, text.find("PACKET"));
  EXPECT_EQ(std::string::npos, text.find("{\""));
  // And the same over every line the sink saw, in case the ring wrapped.
  const std::string all = logText();
  EXPECT_EQ(std::string::npos, all.find(token));
  EXPECT_EQ(std::string::npos, all.find("correct horse"));
  EXPECT_EQ(std::string::npos, all.find("DEADBEEF"));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
