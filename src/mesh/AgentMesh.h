// The board's end of the mesh: an identity, a radio in receive, the adverts it hears - and, since
// task 03, the exchanges a job is: since task 04 a login that may carry a password, a CLI line
// and its text reply, and the path-discovery request.
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

#include "agent/Credentials.h"
#include "agent/Observations.h"
#include "mesh/MeshCoreHeaders.h"
#include "mesh/MeshPort.h"
#include "mesh/ReplyCodec.h"

namespace mm {

// MeshCounters, MeshReply and SendResult live in mesh/MeshPort.h since task p1.1-08: they are
// what the interface carries, and the fake repeater of the host harness fills the same structs.

class AgentMesh : public mesh::Mesh, public MeshPort {
 public:
  static constexpr int kRecent = 5;  // what `mesh` prints: the last five, with their names

  AgentMesh(mesh::Radio& radio, mesh::MillisecondClock& ms, mesh::RNG& rng, mesh::RTCClock& rtc,
            mesh::PacketManager& mgr, mesh::MeshTables& tables)
      : mesh::Mesh(radio, ms, rng, rtc, mgr, tables) {}

  // One turn of the mesh. `mesh::Mesh::loop()` is not virtual, so this is the one name the rest
  // of the firmware calls and it forwards; the qualified call is the only way in.
  void loop() override { mesh::Mesh::loop(); }

  // Where a heard advert goes. Until this is set, adverts are counted and dropped.
  void setRing(ObservationRing* ring) override { ring_ = ring; }

  const MeshCounters& counters() const override { return counters_; }

  // The heartbeat's `overheard`: frames of the mesh's own chat, counted and never read.
  uint32_t overheard() const override {
    return counters_.rxByType[PAYLOAD_TYPE_GRP_TXT] + counters_.rxByType[PAYLOAD_TYPE_GRP_DATA] +
           counters_.rxByType[PAYLOAD_TYPE_TXT_MSG];
  }

  void setFrameLog(bool on) override { frameLog_ = on; }
  bool frameLog() const override { return frameLog_; }

  // The newest first, at most kRecent of them; count() is how many are real.
  const HeardAdvert& recent(int index) const override { return recent_[index]; }
  int recentCount() const override { return recentCount_; }

  static const char* payloadTypeName(uint8_t type);

  // ------------------------------------------------------------------ a job's exchanges

  // The repeater this job is for, and the path the server knows to it (the hop hashes as the
  // claim carries them, `hashSize` bytes each). With no path the mesh floods, which is what
  // EXIT-NODES §3 asks for: the server never guesses, and a companion that knows no route finds
  // one. Returns false when the key is not 64 hex characters.
  bool beginJob(const char* publicKeyHex, const uint8_t* pathHashes, uint8_t hopCount, uint8_t hashSize) override;

  // No job in flight: the peer is forgotten, the shared secret is wiped, and a RESPONSE from
  // anybody is released again before it is decrypted.
  void endJob() override;

  // The server knows the repeater as a direct neighbour: a path of zero hops, which is sent
  // DIRECT with an empty path rather than flooded.
  void setDirectPath() override;
  // The path is stale (a direct login along it got nothing back): forget it, so the next send
  // floods and the reply teaches a fresh one.
  void forgetPath() override;

  bool jobInFlight() const override { return peerSet_; }
  uint8_t pathHopCount() const override { return haveOutPath_ ? static_cast<uint8_t>(outPathLen_ & 63) : 0; }
  bool hasPath() const override { return haveOutPath_; }
  // The out-path as hex hops, for the result's `pathUsed`; returns how many were written.
  int describePath(char hops[][8], int max) const override;

  // A login as ourselves (EXIT-NODES §7). With `password` empty or nullptr it is the ACL login,
  // which offers the repeater our own key and no secret at all; with one it is the guest or
  // admin login, and the plaintext that carried the password is wiped as soon as the packet is
  // encrypted (at most kMaxPasswordLen bytes go, as BaseChatMesh::sendLogin sends them). `tag` is
  // set to **0**: a login reply is prefixed with the repeater's own clock and not with ours, so it
  // is matched on nothing (MeshPort.h, and the comment in sendLogin's body).
  SendResult sendLogin(const char* password, uint32_t& tag) override;

  // REQ_TYPE_* with its request data, on the session the login opened.
  SendResult sendRequest(uint8_t reqType, const uint8_t* data, uint8_t len, uint32_t& tag) override;

  // One CLI line as a TXT_MSG datagram of type CLI data, on the admin session: `timestamp` is
  // the sender timestamp the repeater judges replays by, and must be strictly above the last
  // one it saw from this key (MyMesh::onPeerDataRecv at d9296435 answers an equal one with
  // nothing and does not run the command) - uniqueTime() is that series. The reply is a TXT_MSG
  // of the repeater's own and is awaited with awaitText(). `sensitive` is a line made from a
  // password: its frame, and the reply awaited for it, are printed with their bodies as stars.
  SendResult sendCli(const char* text, uint32_t timestamp, bool sensitive = false) override;

  // The path-discovery request as the companion's CMD_SEND_PATH_DISCOVERY_REQ makes it
  // (examples/companion_radio/MyMesh.cpp:1602-1630 at d9296435): a base-telemetry REQ, always
  // flooded, whose reply comes back as a returned path. The last resort of a discover.
  SendResult sendDiscovery(uint32_t& tag) override;

  // A trace along the round-trip path, which is the way out, the repeater, and the way back.
  SendResult sendTrace(const uint8_t* roundTripHashes, uint8_t hashBytes, uint8_t hashSize, uint32_t& tag) override;

  // Wait for the RESPONSE (or TRACE) with this tag, pumping the mesh (and whatever `pump` is)
  // meanwhile. Returns false on silence. The reply is reply().
  bool awaitReply(uint32_t tag, uint32_t timeoutMs, void (*pump)()) override;
  // Wait for a CLI reply: the first TXT_MSG of CLI data from the peer, which carries no tag.
  bool awaitText(uint32_t timeoutMs, void (*pump)()) override;
  const MeshReply& reply() const override { return reply_; }

  // The RTC's unique second: every tag and every sender timestamp this board sends comes from
  // it, so the series a repeater sees from this key is strictly increasing across REQs and CLI
  // lines alike, which is what its replay check needs.
  uint32_t uniqueTime() override { return getRTCClock()->getCurrentTimeUnique(); }

  // The request types of examples/simple_repeater/MyMesh.cpp at d9296435 are MeshPort's, so the
  // fake repeater of the host harness answers the same numbers.

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

  enum class Awaiting : uint8_t { None, Response, Text };

  void noteRssi(const mesh::Packet* pkt, float rssi);
  bool takeRssi(const mesh::Packet* pkt, float& rssi);
  void remember(const HeardAdvert& advert);
  const uint8_t* sharedSecret();
  // `clearBytes` is how much of the payload is printed as hex before the stars: 0 is the whole
  // frame, kLoginPayloadClearBytes a login with a password, kDatagramClearBytes a `password` line.
  SendResult dispatch(mesh::Packet* pkt, size_t clearBytes = 0);
  bool await(Awaiting what, uint32_t tag, uint32_t timeoutMs, void (*pump)());
  void takeResponse(mesh::Packet* pkt, const uint8_t* data, size_t len, bool viaPath);
  void takeText(mesh::Packet* pkt, const uint8_t* data, size_t len);
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
