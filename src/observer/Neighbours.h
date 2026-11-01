// The neighbour table, the scope sweep and the bytes of its two requests (task p1.8-33): what
// MeshView's *Direct neighbours* section is drawn from, as the observer firmware publishes it -
// agessaman/MeshCore, branch observer-firmware, d664d89df66bdacc1319900a2e2d152fa31b9dc9,
// MQTT_IMPLEMENTATION.md "Neighbors" and examples/simple_repeater/MyMesh.cpp (the line numbers
// below are that file's) - with the exit node's own rules around it: nothing goes on the air
// while a job is open, every request is a transaction on the airtime budget, and the table is a
// fixed array.
//
// What a neighbour is (MyMesh.cpp:783-793, onAdvertRecv): an advert of type repeater heard with
// **zero hops** - a path of no hashes, so the frame came from the repeater's own radio and not
// through another one. Up to kNeighbourCap of them (the observer firmware's cap on a board
// without PSRAM, MQTTBridge.h:720), each with its key, the SNR in quarter dB as the packet
// carries it, the RSSI the radio measured and when it was last heard on the uptime clock; a
// 21st replaces the one heard longest ago (putNeighbour, :86-109). Once per cycle the table is
// refreshed by a **node discovery** (sendNodeDiscoverReq, :1000-1013): one CTL_TYPE_NODE_DISCOVER_REQ
// for repeaters, zero hop, and sixty seconds (:1006) of taking the CTL_TYPE_NODE_DISCOVER_RESP
// frames that come back (onControlDataRecv, :972-991: a repeater's, with this request's tag,
// not our own key).
//
// What a neighbour's scopes are: the answer to an anonymous **regions** request - the login's
// datagram with two other bytes, `{tag}{0x01}{0x00}` (sendAnonRegionsReq, :1959-1977), sent
// **direct on an empty path**, because a repeater answers it only when the request was direct
// (MyMesh.cpp:693 at this branch and :584 at our pin, `ANON_REQ_TYPE_REGIONS && isRouteDirect()`)
// and only four times in three minutes (anon_limiter). The reply is a RESPONSE whose data is
// `{tag}{the repeater's clock}{names}` - the names after the eight-byte prefix, comma-separated,
// of the regions the repeater floods (handleAnonRegionsReq, :203-219: exportNamesTo with
// REGION_DENY_FLOOD). Bounded here to kScopesCap characters, the consumer's own limit
// (Kpa-clawbot/CoreScope, cmd/ingestor/neighbors_guard.go: maxConfiguredScopeLen 256), and cut
// at valid UTF-8 because a JSON string is made of that. A neighbour is `responded`, `timeout` or
// `send_failed` (:2039-2045), and the timeout is neighborDiscoverQueryTimeoutMs (:1997-2001).
//
// The sweep (NeighbourSweep) is one cycle: the discovery window, then one regions request per
// neighbour newest first (:2310-2323: recency, then SNR, then key), one at a time - and the
// exit node's rules: a request waits for the airtime gate the jobs share (agent/Airtime.h) and
// is counted on it; nothing is sent while a job is open or in flight; a claimed job waits for
// at most the one query that is on the air, because a query is synchronous and bounded by the
// timeout. A cycle runs every `mqtt.neighbors.interval` hours while `mqtt.neighbors` is on and a
// broker slot is connected (the report has somewhere to go), or now, on `discover.scopes`.
//
// Host-portable: the mesh is behind NeighbourLink, the clock is a function pointer, and
// test/test_neighbours drives all of it with a fake. Nothing here allocates.

#pragma once

#include <cstddef>
#include <cstdint>

#include <agent/Airtime.h>

