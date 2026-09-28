// The board's end of the mesh: an identity, a radio in receive, the adverts it hears - and, since
// task 03, the four exchanges a read job is.
//
// A mesh::Mesh in the companion role, not a BaseChatMesh: the contact table, the channels, the
// message ACKs and the reciprocal path returns of the chat client are all paths that put a packet
// on the air, and this firmware sends **one packet per command and nothing else**. What it needs
// of the chat client is four calls - a login, a status request, a binary request and a trace -
// and each of them is three lines of MeshCore's own primitives (createAnonDatagram,
// createDatagram, createTrace, sendDirect/sendFlood), which is what BaseChatMesh's own
// sendLogin/sendRequest are made of. allowPacketForward() is still false, so every routing path
// in Mesh::onRecvPacket is closed; `status` shows what was sent, and it is the count of jobs.
//
// **What reaches MeshCore's own dispatch.** An advert, always: it is checked for signature and
// replay and becomes an observation (agent/Observations.h). A RESPONSE, a PATH or a TRACE
// **only while a job is in flight** - they are the reply this board is waiting for, and outside
// that window they are released before anything is decrypted, so no ECDH is ever calculated for
// a stranger. Everything else - a REQ, a text message, an ANON_REQ, the mesh's own chat - is
// counted and released at all times, so there is no branch left that could compose a reply.
//
// `log.frames on` prints one line per frame with its header, and the frame as hex for the frames
// of a job - never an advert's bytes (a stranger's name is in them) and never the mesh's own
// chat (README.md: nothing the mesh says is logged as text).

#pragma once

#include <cstdint>

#include "agent/Observations.h"
#include "mesh/MeshCoreHeaders.h"

namespace mm {

struct MeshCounters {
  uint32_t rx = 0;             // every frame that parsed
  uint32_t rxByType[16] = {};  // by PAYLOAD_TYPE_* (0x00..0x0F)
  uint32_t tx = 0;             // what left the radio: one per transaction, and nothing else
  uint32_t adverts = 0;        // adverts that verified and became observations
  uint32_t advertsDropped = 0; // adverts whose app_data did not decode
};

// What came back from a repeater during a job. One instance, reused: a board runs one
// transaction at a time, by the budget of EXIT-NODES §8.
struct MeshReply {
  enum class Kind : uint8_t { None, Response, Trace };

  Kind kind = Kind::None;
  uint32_t tag = 0;
  bool viaPath = false;  // it came back as a returned path, which also taught us the way out

  // A RESPONSE, after the four-byte tag: the repeater's own reply bytes (mesh/ReplyCodec.h).
  uint8_t data[MAX_PACKET_PAYLOAD] = {};
  uint8_t len = 0;

  float snr = 0;  // what this board measured on the reply

  // A TRACE, as Mesh::onTraceRecv delivers it.
  uint8_t traceFlags = 0;
  uint8_t traceHashes[MAX_PATH_SIZE] = {};
  uint8_t traceHashBytes = 0;
  uint8_t traceSnrs[MAX_PATH_SIZE] = {};
  uint8_t traceSnrCount = 0;
  float traceFinalSnr = 0;
};

// What a send came to. The words are MeshCore's MSG_SEND_* and mean the same thing.
enum class SendResult : uint8_t { Failed, Flood, Direct };

class AgentMesh : public mesh::Mesh {
 public:
  static constexpr int kRecent = 5;  // what `mesh` prints: the last five, with their names

  AgentMesh(mesh::Radio& radio, mesh::MillisecondClock& ms, mesh::RNG& rng, mesh::RTCClock& rtc,
            mesh::PacketManager& mgr, mesh::MeshTables& tables)
      : mesh::Mesh(radio, ms, rng, rtc, mgr, tables) {}

  // Where a heard advert goes. Until this is set, adverts are counted and dropped.
  void setRing(ObservationRing* ring) { ring_ = ring; }

  const MeshCounters& counters() const { return counters_; }

