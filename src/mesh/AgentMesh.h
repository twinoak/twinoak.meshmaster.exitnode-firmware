// The board's end of the mesh, in task 01: an identity, a radio in receive, and nothing sent.
//
// A mesh::Mesh in the companion role - allowPacketForward() is false, so it never repeats - that
// takes every received packet in onRecvPacket(), counts it by type and releases it. Nothing is
// decrypted, nothing is answered, nothing is queued: the base class's ACKs and path returns are
// only ever sent for peers it knows, and this one knows none, and the override below does not
// call the base class at all. Task 02 is where adverts become observations; the counters and
// the frame log are what this task leaves for the bench to read.
//
// `log.frames on` prints one line per received frame - payload type, route, path hops, payload
// length, SNR, RSSI - and never a payload byte (README.md: nothing the mesh says is logged as
// text). Messages of the mesh's own chat are counted under their type and dropped unread.

#pragma once

#include <cstdint>

#include "mesh/MeshCoreHeaders.h"

namespace mm {

struct MeshCounters {
  uint32_t rx = 0;             // every frame that parsed
  uint32_t rxByType[16] = {};  // by PAYLOAD_TYPE_* (0x00..0x0F)
  uint32_t tx = 0;             // what left the radio; 0 in task 01, and `status` shows it
};

class AgentMesh : public mesh::Mesh {
 public:
  AgentMesh(mesh::Radio& radio, mesh::MillisecondClock& ms, mesh::RNG& rng, mesh::RTCClock& rtc,
            mesh::PacketManager& mgr, mesh::MeshTables& tables)
      : mesh::Mesh(radio, ms, rng, rtc, mgr, tables) {}

  const MeshCounters& counters() const { return counters_; }
  void setFrameLog(bool on) { frameLog_ = on; }
  bool frameLog() const { return frameLog_; }

  static const char* payloadTypeName(uint8_t type);

 protected:
  mesh::DispatcherAction onRecvPacket(mesh::Packet* pkt) override;
  bool allowPacketForward(const mesh::Packet*) override { return false; }
  void logTx(mesh::Packet*, int) override { counters_.tx++; }

 private:
  MeshCounters counters_;
  bool frameLog_ = false;
};

}  // namespace mm
