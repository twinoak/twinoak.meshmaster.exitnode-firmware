// The mesh, as the agent loop and the job runner see it: the exchanges a job is, and nothing of
// the radio.
//
// `JobRunner` used to hold an `AgentMesh&`, which is a `mesh::Mesh` and therefore a radio, a
// packet pool and an ESP32. This interface is what it actually used - the five sends each built
// of MeshCore's four primitives (createAnonDatagram, createDatagram, createTrace,
// sendDirect/sendFlood), the three callbacks a reply arrives in (a RESPONSE, a TRACE, a TXT_MSG,
// read out of `reply()` after an `await`), the path the mesh holds, and the RTC's unique second -
// so there are two implementations of it and no third:
//
//   src/mesh/AgentMesh.h   the board: MeshCore's mesh in the companion role, on the air
//   src/host/HostMesh.h    the workstation: a fake repeater that answers as stock firmware does,
//                          so the whole agent loop can be run on Linux (docs/host.md)
//
// Nothing here knows which it is. The rails of README.md are the implementation's: the board
// still sends one packet per command and forwards nothing, and the host still never puts a byte
// on any radio because it has none.
//
// Host-portable: no Arduino, no MeshCore. The two constants MeshCore also has are **ours**, with
// its values written down and a static_assert in src/mesh/AgentMesh.cpp that the two agree - so
// a pin bump that moved either one is a red board build and not a quiet buffer overrun on a
// host.

#pragma once

#include <cstdint>

#include "agent/Observations.h"
#include "mesh/ReplyCodec.h"

namespace mm {

// MeshCore's MAX_PACKET_PAYLOAD and MAX_PATH_SIZE at d9296435 (src/MeshCore.h:20, :22). A reply
// cannot be longer than one packet's payload, and a path cannot be longer than one packet's path.
constexpr size_t kMaxReplyDataLen = 184;
constexpr size_t kMaxMeshPathBytes = 64;

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
  enum class Kind : uint8_t { None, Response, Trace, Text };

  Kind kind = Kind::None;
  uint32_t tag = 0;
  bool viaPath = false;  // it came back as a returned path, which also taught us the way out

  // A RESPONSE, after the four-byte tag: the repeater's own reply bytes (mesh/ReplyCodec.h).
  uint8_t data[kMaxReplyDataLen] = {};
  uint8_t len = 0;

  // A TXT_MSG: the CLI reply's text, as CommonCLI wrote it (at most 160 characters), untrimmed.
  char text[kMaxCliReplyLen + 1] = {0};

  float snr = 0;  // what this board measured on the reply

  // A TRACE, as Mesh::onTraceRecv delivers it.
  uint8_t traceFlags = 0;
  uint8_t traceHashes[kMaxMeshPathBytes] = {};
  uint8_t traceHashBytes = 0;
  uint8_t traceSnrs[kMaxMeshPathBytes] = {};
  uint8_t traceSnrCount = 0;
  float traceFinalSnr = 0;
};

// What a send came to. The words are MeshCore's MSG_SEND_* and mean the same thing.
enum class SendResult : uint8_t { Failed, Flood, Direct };

class MeshPort {
 public:
  // The request types of examples/simple_repeater/MyMesh.cpp at d9296435.
  static constexpr uint8_t kReqTypeGetStatus = 0x01;
  static constexpr uint8_t kReqTypeGetTelemetry = 0x03;
  static constexpr uint8_t kReqTypeGetAccessList = 0x05;
  static constexpr uint8_t kReqTypeGetNeighbours = 0x06;
  static constexpr uint8_t kReqTypeGetOwnerInfo = 0x07;

  virtual ~MeshPort() = default;

  // ---------------------------------------------------------------- the mesh itself

  // One turn of the mesh: what the board does with its radio's inbound queue, and what the fake
  // does with the exchange it is holding. Called from every wait.
  virtual void loop() = 0;

  virtual const MeshCounters& counters() const = 0;

  // The heartbeat's `overheard`: frames of the mesh's own chat, counted and never read.
  virtual uint32_t overheard() const = 0;

  // Where a heard advert goes. Until this is set, adverts are counted and dropped.
  virtual void setRing(ObservationRing* ring) = 0;

