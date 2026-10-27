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
//   waiting-for-heap    free heap or the largest free block is under the reserve that leaves the
//                       uplink its own TLS handshake (kHeapReserve*, below); or the uplink's call
//                       just failed for want of memory and this slot gave way for ten minutes.
//                       Re-checked every kHeapRecheckMs.
//   connecting          the client was opened with the token (or username and password) and
//                       the roots; waiting for the broker's answer, kConnectTimeoutMs at most.
//   connected           publishing: a retained status at once and every kStatusIntervalMs, and
//                       every queued packet message, QoS 0, as the client takes them.
//   backing-off         a refused sign-in, a failed handshake or a lost connection: 5 s, doubled
//                       each time to ten minutes, jittered (agent/Backoff.h), with the reason.
//
// Two connections share one board, so a second slot does not open while the first is still in
// its handshake (two handshakes at once is twice the peak), and the highest slot is the one
// that gives way when the uplink's call fails for want of memory: the radio first, then the
// MeshMaster uplink, then the observer (tasks/phase-1.7/27).
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
  uint32_t (*freeHeap)() = nullptr;      // bytes free now
  uint32_t (*largestBlock)() = nullptr;  // the largest allocation possible now
  uint32_t (*random)() = nullptr;        // for the backoff's jitter
  const char* caPem = nullptr;           // the carried roots, src/agent/TlsRoots.h's kCarriedRootsPem
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

// A TLS connection on this board: the observer firmware's own figure for one WSS slot
// (MQTT_IMPLEMENTATION.md, "about 40 KB of internal heap per TLS/WSS connection";
// MQTTConnectionPolicy.h:228) - mbedtls' two content buffers of CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN
// (16 384 each in this core's sdkconfig, not asymmetric) and the handshake's working set.
constexpr uint32_t kTlsConnectionBytes = 40 * 1024;
// One of those content buffers, as one allocation: what the largest free block must hold.
constexpr uint32_t kTlsBufferBytes = 16 * 1024 + 1024;
// What a slot leaves untouched for the uplink: its own handshake and a margin for the HTTP
// client's body buffers and the heap's own fragmentation. Not measured on a board - nothing
// measured on a board says what the uplink's handshake takes (the [Thomas] item of
// tasks/phase-1.6/FOLLOWUPS.md) - so it is the observer firmware's figure plus a fifth, written
// as a constant a measurement can move.
constexpr uint32_t kUplinkReserveBytes = kTlsConnectionBytes + 8 * 1024;
// A slot opens only while this much is free: its own connection and the uplink's reserve...
constexpr uint32_t kHeapReserveFree = kTlsConnectionBytes + kUplinkReserveBytes;
// ...and the largest block holds two content buffers, its own and one left for the uplink.
constexpr uint32_t kHeapReserveLargest = 2 * kTlsBufferBytes;

constexpr uint32_t kHeapRecheckMs = 5000;         // how often waiting-for-heap looks again
constexpr uint32_t kHeapYieldMs = 10 * 60 * 1000; // how long a slot stays down after the uplink failed for memory
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

  // Every pass of the main loop: events, timers, the queue.
  void loop();

  // Close every slot and leave them Off (a reboot, `erase confirm`).
  void stopAll();

  int configuredSlots() const;
  int connectedSlots() const;
  uint32_t connectFailures() const;  // refusals and errors over both slots, for the status message

  SlotView view(int slot) const;
  // One line for `mqtt.status`: "slot 1 meshview: connected since 12:03:14, 120 published, 0 dropped".
  void describe(int slot, char* out, size_t cap) const;

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
