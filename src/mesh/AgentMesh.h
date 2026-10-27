// The board's end of the mesh: an identity, a radio in receive, the adverts it hears - and, since
// task 03, the exchanges a job is: since task 04 a login that may carry a password, a CLI line
// and its text reply, and the path-discovery request. Since task p1.5-15 it is the in-process
// implementation of the library's IMesh (agent/IMesh.h): the runner talks to that surface, and
// this class is the adapter from it onto MeshCore's primitives.
//
// A mesh::Mesh in the companion role, not a BaseChatMesh: the contact table, the channels, the
// message ACKs and the reciprocal path returns of the chat client are all paths that put a packet
// on the air, and this firmware sends **one packet per command and nothing else**. What it needs
// of the chat client is five calls - a login, a request, a CLI text message, a trace and the
// flooded telemetry request that is path discovery - and each of them is three lines of
// MeshCore's own primitives (createAnonDatagram, createDatagram, createTrace,
// sendDirect/sendFlood), which is what BaseChatMesh's own sendLogin/sendRequest/sendMessage are
// made of. allowPacketForward() is still false, so every routing path in Mesh::onRecvPacket is
// closed; `status` shows what was sent, and it is the count of transactions.
//
// **What reaches MeshCore's own dispatch.** An advert, always: it is checked for signature and
// replay and becomes an observation (agent/Observations.h). A RESPONSE, a PATH, a TRACE or a
// TXT_MSG **only while a job is waiting for its reply** - and then only from the one peer the
// job is for (searchPeersByHash answers for that key alone), so outside that window they are
// released before anything is decrypted and no ECDH is ever calculated for a stranger.
// Everything else - a REQ, an ANON_REQ, the mesh's own chat - is counted and released at all
// times, so there is no branch left that could compose a reply. A TXT_MSG from the peer while a
// CLI reply is awaited is that reply (a repeater sends no chat); one at any other time is
// released with the rest.
//
// `log.frames on` prints one line per frame with its header, and the frame as hex for the frames
// of a job - never an advert's bytes (a stranger's name is in them) and never the mesh's own
// chat (README.md: nothing the mesh says is logged as text). A login that carries a password is
// printed with its encrypted body replaced by one `*` per byte (agent/Credentials.h), which is
// the .NET agent's redaction of CMD_SEND_LOGIN, and a `password` line and the reply that echoes
// it (task 05) are printed the same way, hashes and then stars: nothing made from a password
// reaches the port.

#pragma once

#include <cstdint>

#include <agent/Credentials.h>
#include <agent/IMesh.h>
#include <agent/Observations.h>
#include <agent/ReplyCodec.h>

#include "mesh/MeshCoreHeaders.h"

namespace mm {

// The library's reply is sized by its own constants (agent/IMesh.h) so that nothing of MeshCore
// is included there; this is where MeshCore's are held against them. A bump that grows either of
// MeshCore's is a build error here, not an overrun in takeResponse() or onTraceRecv().
static_assert(kMeshReplyDataCap >= MAX_PACKET_PAYLOAD, "agent/IMesh.h's kMeshReplyDataCap is smaller than MeshCore's MAX_PACKET_PAYLOAD");
static_assert(kMeshPathCap >= MAX_PATH_SIZE, "agent/IMesh.h's kMeshPathCap is smaller than MeshCore's MAX_PATH_SIZE");

// A received frame as the observer (src/observer/, task p1.7-26) takes it: the bytes the radio
// received and its numbers for them, and the packet hash MeshCore computed. Handed over at
// logRx, which Dispatcher::checkRecv (src/Dispatcher.cpp:191-238 at d9296435) calls right after
// logRxRaw for the same frame, once tryParsePacket has accepted it - so the raw bytes stashed
// at logRxRaw and the packet at logRx are the same frame, and a frame the dispatcher refused
// is never handed over. Nothing in it is decoded by anyone: the tap carries bytes.
struct RxFrame {
  const uint8_t* raw = nullptr;  // the whole frame, MAX_TRANS_UNIT bytes at most
  int len = 0;
  float snr = 0;
  float rssi = 0;
  float score = 0;               // the dispatcher's rebroadcast score for it
  uint8_t hash[MAX_HASH_SIZE] = {0};  // mesh::Packet::calculatePacketHash
};

class RxTap {
 public:
  virtual void onRxFrame(const RxFrame& frame) = 0;

 protected:
  ~RxTap() {}
};

class AgentMesh : public mesh::Mesh, public IMesh {
 public:
  static constexpr int kRecent = 5;  // what `mesh` prints: the last five, with their names

