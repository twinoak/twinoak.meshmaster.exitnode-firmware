#include "AgentMesh.h"

#include "Log.h"

namespace mm {

const char* AgentMesh::payloadTypeName(uint8_t type) {
  switch (type) {
    case PAYLOAD_TYPE_REQ: return "req";
    case PAYLOAD_TYPE_RESPONSE: return "response";
    case PAYLOAD_TYPE_TXT_MSG: return "txt";
    case PAYLOAD_TYPE_ACK: return "ack";
    case PAYLOAD_TYPE_ADVERT: return "advert";
    case PAYLOAD_TYPE_GRP_TXT: return "grp-txt";
    case PAYLOAD_TYPE_GRP_DATA: return "grp-data";
    case PAYLOAD_TYPE_ANON_REQ: return "anon-req";
    case PAYLOAD_TYPE_PATH: return "path";
    case PAYLOAD_TYPE_TRACE: return "trace";
    case PAYLOAD_TYPE_MULTIPART: return "multipart";
    case PAYLOAD_TYPE_CONTROL: return "control";
    case PAYLOAD_TYPE_RAW_CUSTOM: return "raw";
    default: return "other";
  }
}

mesh::DispatcherAction AgentMesh::onRecvPacket(mesh::Packet* pkt) {
  const uint8_t type = pkt->getPayloadType();
  counters_.rx++;
  counters_.rxByType[type & 0x0F]++;

  if (frameLog_) {
    const char* route = pkt->isRouteFlood() ? "flood" : (pkt->isRouteDirect() ? "direct" : "other");
    LOG_I("frame %s %s hops=%u len=%u snr=%.1f rssi=%.0f", payloadTypeName(type), route,
          static_cast<unsigned>(pkt->getPathHashCount()), static_cast<unsigned>(pkt->payload_len),
          static_cast<double>(pkt->getSNR()), static_cast<double>(_radio->getLastRSSI()));
  }

  // Counted, and dropped: no decryption, no reply, no forward, in this task.
  return ACTION_RELEASE;
}

}  // namespace mm
