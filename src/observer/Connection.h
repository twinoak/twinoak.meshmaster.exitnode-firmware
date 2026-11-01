// The broker connection (task p1.7-27): two slots, each a state machine over an MQTT client it
// does not own the bytes of, that puts task 26's messages on the air - signed in with the
// node's own token, publishing every packet message the radio produced and a retained status -
// on a board with no PSRAM beside a MeshMaster uplink that must never be the one to lose.
//
// Host-portable, and deliberately so: the client is an interface (Client, below), the board's
// implementation is src/net/MqttClient.h over ESP-IDF's esp-mqtt, the test's is a fake, and
// everything this file decides - when to sign in, when to wait, what to drop, when to give up
// memory - runs on a host with a clock the test moves (test/test_connection). Nothing here
// names the network, logs a line itself or holds a payload longer than the queue below: the log
// goes through one callback the board points at the log, so that test/test_observer's grep of
// src/observer/ still holds and a payload has no path to the port.
//
// What a slot does, in the order it does it:
//
//   waiting-for-link    WiFi is down. Nothing is tried.
//   waiting-for-clock   the clock is not set. A token carries `iat` and `exp`, and a packet
//                       message carries the wall clock, so nothing is sent before there is one
//                       (the observer firmware's jwtClockAvailable, MQTTConnectionPolicy.h:221).
//   waiting-for-heap    the heap could not take this slot and the uplink's next call on top of
//                       it (gateNeed, below: the board's figures, what the uplink's last call
//                       dipped, the floor and the margin); or the uplink's call just failed for
//                       want of memory, or dipped under the floor, and this slot gave way for
//                       ten minutes. Re-checked every kHeapRecheckMs.
//   connecting          the client was opened with the token (or username and password) and
//                       the roots; waiting for the broker's answer, kConnectTimeoutMs at most.
//   connected           publishing: a retained status at once and every kStatusIntervalMs, and
//                       every queued packet message, QoS 0, as the client takes them.
//   backing-off         a refused sign-in, a failed handshake or a lost connection: 5 s, doubled
//                       each time to ten minutes, jittered (agent/Backoff.h), with the reason.
//
// Two connections share one board, so a second slot does not open while the first is still in
// its handshake (two handshakes at once is twice the peak), and the highest slot is the one
// that gives way when the uplink's call fails for want of memory, or - since task p1.8-31 -
// when a call that went through dipped the heap under kHeapFloorBytes: the radio first, then
// the MeshMaster uplink, then the observer (tasks/phase-1.7/27, tasks/phase-1.8/31). How many
// slots a board opens at all is Board.h's: one on a Heltec V3, two on a V4.
//
// What is published, as the observer firmware does (agessaman/MeshCore observer-firmware at
// d664d89d): every received packet as a `packets` message, QoS 0, not retained
// (MQTTBridge.cpp:4263); a `status` message QoS 1 at connect and every mqtt.interval - that
// firmware's default of five minutes (MQTTBridge.cpp:981) - retained where the preset allows
// (:4117); and, beyond what that firmware does (it sets no will), a retained `offline` status as
// the last will, which the task asks for. The token is renewed before it expires exactly as
// Token.h reads it from MQTTConnectionPolicy.h, by closing the connection and opening it with a
// new token, since MQTT takes a password at CONNECT and nowhere after.
//
// The queue between the radio and the client is bytes, not messages: kQueueBytes per slot,
// oldest dropped and counted when a new message does not fit, and a slot that is not connected
// drops at once rather than hoarding (the task's words). The client's own outbox is the second
// queue, bounded by kOutboxBytes before another message is handed to it.

#pragma once

#include <cstddef>
#include <cstdint>

#include <agent/Log.h>

#include "Board.h"
#include "Config.h"
#include "Dialect.h"
#include "Token.h"
#include "Topics.h"

