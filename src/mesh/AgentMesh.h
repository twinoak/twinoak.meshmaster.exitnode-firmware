// The board's end of the mesh: an identity, a radio in receive, the adverts it hears - and
// nothing sent, still.
//
// A mesh::Mesh in the companion role. allowPacketForward() is false, so it never repeats and
// every path in MeshCore's Mesh::onRecvPacket that could put a packet on the air is closed:
// the trace retransmit, the direct route, the flood route and the reciprocal path return are
// each behind that one call. `status` shows tx 0 and a test of the bench walk is that it stays 0.
//
// What task 02 adds is the one thing this firmware wants off the air: an **advert**. An advert,
// and only an advert, is handed to mesh::Mesh::onRecvPacket, which checks its signature and its
// replay timestamp and calls onAdvertRecv() below; every other payload type is counted and
// released by the override before the base class sees it, so nothing is ever decrypted, no
// shared secret is ever calculated and no reply can be composed. In onAdvertRecv the public key,
// the path and hops, the SNR and RSSI, and the name, position and role as decoded become one
// entry of the observation ring (agent/Observations.h). Nothing else of the mesh is kept:
// messages of the mesh's own chat are counted under `overheard` and dropped unread (README.md:
// nothing the mesh says is logged as text).
//
// `log.frames on` prints one line per received frame - payload type, route, path hops, payload
// length, SNR, RSSI - and never a payload byte.

#pragma once

#include <cstdint>

#include "agent/Observations.h"
#include "mesh/MeshCoreHeaders.h"

namespace mm {

struct MeshCounters {
  uint32_t rx = 0;             // every frame that parsed
  uint32_t rxByType[16] = {};  // by PAYLOAD_TYPE_* (0x00..0x0F)
  uint32_t tx = 0;             // what left the radio; 0 in phase 1 up to here, and `status` shows it
  uint32_t adverts = 0;        // adverts that verified and became observations
  uint32_t advertsDropped = 0; // adverts whose app_data did not decode
};

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

 protected:
  mesh::DispatcherAction onRecvPacket(mesh::Packet* pkt) override;
  bool allowPacketForward(const mesh::Packet*) override { return false; }
  void onAdvertRecv(mesh::Packet* pkt, const mesh::Identity& id, uint32_t timestamp, const uint8_t* app_data,
                    size_t app_data_len) override;
  void logRx(mesh::Packet* pkt, int len, float score) override;
  void logTx(mesh::Packet*, int) override { counters_.tx++; }

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

  MeshCounters counters_;
  ObservationRing* ring_ = nullptr;
  RssiNote rssiNotes_[kRssiNotes];
  int rssiNext_ = 0;
  float rssiDbm_ = 0;    // the frame being dispatched, from logRx()
  bool hasRssi_ = false;
  HeardAdvert recent_[kRecent];
  int recentCount_ = 0;
  bool frameLog_ = false;
};

}  // namespace mm