namespace mm {
namespace observer {

constexpr int kNeighbourCap = 20;           // MQTTBridge.h:720, NEIGHBORS_MAX_PUBLISH_ENTRIES without PSRAM
constexpr size_t kNeighbourKeyLen = 32;     // a public key
constexpr size_t kScopesCap = 256;          // CoreScope's maxConfiguredScopeLen
constexpr uint8_t kAdvTypeRepeater = 2;     // ADV_TYPE_REPEATER (helpers/AdvertDataHelpers.h at the pin)
constexpr uint32_t kDiscoveryWindowMs = 60000;  // MyMesh.cpp:1006, pending_discover_until
constexpr int16_t kNeighbourNoRssi = INT16_MIN;

// The control payload's type nibbles (MyMesh.cpp:942-943) and the anonymous request's type byte
// (MyMesh.cpp:55 at the pin, ANON_REQ_TYPE_REGIONS).
constexpr uint8_t kCtlNodeDiscoverReq = 0x80;
constexpr uint8_t kCtlNodeDiscoverResp = 0x90;
constexpr uint8_t kAnonReqTypeRegions = 0x01;

constexpr size_t kDiscoveryRequestLen = 10;  // {type}{filter}{tag(4)}{since(4)}
constexpr size_t kRegionsRequestLen = 6;     // {tag(4)}{0x01}{0x00}

struct Neighbour {
  uint8_t key[kNeighbourKeyLen] = {};
  int8_t snrQ4 = 0;                  // SNR x 4, as the packet carries it
  int16_t rssi = kNeighbourNoRssi;   // whole dBm, or kNeighbourNoRssi
  uint32_t heardAtMs = 0;            // the uptime clock
  bool used = false;
};

class NeighbourTable {
 public:
  // The advert rule in one place: a repeater at zero hops enters (or refreshes its entry);
  // anything else does not. True when it entered.
  bool offer(const uint8_t key[kNeighbourKeyLen], uint8_t advType, uint8_t hops, int8_t snrQ4, int16_t rssi, uint32_t nowMs);
  // A discovery response, already known to be a repeater's: enters or refreshes.
  void put(const uint8_t key[kNeighbourKeyLen], int8_t snrQ4, int16_t rssi, uint32_t nowMs);
  void clear();

  int count() const;
  const Neighbour& at(int i) const { return entries_[i]; }  // 0..kNeighbourCap-1, used or not
  const Neighbour* find(const uint8_t key[kNeighbourKeyLen]) const;

 private:
  Neighbour entries_[kNeighbourCap];
};

// The node-discovery request (MyMesh.cpp:1000-1013): type 0x80 with prefix_only 0, the filter
// bit for repeaters, a tag, and `since` 0. Returns kDiscoveryRequestLen.
size_t discoveryRequest(uint32_t tag, uint8_t out[kDiscoveryRequestLen]);
// A discovery response (MyMesh.cpp:972-991): type 0x90 with a repeater's node type in the low
// nibble, the inbound SNR, the request's tag and the full key. True when it is one for `tag`,
// with the key copied out.
bool readDiscoveryResponse(const uint8_t* payload, size_t len, uint32_t tag, uint8_t key[kNeighbourKeyLen]);

// The regions request's plaintext (MyMesh.cpp:1968-1971): the tag, ANON_REQ_TYPE_REGIONS, and a
// zero-length reply path, which asks for a zero-hop reply. Returns kRegionsRequestLen.
size_t regionsRequest(uint32_t tag, uint8_t out[kRegionsRequestLen]);
// The scopes out of a RESPONSE's data **after its tag** (the mesh strips the tag: agent/IMesh.h,
// MeshReply::data): the repeater's clock, then the names. Bounded to kScopesCap, cut at the
// last byte that still makes valid UTF-8 and at the first NUL (the datagram's zero padding).
// Returns the length written; `scopes` is "" for a reply with no names.
size_t readRegionsReply(const uint8_t* data, size_t len, char* scopes, size_t cap);

enum class QueryStatus : uint8_t { Unsent, Responded, Timeout, SendFailed };
const char* queryStatusName(QueryStatus s);  // "responded" | "timeout" | "send_failed"; "" for Unsent

// What the sweep asks of the mesh. The board's is AgentMesh (src/mesh/AgentMesh.h); the test's
// is a fake.
class NeighbourLink {
 public:
  // A job is open or in flight: nothing of the sweep goes on the air.
  virtual bool busy() const = 0;
  // One node-discovery request, zero hop; for `windowMs` the responses go into the table.
  virtual bool sendDiscovery(uint32_t windowMs) = 0;
  // One regions request to `key`, direct on an empty path, and its reply awaited for at most
  // queryTimeoutMs(): synchronous, so that a claimed job waits for this query and nothing more.
  virtual QueryStatus queryRegions(const uint8_t key[kNeighbourKeyLen], char* scopes, size_t cap) = 0;
  virtual uint32_t queryTimeoutMs() const = 0;

