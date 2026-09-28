#include "AgentMesh.h"

#include <Arduino.h>

#include <cstring>

#include "Log.h"
#include "agent/Envelopes.h"

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

void AgentMesh::noteRssi(const mesh::Packet* pkt, float rssi) {
  for (int i = 0; i < kRssiNotes; i++) {
    if (rssiNotes_[i].pkt == pkt) {  // a pooled packet reused: this is the new frame's number
      rssiNotes_[i].rssi = rssi;
      return;
    }
  }
  rssiNotes_[rssiNext_].pkt = pkt;
  rssiNotes_[rssiNext_].rssi = rssi;
  rssiNext_ = (rssiNext_ + 1) % kRssiNotes;
}

bool AgentMesh::takeRssi(const mesh::Packet* pkt, float& rssi) {
  for (int i = 0; i < kRssiNotes; i++) {
    if (rssiNotes_[i].pkt == pkt) {
      rssi = rssiNotes_[i].rssi;
      rssiNotes_[i].pkt = nullptr;
      return true;
    }
  }
  return false;
}

void AgentMesh::logRx(mesh::Packet* pkt, int, float) {
  // The one moment the radio's "last RSSI" still belongs to this packet: a flood frame can wait
  // in the inbound queue for up to a second before onRecvPacket sees it (Dispatcher::checkRecv).
  noteRssi(pkt, _radio->getLastRSSI());
}

void AgentMesh::remember(const HeardAdvert& advert) {
  for (int i = kRecent - 1; i > 0; i--) recent_[i] = recent_[i - 1];
  recent_[0] = advert;
  if (recentCount_ < kRecent) recentCount_++;
}

mesh::DispatcherAction AgentMesh::onRecvPacket(mesh::Packet* pkt) {
  const uint8_t type = pkt->getPayloadType();
  counters_.rx++;
  counters_.rxByType[type & 0x0F]++;

  hasRssi_ = takeRssi(pkt, rssiDbm_);

  if (frameLog_) {
    const char* route = pkt->isRouteFlood() ? "flood" : (pkt->isRouteDirect() ? "direct" : "other");
    LOG_I("frame %s %s hops=%u len=%u snr=%.2f rssi=%.0f", payloadTypeName(type), route,
          static_cast<unsigned>(pkt->getPathHashCount()), static_cast<unsigned>(pkt->payload_len),
          static_cast<double>(pkt->getSNR()), hasRssi_ ? static_cast<double>(rssiDbm_) : 0.0);
  }

  // Everything but an advert is counted and released here, before MeshCore's own dispatch: no
  // payload is decrypted, no shared secret is calculated, and there is no branch left that could
  // compose a reply. An advert goes on to Mesh::onRecvPacket, which checks its signature and its
  // replay timestamp before calling onAdvertRecv().
  if (type != PAYLOAD_TYPE_ADVERT) return ACTION_RELEASE;
  return mesh::Mesh::onRecvPacket(pkt);
}

void AgentMesh::onAdvertRecv(mesh::Packet* pkt, const mesh::Identity& id, uint32_t, const uint8_t* app_data,
                             size_t app_data_len) {
  AdvertDataParser parser(app_data, static_cast<uint8_t>(app_data_len));
  if (!parser.isValid()) {
    counters_.advertsDropped++;
    LOG_D("advert: app_data did not decode (%u bytes); counted and dropped", static_cast<unsigned>(app_data_len));
    return;
  }

  HeardAdvert heard;
  std::memcpy(heard.publicKey, id.pub_key, kPublicKeyLen);
  heard.heardAtMillis = millis();

  heard.pathHashSize = pkt->getPathHashSize();
  heard.hops = pkt->getPathHashCount();
  size_t bytes = pkt->getPathByteLen();
  if (bytes > kMaxObservationPathBytes) bytes = kMaxObservationPathBytes;
  std::memcpy(heard.pathBytes, pkt->path, bytes);
  heard.pathByteLen = static_cast<uint8_t>(bytes);

  heard.snrQ4 = static_cast<int8_t>(pkt->getSNR() * 4.0f);
  heard.rssi = hasRssi_ ? static_cast<int16_t>(rssiDbm_) : kNoRssi;

  heard.advType = parser.getType();
  if (parser.hasLatLon() && (parser.getIntLat() != 0 || parser.getIntLon() != 0)) {
    heard.hasPosition = true;
    heard.latE6 = parser.getIntLat();
    heard.lonE6 = parser.getIntLon();
  }
  if (parser.hasName()) {
    // A name is whatever a stranger put in an advert: bounded, and cut at the last byte that
    // still makes well-formed UTF-8, because a JSON string is made of those.
    const size_t n = validUtf8PrefixLength(parser.getName(), kMaxObservationNameLen);
    std::memcpy(heard.name, parser.getName(), n);
    heard.name[n] = '\0';
  }

  counters_.adverts++;
  remember(heard);
  if (ring_ != nullptr) ring_->push(heard);

  // The key, not the name: `mesh` on the console is where a name is read, once, by a person.
  char key[16];
  mesh::Utils::toHex(key, heard.publicKey, 4);
  const char* role = roleName(heard.advType);
  LOG_D("advert: %s, %s, hops %u, snr %.2f; %u waiting to be posted", key, role != nullptr ? role : "unknown",
        static_cast<unsigned>(heard.hops), static_cast<double>(heard.snrQ4) / 4.0,
        static_cast<unsigned>(ring_ != nullptr ? ring_->size() : 0));
}

}  // namespace mm