namespace mm {
namespace observer {

// ---- the client, as this file sees it ------------------------------------------------------

// What a slot asks of its client when it opens. Every pointer is valid until close(): the
// board's client copies what esp-mqtt copies and keeps the PEM pointer, which is flash.
struct ClientConfig {
  const char* uri = "";            // wss://host:port/path or mqtts://host:port
  const char* caPem = nullptr;     // the carried roots (src/agent/TlsRoots.h); nullptr = the core's bundle
  const char* username = "";       // "" sends no username
  const char* password = "";       // the token, or the custom slot's password; "" sends none
  uint16_t keepaliveS = 0;         // 0 = the client's default
  const char* willTopic = nullptr; // nullptr = no will
  const char* willPayload = nullptr;
  size_t willLen = 0;
  bool willRetain = false;
};

// What a client reports back, one at a time, from takeEvent().
enum class ClientEvent : uint8_t {
  None,
  Connected,     // the broker accepted the session
  Disconnected,  // the session ended (after Connected), or the transport closed before it began
  Refused,       // the broker answered CONNECT with a refusal: `reason` says which
  Error,         // the transport failed: `kind` and `reason` say how
};

// How a transport failed, as far as this file cares: the chain is the one it acts on.
enum class ErrorKind : uint8_t {
  Other,            // DNS, TCP, a timeout, a handshake failure that is not the chain
  ChainNotVerified, // the certificate chain did not end at a root in the CA store given
  NoMemory,         // the client, its task or its TLS context could not be allocated
};

class Client {
 public:
  // Begin connecting. False when the client could not even start (no memory, a URI the client
  // refuses): `reason` (cap bytes) says why, and nothing is open.
  virtual bool open(const ClientConfig& cfg, char* reason, size_t cap) = 0;
  // End the session, stop the client's task and free everything it held, including its TLS
  // context. Safe to call when nothing is open.
  virtual void close() = 0;
  // Hand one message to the client's outbox. True when taken; false when it was not (the outbox
  // is full, or nothing is open) and the caller keeps it.
  virtual bool publish(const char* topic, const char* payload, size_t len, uint8_t qos, bool retain) = 0;
  // The next event since the last call, or None. `reason` (cap bytes) is one short phrase a
  // person can read - a CONNACK code's name, an esp-tls error's - never a byte of anything sent.
  virtual ClientEvent takeEvent(ErrorKind& kind, char* reason, size_t cap) = 0;
  // Bytes waiting in the client's own outbox, for the bound on handing it more.
  virtual size_t outboxBytes() = 0;

 protected:
  ~Client() {}
};

// ---- what the board provides ---------------------------------------------------------------

struct Environment {
  uint32_t (*millis)() = nullptr;        // monotonic, wraps
  uint32_t (*epoch)() = nullptr;         // UNIX seconds, 0 while there is no time
  uint32_t (*freeHeap)() = nullptr;      // bytes of internal heap free now
  uint32_t (*largestBlock)() = nullptr;  // the largest internal allocation possible now
  uint32_t (*freePsram)() = nullptr;     // bytes of PSRAM free now; nullptr or 0 = none (a V3)
  uint32_t (*largestPsram)() = nullptr;  // the largest PSRAM allocation possible now
  uint32_t (*random)() = nullptr;        // for the backoff's jitter
  const char* caPem = nullptr;           // the carried roots, src/agent/TlsRoots.h's kCarriedRootsPem
  const BoardProfile* board = nullptr;   // nullptr = boardProfile(), the one this image is built for
};

// The status message, made by whoever knows the board (Observer.h on the board, the test's
// stand-in on the host): `status` is "online" or "offline"; the length written, or 0.
class StatusSource {
 public:
  virtual size_t formatStatus(const char* status, char* out, size_t cap) = 0;