  AgentMesh(mesh::Radio& radio, mesh::MillisecondClock& ms, mesh::RNG& rng, mesh::RTCClock& rtc,
            mesh::PacketManager& mgr, mesh::MeshTables& tables)
      : mesh::Mesh(radio, ms, rng, rtc, mgr, tables) {}

  // ------------------------------------------------------------------ IMesh

  void setRing(ObservationRing* ring) override { ring_ = ring; }
  const MeshCounters& counters() const override { return counters_; }
  uint32_t overheard() const override {
    return counters_.rxByType[PAYLOAD_TYPE_GRP_TXT] + counters_.rxByType[PAYLOAD_TYPE_GRP_DATA] +
           counters_.rxByType[PAYLOAD_TYPE_TXT_MSG];
  }
  // MeshCore's own pump, under the interface's name; Mesh::loop() is not virtual, so this hides it
  // for a caller that holds an AgentMesh and forwards to it.
  void loop() override { mesh::Mesh::loop(); }

  bool beginJob(const char* publicKeyHex, const uint8_t* pathHashes, uint8_t hopCount, uint8_t hashSize) override;
  void endJob() override;
  void setDirectPath() override;
  void forgetPath() override;
  bool hasPath() const override { return haveOutPath_; }
  uint8_t pathHopCount() const override { return haveOutPath_ ? static_cast<uint8_t>(outPathLen_ & 63) : 0; }
  int describePath(char hops[][8], int max) const override;

  // The login reply carries no tag of ours (`tag` is left 0: the next RESPONSE from the peer);
  // the timestamp in the packet is what the repeater judges replays by and nothing else.
  SendResult sendLogin(const char* password, uint32_t& tag) override;
  SendResult sendRequest(uint8_t reqType, const uint8_t* data, uint8_t len, uint32_t& tag) override;
  // The sender timestamp is drawn here, from uniqueTime(), and handed back: the repeater runs
  // the line when it is above the last it saw from this key and answers an equal one with
  // nothing (MyMesh::onPeerDataRecv at d9296435).
  SendResult sendCli(const char* text, bool sensitive, uint32_t& timestamp) override;
  // The path-discovery request as the companion's CMD_SEND_PATH_DISCOVERY_REQ makes it
  // (examples/companion_radio/MyMesh.cpp:1602-1630 at d9296435).
  SendResult sendDiscovery(uint32_t& tag) override;
  SendResult sendTrace(const uint8_t* roundTripHashes, uint8_t hashBytes, uint8_t hashSize, uint32_t& tag) override;

  bool awaitReply(uint32_t tag, uint32_t timeoutMs, void (*pump)()) override;
  bool awaitText(uint32_t timeoutMs, void (*pump)()) override;
  const MeshReply& reply() const override { return reply_; }

  // ------------------------------------------------------------------ the board's own

  // The RTC's unique second: every tag and every sender timestamp this board sends comes from
  // it, so the series a repeater sees from this key is strictly increasing across REQs and CLI
  // lines alike, which is what its replay check needs. **Not on `IMesh` since task p1.5-17**: the
  // mesh draws it at every send and hands it back, so nothing of the library asks for one, and
  // the link has no request for it either (agent/IMesh.h says so where the method used to be).
  uint32_t uniqueTime() { return getRTCClock()->getCurrentTimeUnique(); }

  bool jobInFlight() const { return peerSet_; }

  void setFrameLog(bool on) { frameLog_ = on; }
  bool frameLog() const { return frameLog_; }

  // The observer's tap (RxTap above): every frame the dispatcher accepts, as bytes. nullptr is
  // no tap, and then logRxRaw stashes nothing.
  void setRxTap(RxTap* tap) { tap_ = tap; }

  // The newest first, at most kRecent of them; count() is how many are real.
  const HeardAdvert& recent(int index) const { return recent_[index]; }
  int recentCount() const { return recentCount_; }

  static const char* payloadTypeName(uint8_t type);

  // The path-hash size of every flood this board originates: two bytes per hop, never one.
  // MeshCore's sendFlood() defaults to one (MeshCore.h PATH_HASH_SIZE, Mesh.h), and a path of
  // one-byte hashes is what the first bench returned - "EF → 38 → 82 → 49 → 01" - where two
  // repeaters sharing a first byte would both repeat a direct packet. Repeaters append their hash
  // at the size the originator chose (Mesh.cpp, the retransmit path), so this is the whole
  // decision for a request's outbound path; the reply comes back along it. meshguide's standard
  // asks repeaters for path.hash.mode >= 1 (two bytes) for their own floods, and this is the same
  // choice made for ours. Thomas, 3 October 2026: "force it to always use at least 2 bytes".
  static constexpr uint8_t kFloodPathHashSize = 2;

