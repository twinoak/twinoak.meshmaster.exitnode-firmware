// The observer on the board: the dialect of src/observer/ (Dialect.h, Token.h, Presets.h,
// Topics.h, Config.h - all host-portable) wired to this board's settings, identity, clock and
// radio, and since task p1.7-27 the two broker connections (Connection.h) that put it on the air.
// It is the tap AgentMesh hands every received frame to (AgentMesh.h, RxTap): when a slot is
// configured, the frame becomes a packet message in this object's buffer and is handed to every
// slot's queue; the status message is made on demand for the connections and for the will.
//
// The clients are the board's (src/net/MqttClient.h, esp-mqtt), handed in by main.cpp so that
// nothing under src/observer/ names the network: test/test_observer greps for it, and
// test/test_connection drives the same state machine with a fake.
//
// Since task p1.7-28 it is also the agent loop's `IHeartbeatObserver` (agent/AgentLoop.h): the
// heartbeat carries the **state** of each slot - the region, the preset, the broker, the state
// word, since when, published, dropped and the last error - and nothing else of the observer.
// MeshMaster shows that and configures none of it (meshmaster.dk D-46); the library it goes
// through does not know what an observer is.
//
// What it never does: decode a payload, write a packet's bytes to the log or the display, or
// let the private key out of the identity - the token is signed through Signer, whose board
// implementation is mesh::LocalIdentity::sign and nothing else (README.md, the rails; the pile's
// "Privacy, in one place"). The log lines the connections produce - connects, refusals, drops,
// as counts and reasons - go through one callback below.
//
// Board-only: Preferences through SettingsStore, MeshCore's LocalIdentity and the radio's clock.

#pragma once

#include <cstddef>
#include <cstdint>

#include <agent/AgentLoop.h>

#include "mesh/AgentMesh.h"
#include "observer/Config.h"
#include "observer/Connection.h"
#include "observer/Dialect.h"
#include "observer/Neighbours.h"
#include "observer/Token.h"
#include "settings/Store.h"

namespace mm {

// What the board measures for the status message's `stats` (task p1.8-33): the radio's noise
// floor, the dispatcher's air-time counters and the battery, read by main.cpp from the objects
// that have them. Each one with the dialect's own "unknown" sentinel (Dialect.h, StatusFields).
struct BoardStats {
  int noiseFloor = -999;
  int txAirSecs = -1;
  int rxAirSecs = -1;
  int batteryMv = -1;
};

// Signer over the board's own identity: Ed25519 by mesh::LocalIdentity::sign (src/Identity.cpp:151-153
// at d9296435), which is the call the observer firmware's JWTHelper makes.
class IdentitySigner : public observer::Signer {
 public:
  void setIdentity(const mesh::LocalIdentity* id) { id_ = id; }
  bool sign(const uint8_t* msg, size_t len, uint8_t sig[observer::kSignatureSize]) override {
    if (id_ == nullptr) return false;
    id_->sign(sig, msg, static_cast<int>(len));
    return true;
  }

 private:
  const mesh::LocalIdentity* id_ = nullptr;
};

class Observer : public RxTap, public observer::StatusSource, public IHeartbeatObserver {
 public:
  // `publicKeyHex` is the identity as hex (any case: stored uppercase, which is what {device},
  // origin_id and the token want); `model` is the board's name for the status message;
  // `clients` are the two slots' MQTT clients and `env` the board's clocks, heap and roots.
  void begin(SettingsStore& settings, const mesh::LocalIdentity* identity, const char* publicKeyHex, const char* model,
             uint32_t (*clock)(), observer::Client* const clients[observer::kSlots], const observer::Environment& env);
  // Re-read every mqtt.* and mqttN.* key: after a `set` of one, and after `erase confirm`. A
  // slot whose connection would change starts over; the others keep their sessions.
  void reconfigure();

  // The neighbour sweep (task p1.8-33, observer/Neighbours.h): the mesh behind its seam and the
  // jobs' airtime gate, which every request of the sweep waits for and is counted on.
  void beginNeighbours(observer::NeighbourLink& link, AirtimeGate* gate);
  // What the board measures for the status message (BoardStats above); nullptr leaves them out.
  void setBoardStats(void (*fill)(BoardStats& out)) { boardStats_ = fill; }
  // The report's `self.default_scope`: the mesh's flood scope (AgentMesh::floodScope, a string
  // that outlives this object), `*` when unscoped. nullptr writes "*".
  void setDefaultScope(const char* scope) { floodScope_ = scope; }

  // Every pass of the main loop: the connections' events, timers and queues, and the sweep's
  // next step - which may be one regions request and its wait, pumped through `pump` below.
  void loop();
  // WiFi's state, every pass (WifiLink::connected()).
  void setLinkUp(bool up);
  // The MeshMaster uplink's call failed for want of memory (AgentClient::memoryFailures moved):
  // the highest slot gives way for ten minutes.
  void onUplinkMemoryFailure();
  // The uplink made a call (AgentClient::lastCost): the gate reads what it dipped, and a call
  // under the floor takes the highest slot down for ten minutes (task p1.8-31).
  void onUplinkCall(uint32_t freeBefore, uint32_t lowest, uint32_t freeAfter) { link_.onUplinkCall(freeBefore, lowest, freeAfter); }

  bool enabled() const { return enabled_; }
  const observer::ObserverSettings& settings() const { return view_; }
  observer::Signer& signer() { return signer_; }
  const observer::Connections& connections() const { return link_; }