 protected:
  ~StatusSource() {}
};

// One finished log line, with its level. The board's is the log (src/observer/Observer.cpp); the
// test's collects them. A line here is counts and reasons - never a payload, a token or a
// password - and test/test_connection reads the ring back to hold that.
using ReportLine = void (*)(void* ctx, LogLevel level, const char* line);

// ---- the policy's numbers -------------------------------------------------------------------

// The gate (task p1.8-31). What one slot costs and what the uplink's call dips are the board's
// own figures (Board.h: measured on a Heltec V3 on 28 October 2026, derived for the V4), not the
// observer firmware's 40 KB that task 27 borrowed. A slot opens while
//
//   free internal heap >= board.slotInternalBytes + uplink dip + kHeapFloorBytes + kGateMarginBytes
//
// where the uplink dip is what the uplink's last call dipped the internal heap - freeBefore
// less lowest, read from task p1.8-30's cost record through onUplinkCall() - or, before any
// call has been measured on this boot, the board's conservative constant; and the largest free
// block holds one TLS content buffer (on a V4 the buffers are PSRAM's: that heap is checked
// instead, and the internal block only has to hold the handshake's largest internal allocation).
// After a slot is open the gate keeps watching: every uplink call's lowest free heap is read,
// and one under kHeapFloorBytes closes the highest slot for kHeapYieldMs, counted as *gave way*.
//
// One of mbedtls's two content buffers as one allocation (CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN
// 16 384 in this core's sdkconfig, not asymmetric, plus its header room): what the heap holding
// the buffers must be able to allocate at all.
constexpr uint32_t kTlsBufferBytes = 16 * 1024 + 1024;
// On a board with PSRAM, mbedtls allocations of this size and over go there (src/platform/
// TlsHeap.cpp), which is the core's own line for malloc (CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL
// 4096); what stays internal is smaller than this, so this is what the largest internal block
// must hold there.
constexpr uint32_t kPsramThresholdBytes = 4096;
// What WiFi and the radio need to survive a call: not measured. The WiFi driver takes its
// dynamic receive buffers from the internal heap as frames arrive (CONFIG_ESP32_WIFI_DYNAMIC_RX_
// BUFFER_NUM 32 of about 1.6 KB, a few in flight at a time), lwIP its segments for the open
// connections, the uplink's HTTP client its 1 KB chunk buffer and the reply's String, and the
// console and the log a line each; 16 KB holds a handful of each twice over. The bench of 28 October saw a call
// go through with 3 088 bytes free, so this floor is above survival and below comfort, and it is
// the one constant the bench walk of this pile is most likely to move.
constexpr uint32_t kHeapFloorBytes = 16 * 1024;
// The gate's margin over the arithmetic: the heap's fragmentation and the slot's own outbox
// while a call is in flight. Not measured; one allocation of the PSRAM threshold's size.
constexpr uint32_t kGateMarginBytes = 4096;

constexpr uint32_t kHeapRecheckMs = 5000;         // how often waiting-for-heap looks again
constexpr uint32_t kHeapYieldMs = 10 * 60 * 1000; // how long a slot stays down after it gave way to the uplink
constexpr uint32_t kFirstBackoffMs = 5000;        // the task: 5 s doubling to ten minutes
constexpr uint32_t kMaxBackoffMs = 10 * 60 * 1000;
constexpr uint32_t kStableMs = 120 * 1000;        // up this long clears the backoff (MQTTConnectionPolicy.h:12)
constexpr uint32_t kConnectTimeoutMs = 60 * 1000; // no answer from the client in this long is a failure
constexpr uint32_t kStatusIntervalMs = 5 * 60 * 1000;  // the observer firmware's mqtt.interval default (MQTTBridge.cpp:981)
// Keepalive for every slot on a board without PSRAM: the observer firmware's 75 s there
// (MQTTBridge.cpp:5125-5135: Cloudflare closes an idle WebSocket at 100 s, and a preset's own
// shorter value is not applied without PSRAM, to spare the TLS churn).
constexpr uint16_t kKeepaliveS = 75;

constexpr size_t kQueueBytes = 2048;     // per slot, between the radio and the client
constexpr size_t kOutboxBytes = 4096;    // the client's own outbox, before another message is handed over
constexpr size_t kReasonCap = 96;     // one phrase and its numbers: a heap figure line is the longest

// ---- the slots -------------------------------------------------------------------------------

enum class SlotState : uint8_t {
  Off,             // not configured, or the observer is off
  WaitingForLink,
  WaitingForClock,
  WaitingForHeap,
  Connecting,
  Connected,
  BackingOff,
};

const char* slotStateName(SlotState s);

struct SlotStats {
  uint32_t published = 0;   // messages the client took, packets and status alike
  uint32_t dropped = 0;     // packet messages dropped: queue full, or the slot not connected
  uint32_t connects = 0;    // sessions the broker accepted
  uint32_t refusals = 0;    // CONNECTs the broker refused
  uint32_t errors = 0;      // transport failures and lost connections
  uint32_t heapYields = 0;  // times this slot gave way to the uplink
  uint32_t renewals = 0;    // tokens made again before they expired
};

// What a `mqtt.status` line is made of, read by the console's printer.
struct SlotView {
  bool configured = false;
  const char* name = "none";      // the preset's name, or "custom"
  const char* host = "";          // the broker's URL
  SlotState state = SlotState::Off;
  uint32_t sinceMillis = 0;       // when the state was entered
  uint32_t sinceEpoch = 0;        // the same, as the clock had it, or 0
  uint32_t waitMs = 0;            // backing-off / waiting-for-heap: how long until the next look
  const char* root = "";          // "ISRG Root X1/X2" or "the core's bundle"
  const char* lastError = "";     // the last refusal or failure, one phrase, "" when none
  size_t queued = 0;              // bytes waiting in this file's queue
  SlotStats stats;
};

class Connections {
 public:
  // `clients[i]` is slot i's client (kSlots of them); `signer` signs the token; `status` makes
  // the status message; `report`/`reportCtx` take every log line.
  void begin(Client* const clients[kSlots], const Environment& env, Signer& signer, StatusSource& status,
             ReportLine report, void* reportCtx);

