// A received frame's layout, read without decoding it: where the header, the path and the payload
// are. The observer carries the raw bytes it was handed and the radio's numbers; what it needs to
// know of a frame is its type, its route, how long its payload is and which bytes are its path -
// all of them in the two header bytes and the path, none of them in the payload. Nothing here
// looks inside the payload, and nothing here writes a byte of it anywhere.
//
// The layout is MeshCore's at the pin, mesh::Packet::readFrom (src/Packet.cpp:65-85 at d9296435)
// and Dispatcher::tryParsePacket (src/Dispatcher.cpp:148-190): a header byte; four bytes of
// transport codes when the route says so; the path_len byte, whose top two bits are the hash size
// minus one and whose low six are the hop count; the path; the payload to the end of the frame.
// The checks are tryParsePacket's: a path mode of 3 and a payload over MAX_PACKET_PAYLOAD are
// refused, and so is a frame too short for the path it claims. The board half only ever sees
// frames MeshCore has already accepted (Dispatcher::checkRecv calls logRx after tryParsePacket),
// so on the board these checks never fail; the host sees whatever a test hands it.
//
// Host-portable: no Arduino, no MeshCore. The three sizes are MeshCore.h's at the pin, repeated
// here so that this file compiles on a host; src/mesh/AgentMesh.h holds them against MeshCore's.

#pragma once

#include <cstddef>
#include <cstdint>

namespace mm {
namespace observer {

constexpr size_t kMaxFrame = 255;     // MeshCore.h MAX_TRANS_UNIT: the most a radio frame holds
constexpr size_t kMaxPath = 64;       // MeshCore.h MAX_PATH_SIZE
constexpr size_t kMaxPayload = 184;   // MeshCore.h MAX_PACKET_PAYLOAD
constexpr size_t kHashSize = 8;       // MeshCore.h MAX_HASH_SIZE: the packet hash is eight bytes

// Packet.h at the pin: the route is the header's low two bits, the type the four above them.
constexpr uint8_t kRouteTransportFlood = 0x00;
constexpr uint8_t kRouteFlood = 0x01;
constexpr uint8_t kRouteDirect = 0x02;
constexpr uint8_t kRouteTransportDirect = 0x03;
constexpr uint8_t kPayloadTypeTrace = 0x09;

struct Frame {
  uint8_t header = 0;
  uint8_t pathLen = 0;               // the encoded byte: (hashSize - 1) << 6 | hopCount
  const uint8_t* path = nullptr;     // hopCount() * hashSize() bytes of the frame
  const uint8_t* payload = nullptr;  // payloadLen bytes of the frame, never read by this code
  uint16_t payloadLen = 0;

  uint8_t routeType() const { return header & 0x03; }
  uint8_t payloadType() const { return (header >> 2) & 0x0F; }
  bool hasTransportCodes() const { return routeType() == kRouteTransportFlood || routeType() == kRouteTransportDirect; }
  bool routeDirect() const { return routeType() == kRouteDirect || routeType() == kRouteTransportDirect; }
  uint8_t hashSize() const { return static_cast<uint8_t>((pathLen >> 6) + 1); }
  uint8_t hopCount() const { return static_cast<uint8_t>(pathLen & 63); }
};

// True when `raw` is a frame MeshCore's tryParsePacket would accept; `out` then points into `raw`.
bool parseFrame(const uint8_t* raw, size_t len, Frame& out);

}  // namespace observer
}  // namespace mm