 protected:
  ~NeighbourLink() {}
};

// One neighbour of a cycle's snapshot, with what the query found.
struct SweepEntry {
  uint8_t key[kNeighbourKeyLen] = {};
  int8_t snrQ4 = 0;
  uint32_t heardAtMs = 0;
  char scopes[kScopesCap + 1] = {0};
  QueryStatus status = QueryStatus::Unsent;
};

struct SweepSettings {
  bool enabled = true;         // mqtt.neighbors
  uint32_t intervalHours = 24; // mqtt.neighbors.interval, 12..336
};

enum class SweepPhase : uint8_t { Idle, Discovering, Querying };

// How the last cycle's report went, as the Observer records it.
enum class ReportResult : uint8_t { None, Ok, SkippedHeap, Failed, NoSlot };
const char* reportResultName(ReportResult r);  // "none yet" | "ok" | "skipped (heap)" | "failed" | "no slot"

constexpr uint32_t kFirstCycleDelayMs = 5 * 60 * 1000;  // after the sweep is switched on: let the table fill and WiFi settle

class NeighbourSweep {
 public:
  // `gate` is the jobs' airtime gate (JobRunner::gate()): every request of the sweep waits for it
  // and is counted on it; nullptr is no budget at all (tests).
  void begin(NeighbourTable& table, NeighbourLink& link, AirtimeGate* gate, uint32_t (*millis)());
  // mqtt.neighbors and mqtt.neighbors.interval, read again after every `set`. Turning the sweep
  // off cancels a running periodic cycle; a manual one (discover.scopes) runs to its end.
  void configure(const SweepSettings& s);
  // `discover.scopes`: a cycle now, whatever the schedule and the setting say. False when one is
  // already running.
  bool requestNow();

  // Every pass of the main loop. `slotConnected` is whether a periodic cycle has somewhere to
  // send its report; a manual one runs regardless. Returns true when a cycle has just finished
  // and the snapshot below is the report's.
  bool loop(bool slotConnected);

  // The last finished cycle's snapshot, newest first.
  int snapshotCount() const { return snapshotCount_; }
  const SweepEntry& entry(int i) const { return snapshot_[i]; }
  int queriedCount() const { return queried_; }
  int respondedCount() const { return responded_; }
  // What became of its report, recorded by whoever published it.
  void noteReport(ReportResult r);
  ReportResult lastReport() const { return lastReport_; }
  uint32_t lastReportAtMs() const { return lastReportAtMs_; }

  SweepPhase phase() const { return phase_; }
  bool enabled() const { return settings_.enabled; }
  uint32_t cyclesRun() const { return cycles_; }

  // The `nbr:` line of `get mqtt.status`: the schedule, the last report, the table, the responses.
  void describe(char* out, size_t cap) const;

 private:
  void startCycle(bool manual);
  void finishCycle();
  void takeSnapshot();

  NeighbourTable* table_ = nullptr;
  NeighbourLink* link_ = nullptr;
  AirtimeGate* gate_ = nullptr;
  uint32_t (*millis_)() = nullptr;
  SweepSettings settings_;
  SweepPhase phase_ = SweepPhase::Idle;
  bool manual_ = false;
  bool wantNow_ = false;
  bool scheduled_ = false;     // a periodic cycle is on the calendar (nextDueAt_)
  uint32_t nextDueAt_ = 0;     // periodic: when
  uint32_t lastCycleEndAt_ = 0;  // 0 = no cycle has finished yet
  bool discoverySent_ = false; // discovering: the request has gone (or could not be made) and the window is open
  uint32_t windowEndsAt_ = 0;  // discovering: when the window closes
  uint32_t discoveryFailures_ = 0;
  int next_ = 0;               // querying: the entry being queried
  SweepEntry snapshot_[kNeighbourCap];
  int snapshotCount_ = 0;
  int queried_ = 0;
  int responded_ = 0;
  uint32_t cycles_ = 0;
  ReportResult lastReport_ = ReportResult::None;
  uint32_t lastReportAtMs_ = 0;
};

}  // namespace observer
}  // namespace mm