  // Read the settings again: resolve both slots, build their topics, and restart a slot whose
  // connection would change (its URL, its sign-in, its topics, the owner or the email in its
  // token). `enabled` is Config.h's observerEnabled(); when false every slot is Off.
  // `publicKeyHex` is 64 hex, any case; `clientVersion` is the token's `client` claim.
  void configure(const ObserverSettings& settings, bool enabled, const char* publicKeyHex, const char* clientVersion);

  // WiFi's state, every pass: a slot opens only while true and closes when it goes false.
  void setLinkUp(bool up);

  // One packet message, already formatted (Dialect.h), for every slot that is configured.
  void onPacketMessage(const char* json, size_t len);

  // The uplink's call failed for want of memory: the highest slot that is up or opening closes
  // and stays down kHeapYieldMs (the task's rule), and is counted.
  void onUplinkMemoryFailure();

  // The uplink made a call (task p1.8-30's cost record: free internal heap before it, the lowest
  // seen during it, free after it). The gate remembers what the call dipped for its next look,
  // and if the lowest went under kHeapFloorBytes while a slot was up or opening, the highest
  // such slot closes and stays down kHeapYieldMs, counted as *gave way* like a memory failure.
  void onUplinkCall(uint32_t freeBefore, uint32_t lowest, uint32_t freeAfter);

  // Every pass of the main loop: events, timers, the queue.
  void loop();

  // Close every slot and leave them Off (a reboot, `erase confirm`).
  void stopAll();

  int configuredSlots() const;
  int connectedSlots() const;
  uint32_t connectFailures() const;  // refusals and errors over both slots, for the status message

  // The board's profile (Board.h): how many slots it opens and the gate's figures.
  const BoardProfile& board() const;
  // How much internal heap a slot needs free to open right now: the board's slot cost, the
  // uplink's dip (measured on this boot, else the board's constant), the floor and the margin.
  uint32_t gateNeedBytes() const;
  // What the uplink's call dips, as the gate uses it now, and whether that was measured on this
  // boot (true) or is the board's constant (false).
  uint32_t uplinkDipBytes() const { return uplinkMeasured_ ? uplinkDip_ : board().uplinkDipBytes; }
  bool uplinkDipMeasured() const { return uplinkMeasured_; }