  // RxTap: a received frame, after MeshCore accepted it. Formats it when enabled; counts always.
  void onRxFrame(const RxFrame& frame) override;

  // StatusSource: the status message as of now, into `out`; its length or 0.
  size_t formatStatus(const char* status, char* out, size_t cap) override;

  // IHeartbeatObserver (task p1.7-28): the observer's **state** for the heartbeat - the region
  // and one entry per slot - and false while the observer is off, which leaves the field out of
  // the body altogether. Nothing of what was published goes in it: not a packet, not a topic,
  // not a count per key (meshmaster.dk D-46). Called from the agent loop, with the slots' own
  // strings handed over by pointer; they are this object's and the connections', and the
  // heartbeat is written before the call returns.
  bool describeObserver(HeartbeatObserver& out) override;

  // The last packet message made ("" until one was).
  const char* lastPacketMessage() const { return packet_; }
  size_t lastPacketLength() const { return packetLen_; }

  // One line for `status`: the slots, the code, the origin, and the counts.
  void describe(char* out, size_t cap) const;
  // One line per slot for `mqtt.status` (Connections::describe).
  void describeSlot(int slot, char* out, size_t cap) const { link_.describe(slot, out, cap); }
  // The memory line of `mqtt.status`: the gate's rule with this board's figures (Connections::describeGate).
  void describeGate(char* out, size_t cap) const { link_.describeGate(out, cap); }
  // The `nbr:` line of `mqtt.status` (NeighbourSweep::describe).
  void describeNeighbours(char* out, size_t cap) const { sweep_.describe(out, cap); }
  // `discover.scopes`: a cycle now. False when one is already running (the line says which phase).
  bool discoverScopes() { return sweep_.requestNow(); }
  const observer::NeighbourTable& neighbourTable() const { return table_; }
  observer::NeighbourTable& neighbourTable() { return table_; }  // the mesh's to fill (AgentMesh::setNeighbourTable)
  const observer::NeighbourSweep& sweep() const { return sweep_; }
  // The last report built, for the bench: "" until one was.
  const char* lastNeighboursMessage() const { return neighbours_; }
  size_t lastNeighboursLength() const { return neighboursLen_; }

  // How many slots this board opens (observer/Board.h: one on a V3, two on a V4), and whether
  // slot `slot` is shown at all - the board's own, or one beyond them with a preset stored in
  // it, which is shown off with the reason rather than hidden.
  int slotsOnBoard() const { return link_.board().slots; }
  bool slotShown(int slot) const;

  int slotsConfigured() const { return link_.configuredSlots(); }
  int slotsConnected() const { return link_.connectedSlots(); }

  uint32_t packetsFormatted() const { return formatted_; }
  uint32_t packetsNotFitting() const { return notFitting_; }

 private:
  void readSettings();
  const char* origin() const;
  static void reportLine(void* ctx, LogLevel level, const char* line);
  // A cycle has finished: build the report from the sweep's snapshot and offer it to the slots.
  void reportNeighbours();
  // The hand-over of the last report has ended: record how it went.
  void settleNeighbours();

  SettingsStore* settings_ = nullptr;
  IdentitySigner signer_;
  uint32_t (*clock_)() = nullptr;
  const char* model_ = "";
  char publicKeyHex_[observer::kPublicKeyHexLen + 1] = {0};
  char defaultOrigin_[16] = {0};  // "MeshMaster-" + the first four of the key
  char clientVersion_[96] = {0};  // "meshcore/<firmware>", the token's `client` claim and the status message's client_version

  // The settings, as the store holds them (sizes: Keys.cpp's maxLen + 1).
  char iata_[4] = {0};
  char owner_[65] = {0};
  char email_[64] = {0};
  char origin_[32] = {0};
  struct SlotStore {
    char preset[11];
    char server[64];
    char audience[64];
    char topic[96];
    char username[32];
    char password[64];
  } slot_[observer::kSlots] = {};
  observer::ObserverSettings view_;
  bool enabled_ = false;
  char why_[64] = {0};

  observer::Connections link_;
  // Where describeObserver()'s answer lives: the loop copies nothing, so the array outlives the
  // call as the settings above do.
  HeartbeatObserverSlot hbSlots_[observer::kSlots] = {};

  char packet_[observer::kPacketMessageCap] = {0};
  size_t packetLen_ = 0;
  uint32_t formatted_ = 0;
  uint32_t notFitting_ = 0;
  uint32_t heardWhileOff_ = 0;

  // The neighbours (task p1.8-33): the table the mesh fills, the sweep over it, and the report
  // built once per cycle into a buffer of its own - static, like the queues, so the gate counts
  // nothing for it; the outbox's copy is what heapAllowsReport gates.
  observer::NeighbourTable table_;
  observer::NeighbourSweep sweep_;
  char neighbours_[observer::kNeighboursMessageCap] = {0};
  size_t neighboursLen_ = 0;
  bool inSweep_ = false;         // the sweep's step is running: loop() re-entered from the query's pump skips it
  bool reportOffered_ = false;   // a report is being handed to the slots: settle it when none is pending
  observer::SweepSettings sweepSettings_;  // mqtt.neighbors and mqtt.neighbors.interval, as last read
  const char* floodScope_ = nullptr;       // the mesh's, for self.default_scope
  void (*boardStats_)(BoardStats& out) = nullptr;
};

}  // namespace mm