  // The heartbeat's `overheard`: frames of the mesh's own chat, counted and never read.
  uint32_t overheard() const {
    return counters_.rxByType[PAYLOAD_TYPE_GRP_TXT] + counters_.rxByType[PAYLOAD_TYPE_GRP_DATA] +
           counters_.rxByType[PAYLOAD_TYPE_TXT_MSG];
  }

  void setFrameLog(bool on) { frameLog_ = on; }
  bool frameLog() const { return frameLog_; }

  // The newest first, at most kRecent of them; count() is how many are real.
  const HeardAdvert& recent(int index) const { return recent_[index]; }
  int recentCount() const { return recentCount_; }

  static const char* payloadTypeName(uint8_t type);

  // ------------------------------------------------------------------ a job's exchanges

  // The repeater this job is for, and the path the server knows to it (the hop hashes as the
  // claim carries them, `hashSize` bytes each). With no path the mesh floods, which is what
  // EXIT-NODES §3 asks for: the server never guesses, and a companion that knows no route finds
  // one. Returns false when the key is not 64 hex characters.
  bool beginJob(const char* publicKeyHex, const uint8_t* pathHashes, uint8_t hopCount, uint8_t hashSize);

  // No job in flight: the peer is forgotten, the shared secret is wiped, and a RESPONSE from
  // anybody is released again before it is decrypted.
  void endJob();

  bool jobInFlight() const { return peerSet_; }
  uint8_t pathHopCount() const { return haveOutPath_ ? static_cast<uint8_t>(outPathLen_ & 63) : 0; }
  bool hasPath() const { return haveOutPath_; }
  // The out-path as hex hops, for the result's `pathUsed`; returns how many were written.
  int describePath(char hops[][8], int max) const;

  // A blank-password login as ourselves: the ACL login of EXIT-NODES §7, which offers the
  // repeater our own key and no secret at all. `tag` is what the reply is matched by.
  SendResult sendAclLogin(uint32_t& tag);

  // REQ_TYPE_* with its request data, on the session the login opened.
  SendResult sendRequest(uint8_t reqType, const uint8_t* data, uint8_t len, uint32_t& tag);

  // A trace along the round-trip path, which is the way out, the repeater, and the way back.
  SendResult sendTrace(const uint8_t* roundTripHashes, uint8_t hashBytes, uint8_t hashSize, uint32_t& tag);

  // Wait for the reply with this tag, pumping the mesh (and whatever `pump` is) meanwhile.
  // Returns false on silence. The reply is reply().
  bool awaitReply(uint32_t tag, uint32_t timeoutMs, void (*pump)());
  const MeshReply& reply() const { return reply_; }

  // The four request types of examples/simple_repeater/MyMesh.cpp at d9296435.
  static constexpr uint8_t kReqTypeGetStatus = 0x01;
  static constexpr uint8_t kReqTypeGetTelemetry = 0x03;
  static constexpr uint8_t kReqTypeGetNeighbours = 0x06;
  static constexpr uint8_t kReqTypeGetOwnerInfo = 0x07;

 protected:
  mesh::DispatcherAction onRecvPacket(mesh::Packet* pkt) override;
  bool allowPacketForward(const mesh::Packet*) override { return false; }
  void onAdvertRecv(mesh::Packet* pkt, const mesh::Identity& id, uint32_t timestamp, const uint8_t* app_data,
                    size_t app_data_len) override;
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

  void noteRssi(const mesh::Packet* pkt, float rssi);
  bool takeRssi(const mesh::Packet* pkt, float& rssi);
  void remember(const HeardAdvert& advert);
  const uint8_t* sharedSecret();
  SendResult dispatch(mesh::Packet* pkt);
  void takeResponse(mesh::Packet* pkt, const uint8_t* data, size_t len, bool viaPath);
  void logFrame(const char* direction, const mesh::Packet* pkt, bool withHex);

  MeshCounters counters_;
  ObservationRing* ring_ = nullptr;
  RssiNote rssiNotes_[kRssiNotes];
  int rssiNext_ = 0;
  float rssiDbm_ = 0;    // the frame being dispatched, from logRx()
  bool hasRssi_ = false;
  HeardAdvert recent_[kRecent];
  int recentCount_ = 0;
  bool frameLog_ = false;

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
};

}  // namespace mm