  SlotView view(int slot) const;
  // One line for `mqtt.status`: "slot 1 meshview: connected since 12:03:14, 120 published, 0 dropped".
  void describe(int slot, char* out, size_t cap) const;
  // The memory line of `mqtt.status`: the rule with the board's figures and the heap as it is now.
  void describeGate(char* out, size_t cap) const;

 private:
  struct Slot {
    ResolvedSlot cfg;
    uint32_t configHash = 0;
    char topicStatus[kTopicCap] = {0};
    char topicPackets[kTopicCap] = {0};
    SlotState state = SlotState::Off;
    uint32_t sinceMillis = 0;
    uint32_t sinceEpoch = 0;
    uint32_t backoffMs = 0;        // the sequence's step, 0 = none earned
    uint32_t nextAttemptAt = 0;    // backing-off: when
    uint32_t holdUntil = 0;        // waiting-for-heap after a yield: until when (0 = no hold)
    uint32_t lastHeapCheckAt = 0;
    uint32_t attemptStartedAt = 0;
    uint32_t connectedAt = 0;
    uint32_t tokenExpiresAt = 0;
    uint32_t statusDueAt = 0;
    bool statusPending = false;
    bool useBundle = false;        // the chain did not end at a carried root: the core's bundle
    char lastError[kReasonCap] = {0};
    // The queue: [len lo][len hi][json]... from the front, the oldest first; a pop moves the
    // rest down (two kilobytes at most), so the message at the front is always contiguous and is
    // handed to the client straight from here.
    uint8_t queue[kQueueBytes] = {0};
    size_t queueSize = 0;
    SlotStats stats;
  };

  void enter(Slot& s, SlotState state);
  void tryToOpen(int i);
  bool heapAllows(Slot& s);
  // The highest slot that is up or opening closes and stays down kHeapYieldMs, with `why` as its
  // last error and `logWhy` on the log; true when there was one.
  bool yieldHighest(const char* why, const char* logWhy);
  void closeAndBackOff(int i, const char* why, bool refused);
  void pollEvents(int i);
  void serveConnected(int i);  // a connected slot's pass: the backoff's reset, the renewal, the status, the queue
  void pumpQueue(int i);
  void publishStatus(int i);
  bool makeSlotToken(int i, char* out, size_t cap);
  void queuePush(Slot& s, const char* json, size_t len);
  void queuePop(Slot& s);
  void report(LogLevel level, const char* fmt, ...) __attribute__((format(printf, 3, 4)));

  Client* clients_[kSlots] = {nullptr, nullptr};
  Environment env_;
  Signer* signer_ = nullptr;
  StatusSource* status_ = nullptr;
  ReportLine report_ = nullptr;
  void* reportCtx_ = nullptr;
  bool linkUp_ = false;
  bool enabled_ = false;
  // What the uplink's last call dipped the internal heap (freeBefore less lowest), once one
  // has been measured on this boot; the board's constant stands in before that.
  uint32_t uplinkDip_ = 0;
  bool uplinkMeasured_ = false;
  uint32_t lastUplinkLowest_ = 0;  // the lowest free heap of the last call, for the memory line
  char publicKeyHex_[kPublicKeyHexLen + 1] = {0};
  char owner_[kPublicKeyHexLen + 1] = {0};
  char email_[64] = {0};
  char clientVersion_[96] = {0};
  Slot slots_[kSlots];
  // Scratch the open() of one slot uses at a time: the token (the password of the CONNECT), the
  // username and the will - all copied by the client before open() returns. The will's buffer is
  // also where a status message is formatted for publishing, since no slot opens while one is
  // being published (both happen in loop(), one after the other).
  char token_[kTokenCap] = {0};
  char username_[kUsernameCap] = {0};
  char status_scratch_[kStatusMessageCap] = {0};
};

}  // namespace observer
}  // namespace mm