 protected:
  mesh::DispatcherAction onRecvPacket(mesh::Packet* pkt) override;
  bool allowPacketForward(const mesh::Packet*) override { return false; }
  void onAdvertRecv(mesh::Packet* pkt, const mesh::Identity& id, uint32_t timestamp, const uint8_t* app_data,
                    size_t app_data_len) override;
  void logRxRaw(float snr, float rssi, const uint8_t raw[], int len) override;
  void logRx(mesh::Packet* pkt, int len, float score) override;
  void logTx(mesh::Packet*, int) override { counters_.tx++; }

  int searchPeersByHash(const uint8_t* hash) override;
  void getPeerSharedSecret(uint8_t* dest_secret, int peer_idx) override;
  void onPeerDataRecv(mesh::Packet* pkt, uint8_t type, int sender_idx, const uint8_t* secret, uint8_t* data,
                      size_t len) override;
  bool onPeerPathRecv(mesh::Packet* pkt, int sender_idx, const uint8_t* secret, uint8_t* path, uint8_t path_len,
                      uint8_t extra_type, uint8_t* extra, uint8_t extra_len) override;
  void onTraceRecv(mesh::Packet* pkt, uint32_t tag, uint32_t auth_code, uint8_t flags, const uint8_t* path_snrs,
                   const uint8_t* path_hashes, uint8_t path_len) override;

 private:
  // A flood packet can wait in the inbound queue for up to a second before onRecvPacket sees it,
  // and the radio's "last RSSI" has moved on by then. logRx() runs while it is still the
  // packet's, so the number is noted there against the packet and taken back here.
  struct RssiNote {
    const mesh::Packet* pkt = nullptr;
    float rssi = 0;
  };
  static constexpr int kRssiNotes = 16;  // the packet pool's size (src/main.cpp)

  enum class Awaiting : uint8_t { None, Response, Text };

  void noteRssi(const mesh::Packet* pkt, float rssi);
  bool takeRssi(const mesh::Packet* pkt, float& rssi);
  void remember(const HeardAdvert& advert);
  const uint8_t* sharedSecret();
  // `clearBytes` is how much of the payload is printed as hex before the stars: 0 is the whole
  // frame, kLoginPayloadClearBytes a login with a password, kDatagramClearBytes a `password` line.
  SendResult dispatch(mesh::Packet* pkt, size_t clearBytes = 0);
  bool await(Awaiting what, uint32_t tag, uint32_t timeoutMs, void (*pump)());
  void takeResponse(const uint8_t* data, size_t len);
  void takeText(const uint8_t* data, size_t len);
  void logFrame(const char* direction, const mesh::Packet* pkt, bool withHex, size_t clearBytes = 0);

  MeshCounters counters_;
  ObservationRing* ring_ = nullptr;
  RssiNote rssiNotes_[kRssiNotes];
  int rssiNext_ = 0;
  float rssiDbm_ = 0;    // the frame being dispatched, from logRx()
  bool hasRssi_ = false;
  HeardAdvert recent_[kRecent];
  int recentCount_ = 0;
  bool frameLog_ = false;

  // The observer's tap and the frame stashed for it between logRxRaw and logRx (RxFrame above).
  RxTap* tap_ = nullptr;
  uint8_t rawStash_[MAX_TRANS_UNIT] = {0};
  int rawStashLen_ = 0;
  float rawStashSnr_ = 0;
  float rawStashRssi_ = 0;
  bool rawStashed_ = false;

  mesh::Identity peer_;
  bool peerSet_ = false;
  uint8_t peerSecret_[PUB_KEY_SIZE] = {};
  bool haveSecret_ = false;
  uint8_t outPath_[MAX_PATH_SIZE] = {};
  uint8_t outPathLen_ = 0;  // the encoded path_len byte: (hashSize - 1) << 6 | hopCount
  bool haveOutPath_ = false;

  MeshReply reply_;
  uint32_t waitingFor_ = 0;
  bool waiting_ = false;
  Awaiting awaiting_ = Awaiting::None;
  bool sensitiveAwait_ = false;  // the text awaited answers a `password` line: its frame is starred
};

}  // namespace mm
