#include "Frame.h"

namespace mm {
namespace observer {

bool parseFrame(const uint8_t* raw, size_t len, Frame& out) {
  if (raw == nullptr || len < 2) return false;
  size_t i = 0;
  out.header = raw[i++];
  // Dispatcher.cpp:152: a payload version above PAYLOAD_VER_1 is refused before anything else.
  if (((out.header >> 6) & 0x03) > 0) return false;
  if (out.hasTransportCodes()) {
    if (len < 6) return false;
    i += 4;  // Packet.cpp:68-70: two uint16 transport codes, not the observer's business
  }
  if (i >= len) return false;
  out.pathLen = raw[i++];
  if ((out.pathLen >> 6) == 3) return false;  // Dispatcher.cpp:165-169: path mode 3 is reserved
  const size_t pathBytes = static_cast<size_t>(out.hopCount()) * out.hashSize();
  if (pathBytes > kMaxPath || i + pathBytes > len) return false;  // Dispatcher.cpp:172-176
  out.path = raw + i;
  i += pathBytes;
  if (i >= len) return false;  // Packet.cpp:80: a frame with no payload is a bad encoding
  const size_t payloadLen = len - i;
  if (payloadLen > kMaxPayload) return false;  // Dispatcher.cpp:180-184
  out.payload = raw + i;
  out.payloadLen = static_cast<uint16_t>(payloadLen);
  return true;
}

}  // namespace observer
}  // namespace mm
