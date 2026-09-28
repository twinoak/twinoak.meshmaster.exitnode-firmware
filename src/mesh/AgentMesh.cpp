#include "AgentMesh.h"

#include <Arduino.h>

#include <cstdio>
#include <cstring>

#include "Log.h"
#include "agent/Envelopes.h"

namespace mm {

namespace {

bool elapsed(uint32_t now, uint32_t deadline) { return static_cast<int32_t>(now - deadline) >= 0; }

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

const char kHex[] = "0123456789abcdef";

// A frame as hex for the console, bounded: a packet is at most 255 bytes on the wire and this
// prints what fits in one line's worth of buffer.
void hexLine(const uint8_t* bytes, size_t len, char* out, size_t cap) {
  size_t n = 0;
  for (size_t i = 0; i < len && n + 3 < cap; i++) {
    out[n++] = kHex[bytes[i] >> 4];
    out[n++] = kHex[bytes[i] & 0x0F];
  }
  out[n] = '\0';
}

}  // namespace

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

void AgentMesh::logFrame(const char* direction, const mesh::Packet* pkt, bool withHex) {
  if (!frameLog_) return;
  const uint8_t type = pkt->getPayloadType();
  const char* route = pkt->isRouteFlood() ? "flood" : (pkt->isRouteDirect() ? "direct" : "other");
  if (!withHex) {
    LOG_I("frame %s %s %s hops=%u len=%u snr=%.2f", direction, payloadTypeName(type), route,
          static_cast<unsigned>(pkt->getPathHashCount()), static_cast<unsigned>(pkt->payload_len),
          static_cast<double>(pkt->getSNR()));
    return;
  }
  // The frame as hex, both directions, for the raw-exchange log - the board's version of the
  // .NET agent's --log-frames. What is never printed: an advert's bytes (a stranger's name is
  // in them, and `mesh` is where a person reads one) and the mesh's own chat. A login frame
  // carries no password on this firmware and would be ciphertext if it did.
  char payload[2 * MAX_PACKET_PAYLOAD + 1];
  char path[2 * MAX_PATH_SIZE + 1];
  hexLine(pkt->payload, pkt->payload_len, payload, sizeof payload);
  hexLine(pkt->path, pkt->getPathByteLen(), path, sizeof path);
  LOG_I("frame %s %s %s hdr=%02x path=%s payload=%s", direction, payloadTypeName(type), route,
        static_cast<unsigned>(pkt->header), path[0] != '\0' ? path : "-", payload);
}

void AgentMesh::remember(const HeardAdvert& advert) {
  for (int i = kRecent - 1; i > 0; i--) recent_[i] = recent_[i - 1];
  recent_[0] = advert;
  if (recentCount_ < kRecent) recentCount_++;
}

void AgentMesh::logRx(mesh::Packet* pkt, int, float) {
  // The one moment the radio's "last RSSI" still belongs to this packet: a flood frame can wait
  // in the inbound queue for up to a second before onRecvPacket sees it (Dispatcher::checkRecv).
  noteRssi(pkt, _radio->getLastRSSI());
}

mesh::DispatcherAction AgentMesh::onRecvPacket(mesh::Packet* pkt) {
  const uint8_t type = pkt->getPayloadType();
  counters_.rx++;
  counters_.rxByType[type & 0x0F]++;

  hasRssi_ = takeRssi(pkt, rssiDbm_);

  // An advert's bytes carry a name somebody else chose; the chat's bytes are other people's
  // conversation. Neither is printed as hex, whatever `log.frames` says.
  const bool jobFrame = type == PAYLOAD_TYPE_RESPONSE || type == PAYLOAD_TYPE_PATH || type == PAYLOAD_TYPE_TRACE;
  logFrame("rx", pkt, jobFrame && waiting_);

  // Everything but an advert - and, while a job is waiting for its reply, a RESPONSE, a PATH or
  // a TRACE - is counted and released here, before MeshCore's own dispatch: no payload is
  // decrypted, no shared secret is calculated, and there is no branch left that could compose a
  // reply. What does go on to Mesh::onRecvPacket gets its signature check, its replay check and
  // its de-duplication for free.
  if (type == PAYLOAD_TYPE_ADVERT) return mesh::Mesh::onRecvPacket(pkt);
  if (jobFrame && waiting_ && peerSet_) return mesh::Mesh::onRecvPacket(pkt);
  return ACTION_RELEASE;
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

// ------------------------------------------------------------------ a job's exchanges

bool AgentMesh::beginJob(const char* publicKeyHex, const uint8_t* pathHashes, uint8_t hopCount, uint8_t hashSize) {
  endJob();
  if (publicKeyHex == nullptr) return false;
  uint8_t key[PUB_KEY_SIZE];
  for (int i = 0; i < PUB_KEY_SIZE; i++) {
    const int hi = hexValue(publicKeyHex[i * 2]);
    const int lo = hexValue(publicKeyHex[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    key[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  peer_ = mesh::Identity(key);
  peerSet_ = true;

  if (pathHashes != nullptr && hopCount > 0 && hashSize >= 1 && hashSize <= 3 &&
      static_cast<size_t>(hopCount) * hashSize <= MAX_PATH_SIZE && hopCount <= 63) {
    std::memcpy(outPath_, pathHashes, static_cast<size_t>(hopCount) * hashSize);
    outPathLen_ = static_cast<uint8_t>(((hashSize - 1) << 6) | hopCount);
    haveOutPath_ = true;
  }
  return true;
}

void AgentMesh::endJob() {
  peerSet_ = false;
  haveOutPath_ = false;
  outPathLen_ = 0;
  waiting_ = false;
  waitingFor_ = 0;
  reply_ = MeshReply();
  // The ECDH secret is the one thing here that is worth wiping rather than dropping.
  volatile uint8_t* p = peerSecret_;
  for (size_t i = 0; i < sizeof peerSecret_; i++) p[i] = 0;
  haveSecret_ = false;
}

int AgentMesh::describePath(char hops[][8], int max) const {
  if (!haveOutPath_) return 0;
  const int size = (outPathLen_ >> 6) + 1;
  int count = outPathLen_ & 63;
  if (count > max) count = max;
  for (int i = 0; i < count; i++) {
    mesh::Utils::toHex(hops[i], &outPath_[i * size], static_cast<uint8_t>(size));
  }
  return count;
}

const uint8_t* AgentMesh::sharedSecret() {
  if (!haveSecret_) {
    self_id.calcSharedSecret(peerSecret_, peer_);
    haveSecret_ = true;
  }
  return peerSecret_;
}

SendResult AgentMesh::dispatch(mesh::Packet* pkt) {
  if (pkt == nullptr) return SendResult::Failed;
  logFrame("tx", pkt, true);
  if (haveOutPath_) {
    sendDirect(pkt, outPath_, outPathLen_);
    return SendResult::Direct;
  }
  sendFlood(pkt);
  return SendResult::Flood;
}

SendResult AgentMesh::sendAclLogin(uint32_t& tag) {
  if (!peerSet_) return SendResult::Failed;
  uint8_t temp[4];
  tag = getRTCClock()->getCurrentTimeUnique();
  std::memcpy(temp, &tag, 4);
  // A blank password, which is the ACL login: the four bytes of the tag and nothing after them.
  // `MyMesh::handleLoginReq` reads data[0] of what follows the timestamp, and the datagram's
  // plaintext is zero-padded to a cipher block, so the empty string arrives as the empty string
  // and the repeater answers from its ACL alone (simple_repeater/MyMesh.cpp:90-99 at d9296435).
  return dispatch(createAnonDatagram(PAYLOAD_TYPE_ANON_REQ, self_id, peer_, sharedSecret(), temp, sizeof temp));
}

SendResult AgentMesh::sendRequest(uint8_t reqType, const uint8_t* data, uint8_t len, uint32_t& tag) {
  if (!peerSet_ || len + 5u > MAX_PACKET_PAYLOAD - 16u) return SendResult::Failed;
  uint8_t temp[MAX_PACKET_PAYLOAD];
  tag = getRTCClock()->getCurrentTimeUnique();
  std::memcpy(temp, &tag, 4);  // the repeater reflects it back as the reply's own tag
  temp[4] = reqType;
  if (len > 0 && data != nullptr) std::memcpy(&temp[5], data, len);
  return dispatch(createDatagram(PAYLOAD_TYPE_REQ, peer_, sharedSecret(), temp, 5u + len));
}

SendResult AgentMesh::sendTrace(const uint8_t* roundTripHashes, uint8_t hashBytes, uint8_t hashSize, uint32_t& tag) {
  if (roundTripHashes == nullptr || hashBytes == 0) return SendResult::Failed;
  uint8_t flags = 0;
  switch (hashSize) {
    case 1: flags = 0; break;
    case 2: flags = 1; break;
    case 4: flags = 2; break;
    case 8: flags = 3; break;
    default: return SendResult::Failed;  // a three-byte hash has no encoding in a trace packet
  }
  tag = getRTCClock()->getCurrentTimeUnique();
  uint32_t auth = 0;
  getRNG()->random(reinterpret_cast<uint8_t*>(&auth), 4);
  mesh::Packet* pkt = createTrace(tag, auth, flags);
  if (pkt == nullptr) return SendResult::Failed;
  logFrame("tx", pkt, true);
  // A trace is always DIRECT: the path travels in the payload and every hop appends its SNR.
  sendDirect(pkt, roundTripHashes, hashBytes);
  return SendResult::Direct;
}

bool AgentMesh::awaitReply(uint32_t tag, uint32_t timeoutMs, void (*pump)()) {
  reply_ = MeshReply();
  waitingFor_ = tag;
  waiting_ = true;
  const uint32_t deadline = millis() + timeoutMs;
  while (!elapsed(millis(), deadline)) {
    loop();
    if (pump != nullptr) pump();
    if (reply_.kind != MeshReply::Kind::None) {
      waiting_ = false;
      return true;
    }
    delay(1);
  }
  waiting_ = false;
  return false;
}

int AgentMesh::searchPeersByHash(const uint8_t* hash) {
  // One peer, ever: the repeater of the job in flight. Nobody else's packet is decrypted.
  if (!peerSet_ || !waiting_) return 0;
  return peer_.isHashMatch(hash) ? 1 : 0;
}

void AgentMesh::getPeerSharedSecret(uint8_t* dest_secret, int) { std::memcpy(dest_secret, sharedSecret(), PUB_KEY_SIZE); }

void AgentMesh::takeResponse(mesh::Packet* pkt, const uint8_t* data, size_t len, bool viaPath) {
  if (len < 4) return;
  uint32_t tag = 0;
  std::memcpy(&tag, data, 4);
  if (waitingFor_ != 0 && tag != waitingFor_) {
    // Not the reply this job is waiting for - an old one that took another route home. It is
    // dropped rather than read: a tag is how a reply is matched, on both ends.
    LOG_D("mesh: a response with another tag arrived and was dropped");
    return;
  }
  reply_ = MeshReply();
  reply_.kind = MeshReply::Kind::Response;
  reply_.tag = tag;
  reply_.viaPath = viaPath;
  size_t n = len - 4;
  if (n > sizeof reply_.data) n = sizeof reply_.data;
  std::memcpy(reply_.data, data + 4, n);
  reply_.len = static_cast<uint8_t>(n);
  reply_.snr = pkt->getSNR();
}

void AgentMesh::onPeerDataRecv(mesh::Packet* pkt, uint8_t type, int, const uint8_t*, uint8_t* data, size_t len) {
  // A REQ never reaches this: onRecvPacket released it, so nothing here can compose a reply.
  if (type != PAYLOAD_TYPE_RESPONSE) return;
  takeResponse(pkt, data, len, false);
}

bool AgentMesh::onPeerPathRecv(mesh::Packet* pkt, int, const uint8_t*, uint8_t* path, uint8_t path_len,
                               uint8_t extra_type, uint8_t* extra, uint8_t extra_len) {
  // A flooded request is answered with the way back, and the reply rides in `extra`. Take the
  // path - the next transaction of this job goes direct - and the reply with it.
  const uint8_t size = (path_len >> 6) + 1;
  const uint8_t count = path_len & 63;
  if (static_cast<size_t>(size) * count <= MAX_PATH_SIZE) {
    std::memcpy(outPath_, path, static_cast<size_t>(size) * count);
    outPathLen_ = path_len;
    haveOutPath_ = true;
    LOG_D("mesh: the reply came back along a path of %u hop(s); the rest of this job goes direct",
          static_cast<unsigned>(count));
  }
  if (extra_type == PAYLOAD_TYPE_RESPONSE && extra != nullptr) takeResponse(pkt, extra, extra_len, true);
  // **false**: no reciprocal path is sent. One send per command is the rail, and a repeater that
  // answered us has already learnt what it needs from the request it answered.
  return false;
}

void AgentMesh::onTraceRecv(mesh::Packet* pkt, uint32_t tag, uint32_t, uint8_t flags, const uint8_t* path_snrs,
                            const uint8_t* path_hashes, uint8_t path_len) {
  if (waitingFor_ != 0 && tag != waitingFor_) return;
  const uint8_t hops = static_cast<uint8_t>(path_len >> (flags & 0x03));
  reply_ = MeshReply();
  reply_.kind = MeshReply::Kind::Trace;
  reply_.tag = tag;
  reply_.traceFlags = flags;
  reply_.traceHashBytes = path_len > MAX_PATH_SIZE ? MAX_PATH_SIZE : path_len;
  std::memcpy(reply_.traceHashes, path_hashes, reply_.traceHashBytes);
  reply_.traceSnrCount = hops > MAX_PATH_SIZE ? MAX_PATH_SIZE : hops;
  std::memcpy(reply_.traceSnrs, path_snrs, reply_.traceSnrCount);
  reply_.traceFinalSnr = pkt->getSNR();
  reply_.snr = pkt->getSNR();
}

}  // namespace mm