  virtual void setFrameLog(bool on) = 0;
  virtual bool frameLog() const = 0;

  // What `mesh` prints: the newest first; recentCount() is how many are real.
  virtual const HeardAdvert& recent(int index) const = 0;
  virtual int recentCount() const = 0;

  // ---------------------------------------------------------------- a job's exchanges

  // The repeater this job is for, and the path the server knows to it (the hop hashes as the
  // claim carries them, `hashSize` bytes each). With no path the mesh floods, which is what
  // EXIT-NODES §3 asks for. Returns false when the key is not 64 hex characters.
  virtual bool beginJob(const char* publicKeyHex, const uint8_t* pathHashes, uint8_t hopCount, uint8_t hashSize) = 0;

  // No job in flight: the peer is forgotten and the shared secret is wiped.
  virtual void endJob() = 0;

  // The server knows the repeater as a direct neighbour: a path of zero hops.
  virtual void setDirectPath() = 0;
  // The path is stale: forget it, so the next send floods and the reply teaches a fresh one.
  virtual void forgetPath() = 0;

  virtual bool jobInFlight() const = 0;
  virtual uint8_t pathHopCount() const = 0;
  virtual bool hasPath() const = 0;
  // The out-path as hex hops, for the result's `pathUsed`; returns how many were written.
  virtual int describePath(char hops[][8], int max) const = 0;

  // A login as ourselves (EXIT-NODES §7). With `password` empty or nullptr it is the ACL login;
  // with one it is the guest or admin login.
  //
  // `tag` comes back **0**, and that is not an omission. Every other reply this firmware waits for
  // reflects the four bytes we sent - `MyMesh::handleRequest` at d9296435 calls them "kind of like
  // a 'tag'" (simple_repeater/MyMesh.cpp:214) - but a login reply is prefixed with the
  // **repeater's** own `getCurrentTimeUnique()` (`handleLoginReq`, :129-131). So a login is matched
  // on nothing, which is also how the .NET agent matches `PUSH_CODE_LOGIN_SUCCESS`, and
  // awaitReply(0, ...) is how that is said. The host harness of task p1.1-08 is what found this.
  virtual SendResult sendLogin(const char* password, uint32_t& tag) = 0;

  // REQ_TYPE_* with its request data, on the session the login opened.
  virtual SendResult sendRequest(uint8_t reqType, const uint8_t* data, uint8_t len, uint32_t& tag) = 0;

  // One CLI line as a TXT_MSG datagram of type CLI data, on the admin session. `timestamp` must
  // be strictly above the last one this key sent (MyMesh::onPeerDataRecv at d9296435 answers an
  // equal one with nothing and does not run the command). `sensitive` stars the frame's body in
  // the frame log.
  virtual SendResult sendCli(const char* text, uint32_t timestamp, bool sensitive = false) = 0;

  // The path-discovery request as the companion's CMD_SEND_PATH_DISCOVERY_REQ makes it: a
  // base-telemetry REQ, always flooded, whose reply comes back as a returned path.
  virtual SendResult sendDiscovery(uint32_t& tag) = 0;

  // A trace along the round-trip path, which is the way out, the repeater, and the way back.
  virtual SendResult sendTrace(const uint8_t* roundTripHashes, uint8_t hashBytes, uint8_t hashSize,
                               uint32_t& tag) = 0;

  // Wait for the RESPONSE (or TRACE) with this tag, pumping the mesh (and whatever `pump` is)
  // meanwhile. False on silence. The reply is reply().
  virtual bool awaitReply(uint32_t tag, uint32_t timeoutMs, void (*pump)()) = 0;
  // Wait for a CLI reply: the first TXT_MSG of CLI data from the peer, which carries no tag.
  virtual bool awaitText(uint32_t timeoutMs, void (*pump)()) = 0;
  virtual const MeshReply& reply() const = 0;

  // The unique second: every tag and every sender timestamp this exit node sends comes from it,
  // so the series a repeater sees from this key is strictly increasing across REQs and CLI lines
  // alike, which is what its replay check needs.
  virtual uint32_t uniqueTime() = 0;
};

}  // namespace mm
