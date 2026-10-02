#include "HostMesh.h"

#include <Arduino.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "Log.h"
#include "agent/Credentials.h"

namespace mm {

namespace {

constexpr size_t kCipherBlockSize = 16;  // MeshCore's CIPHER_BLOCK_SIZE (src/MeshCore.h:14)
// `Mesh::createDatagram` refuses a plaintext past MAX_PACKET_PAYLOAD - MAC - (BLOCK - 1), so a
// reply longer than this is never sent at all: the request is answered with silence.
constexpr size_t kMaxDatagramPlaintext = kMaxReplyDataLen - 2 - (kCipherBlockSize - 1);  // 167
constexpr uint8_t kFirmwareVerLevel = 2;  // FIRMWARE_VER_LEVEL, simple_repeater/MyMesh.cpp:43
constexpr int kMaxClients = 32;           // MAX_CLIENTS, simple_repeater/MyMesh.h:63
constexpr size_t kCliReplyCap = kMaxCliReplyLen + 1;

const char kHex[] = "0123456789abcdef";

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

size_t hexToBytes(const char* hex, uint8_t* out, size_t max) {
  size_t n = 0;
  while (n < max && hex[n * 2] != '\0' && hex[n * 2 + 1] != '\0') {
    const int hi = hexValue(hex[n * 2]);
    const int lo = hexValue(hex[n * 2 + 1]);
    if (hi < 0 || lo < 0) break;
    out[n++] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return n;
}

void put16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

void put32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = static_cast<uint8_t>((v >> (8 * i)) & 0xFF);
}

char* skipSpaces(char* s) {
  while (*s == ' ') s++;
  return s;
}

bool isBlank(const char* s) {
  while (*s != '\0') {
    if (*s != ' ' && *s != '\t' && *s != '\r' && *s != '\n') return false;
    s++;
  }
  return true;
}

// `isValidName` of CommonCLI.cpp:22-28.
bool isValidName(const char* n) {
  while (*n != '\0') {
    if (*n == '[' || *n == ']' || *n == '\\' || *n == ':' || *n == ',' || *n == '?' || *n == '*') return false;
    n++;
  }
  return true;
}

// `_atoi` of CommonCLI.cpp:13-20: digits only, and it stops at the first thing that is not one.
uint32_t firmwareAtoi(const char* sp) {
  uint32_t n = 0;
  while (*sp != '\0' && *sp >= '0' && *sp <= '9') {
    n *= 10;
    n += static_cast<uint32_t>(*sp++ - '0');
  }
  return n;
}

// `StrHelper::ftoa` of TxtDataHelpers.cpp:52-130, which is what every float-valued `get` answers
// with - seven digits of fraction, **truncated** and not rounded, trailing zeros removed, and
// "0.0" for zero. Ported rather than approximated with "%g": `get lat` on a repeater at 56.41
// answers `> 56.4099998` and not `> 56.41`, and a config snapshot that says otherwise is a
// snapshot of something else.
const char* firmwareFtoa(float f) {
  static char tmp[24];
  char* p = tmp;
  if (f == 0.0f) {
    std::snprintf(tmp, sizeof tmp, "0.0");
    return tmp;
  }
  union {
    int32_t Long;
    float Float;
  } x;
  x.Float = f;
  const int16_t exp2 = static_cast<int16_t>(static_cast<unsigned char>(x.Long >> 23) - 127);
  const int32_t mantissa = (x.Long & 0xFFFFFF) | 0x800000;
  int32_t frac = 0;
  int32_t whole = 0;
  if (exp2 >= 31 || exp2 < -23) {
    std::snprintf(tmp, sizeof tmp, "0");  // the firmware's own fallback for out of range
    return tmp;
  }
  if (exp2 >= 23) {
    whole = mantissa << (exp2 - 23);
  } else if (exp2 >= 0) {
    whole = mantissa >> (23 - exp2);
    frac = (mantissa << (exp2 + 1)) & 0xFFFFFF;
  } else {
    frac = (mantissa & 0xFFFFFF) >> -(exp2 + 1);
  }
  if (x.Long < 0) *p++ = '-';
  if (whole == 0) {
    *p++ = '0';
  } else {
    p += std::snprintf(p, sizeof tmp - static_cast<size_t>(p - tmp), "%ld", static_cast<long>(whole));
  }
  *p++ = '.';
  if (frac == 0) {
    *p++ = '0';
  } else {
    for (int m = 0; m < 7; m++) {  // PRECISION
      frac = (frac << 3) + (frac << 1);
      *p++ = static_cast<char>((frac >> 24) + '0');
      frac &= 0xFFFFFF;
    }
    for (--p; p[0] == '0' && p[-1] != '.'; --p) {
    }
    ++p;
  }
  *p = '\0';
  return tmp;
}

// The command as the journal records it: a `password <new>` or a `set guest.password <new>` keeps
// its prefix and loses its value, by the rule agent/Credentials.h sets for the frame log.
void journalCommand(const char* command, char* out, size_t cap) {
  static const char* const kSecret[] = {"password ", "set guest.password "};
  for (const char* prefix : kSecret) {
    const size_t n = std::strlen(prefix);
    if (std::strncmp(command, prefix, n) != 0) continue;
    size_t at = 0;
    while (at < n && at + 1 < cap) {
      out[at] = prefix[at];
      at++;
    }
    for (size_t i = 0; command[n + i] != '\0' && at + 1 < cap; i++) out[at++] = '*';
    out[at] = '\0';
    return;
  }
  std::snprintf(out, cap, "%s", command);
}

// `getLogDateTime`'s shape, which is what every clock reply of CommonCLI is built from:
// "%02d:%02d - %d/%d/%d UTC" (CommonCLI.cpp:204-206, :222-224).
void formatRepeaterClock(uint32_t epoch, char* out, size_t cap) {
  const std::time_t when = static_cast<std::time_t>(epoch);
  std::tm parts{};
#if defined(_WIN32)
  ::gmtime_s(&parts, &when);
#else
  ::gmtime_r(&when, &parts);
#endif
  std::snprintf(out, cap, "%02d:%02d - %d/%d/%d UTC", parts.tm_hour, parts.tm_min, parts.tm_mday, parts.tm_mon + 1,
                parts.tm_year + 1900);
}

// The firmware's own refusal for a key whose value this repeater will not take (the `refuse` knob
// of a scenario): the spelling CommonCLI::handleSetCmd uses for that key and no other.
const char* refusalFor(const char* key) {
  if (std::strcmp(key, "dutycycle") == 0) return "ERROR: dutycycle must be 1-100";
  if (std::strcmp(key, "advert.interval") == 0) return "Error: interval range is 60-240 minutes";
  if (std::strcmp(key, "flood.advert.interval") == 0) return "Error: interval range is 3-168 hours";
  if (std::strncmp(key, "flood.max", 9) == 0) return "Error, max 64";
  if (std::strcmp(key, "path.hash.mode") == 0) return "Error, must be 0,1, or 2";
  if (std::strcmp(key, "name") == 0) return "Error, bad chars";
  return "Error";
}

}  // namespace

void HostMesh::begin(HostScenario& scenario, const char* exitNodeKeyHex) {
  scenario_ = &scenario;
  std::snprintf(exitNodeKey_, sizeof exitNodeKey_, "%s", exitNodeKeyHex == nullptr ? "" : exitNodeKeyHex);
  for (Client& c : clients_) c = Client();
  journalCount_ = 0;
}

void HostMesh::setClock(uint32_t (*clock)()) { clock_ = clock; }

uint32_t HostMesh::repeaterNow() const {
  const uint32_t now = clock_ != nullptr ? clock_() : 0;
  const int32_t offset = peer_ != nullptr ? peer_->clockOffsetS : 0;
  return static_cast<uint32_t>(static_cast<int64_t>(now) + offset);
}

const HeardAdvert& HostMesh::recent(int) const { return recent_[0]; }

uint32_t HostMesh::uniqueTime() {
  const uint32_t now = clock_ != nullptr ? clock_() : 0;
  lastUnique_ = now > lastUnique_ ? now : lastUnique_ + 1;
  return lastUnique_;
}

// ------------------------------------------------------------------------------- the journal

const HostTransaction& HostMesh::transaction(int i) const {
  static const HostTransaction none;
  if (i < 0 || i >= journalCount_) return none;
  return journal_[i];
}

void HostMesh::note(const char* kind, const char* what, uint32_t timestamp, bool flood, bool answered) {
  counters_.tx++;
  if (journalCount_ >= kMaxHostTransactions) return;
  HostTransaction& t = journal_[journalCount_];
  t = HostTransaction();
  t.seq = journalCount_ + 1;
  std::snprintf(t.repeater, sizeof t.repeater, "%s", journalKey_);
  std::snprintf(t.kind, sizeof t.kind, "%s", kind);
  std::snprintf(t.what, sizeof t.what, "%s", what);
  t.timestamp = timestamp;
  t.flood = flood;
  t.answered = answered;
  journalCount_++;
  if (frameLog_) {
    LOG_D("fake: %s %s ts %lu (%s) -> %s", kind, what, static_cast<unsigned long>(timestamp),
          flood ? "flood" : "direct", answered ? "answered" : "silence");
  }
}

size_t HostMesh::writeJournal(char* out, size_t cap) const {
  size_t n = 0;
  const int written = std::snprintf(out, cap, "{\"transactions\":[");
  if (written < 0) return 0;
  n = static_cast<size_t>(written);
  for (int i = 0; i < journalCount_ && n < cap; i++) {
    const HostTransaction& t = journal_[i];
    char what[2 * sizeof t.what];
    size_t w = 0;
    for (size_t c = 0; t.what[c] != '\0' && w + 2 < sizeof what; c++) {
      if (t.what[c] == '"' || t.what[c] == '\\') what[w++] = '\\';
      what[w++] = t.what[c];
    }
    what[w] = '\0';
    const int one = std::snprintf(
        out + n, cap - n,
        "%s{\"seq\":%d,\"repeater\":\"%s\",\"kind\":\"%s\",\"what\":\"%s\",\"timestamp\":%lu,\"route\":\"%s\","
        "\"answered\":%s}",
        i > 0 ? "," : "", t.seq, t.repeater, t.kind, what, static_cast<unsigned long>(t.timestamp),
        t.flood ? "flood" : "direct", t.answered ? "true" : "false");
    if (one < 0 || static_cast<size_t>(one) >= cap - n) return 0;
    n += static_cast<size_t>(one);
  }
  const int tail = std::snprintf(out + n, cap - n, "]}");
  if (tail < 0 || static_cast<size_t>(tail) >= cap - n) return 0;
  return n + static_cast<size_t>(tail);
}

// ------------------------------------------------------------------------------- the job

HostAclEntry* HostMesh::ownEntry() const {
  if (peer_ == nullptr) return nullptr;
  return const_cast<HostAclEntry*>(peer_->aclEntry(exitNodeKey_));
}

bool HostMesh::beginJob(const char* publicKeyHex, const uint8_t* pathHashes, uint8_t hopCount, uint8_t hashSize) {
  if (publicKeyHex == nullptr || std::strlen(publicKeyHex) != kPublicKeyHexLen) return false;
  peer_ = scenario_ != nullptr ? scenario_->find(publicKeyHex) : nullptr;
  peerIndex_ = -1;
  if (scenario_ != nullptr && peer_ != nullptr) {
    for (int i = 0; i < scenario_->repeaterCount; i++) {
      if (&scenario_->repeaters[i] == peer_) peerIndex_ = i;
    }
  }
  if (peer_ == nullptr) {
    // A repeater the scenario does not hold is one that is not on this mesh: the frames go out and
    // nothing answers, which is what a board at a window sees when a repeater has been taken down.
    LOG_W("fake: no repeater in the scenario has the key %.12s...: every frame to it is silence", publicKeyHex);
    // The peer is still "set" so that a send is counted and journalled; a nullptr spec is silence.
    peer_ = nullptr;
  }
  hasPath_ = false;
  outHops_ = 0;
  outHashSize_ = 1;
  if (pathHashes != nullptr && hopCount > 0 && hashSize > 0 &&
      static_cast<size_t>(hopCount) * hashSize <= kMaxMeshPathBytes) {
    std::memcpy(outPath_, pathHashes, static_cast<size_t>(hopCount) * hashSize);
    outHops_ = hopCount;
    outHashSize_ = hashSize;
    hasPath_ = true;
  }
  pending_ = Pending::None;
  reply_ = MeshReply();
  // The peer's own key must be known even when the scenario has no such repeater, so that a job
  // for a stranger is journalled against it.
  std::snprintf(journalKey_, sizeof journalKey_, "%.12s", publicKeyHex);
  return true;
}

void HostMesh::endJob() {
  peer_ = nullptr;
  peerIndex_ = -1;
  pending_ = Pending::None;
  hasPath_ = false;
  outHops_ = 0;
}

void HostMesh::setDirectPath() {
  hasPath_ = true;
  outHops_ = 0;
  outHashSize_ = 1;
}

void HostMesh::forgetPath() {
  hasPath_ = false;
  outHops_ = 0;
}

int HostMesh::describePath(char hops[][8], int max) const {
  if (!hasPath_) return 0;
  int written = 0;
  for (int i = 0; i < outHops_ && written < max; i++) {
    char* out = hops[written];
    size_t n = 0;
    for (int b = 0; b < outHashSize_ && n + 2 < 8; b++) {
      const uint8_t byte = outPath_[i * outHashSize_ + b];
      out[n++] = kHex[byte >> 4];
      out[n++] = kHex[byte & 0x0F];
    }
    out[n] = '\0';
    written++;
  }
  return written;
}

void HostMesh::learnPathFromFlood() {
  if (peer_ == nullptr || peer_->hops <= 0) {
    setDirectPath();
    return;
  }
  // The hops the reply came back along, made up from the repeater's key so that a run is
  // repeatable and two repeaters do not share a path.
  uint8_t key[32];
  hexToBytes(peer_->key, key, sizeof key);
  const int hops = peer_->hops > static_cast<int>(kMaxMeshPathBytes) ? static_cast<int>(kMaxMeshPathBytes) : peer_->hops;
  for (int i = 0; i < hops; i++) outPath_[i] = static_cast<uint8_t>(key[i % 32] ^ (0x5A + i));
  outHops_ = static_cast<uint8_t>(hops);
  outHashSize_ = 1;
  hasPath_ = true;
}

// ------------------------------------------------------------------------------- the replies

void HostMesh::queue(Pending kind, const uint8_t* body, size_t len, uint32_t tag) {
  staged_ = MeshReply();
  staged_.kind = kind == Pending::Trace ? MeshReply::Kind::Trace : MeshReply::Kind::Response;
  staged_.tag = tag;
  staged_.viaPath = !hasPath_;
  staged_.snr = 7.25f;
  // The cipher block's zero padding: the reply is one AES datagram and MeshCore pads its plaintext
  // to a multiple of sixteen (`Utils::encrypt`, Utils.cpp:85-105), so what arrives at the board is
  // the reply **and between one and fifteen zero bytes**. Everything that reads a reply has to
  // cope with them (mesh/ReplyCodec.h), and a fake that trimmed them would be testing a mesh that
  // does not exist.
  size_t padded = ((len + kCipherBlockSize - 1) / kCipherBlockSize) * kCipherBlockSize;
  if (padded > kMaxReplyDataLen) padded = kMaxReplyDataLen;
  size_t n = padded > 4 ? padded - 4 : 0;  // the four-byte tag is stripped by the board
  if (n > sizeof staged_.data) n = sizeof staged_.data;
  if (body != nullptr && len > 4) {
    const size_t copy = (len - 4) < n ? (len - 4) : n;
    std::memcpy(staged_.data, body + 4, copy);
  }
  staged_.len = static_cast<uint8_t>(n);
  pending_ = kind;
  pendingAtMs_ = millis() + (peer_ != nullptr ? peer_->latencyMs * static_cast<uint32_t>(peer_->hops + 1) : 60u);
}

void HostMesh::queueText(const char* text) {
  staged_ = MeshReply();
  staged_.kind = MeshReply::Kind::Text;
  staged_.snr = 7.25f;
  std::snprintf(staged_.text, sizeof staged_.text, "%s", text);
  pending_ = Pending::Text;
  pendingAtMs_ = millis() + (peer_ != nullptr ? peer_->latencyMs * static_cast<uint32_t>(peer_->hops + 1) : 60u);
}

void HostMesh::loop() {
  if (pending_ == Pending::None) return;
  if (static_cast<int32_t>(millis() - pendingAtMs_) < 0) return;
  const bool wantText = awaitingText_;
  const bool haveText = pending_ == Pending::Text;
  if (wantText != haveText) {
    // The board drops a RESPONSE while a text is awaited and a text that is not the one awaited,
    // and so does this (src/mesh/AgentMesh.cpp's takeResponse/takeText).
    pending_ = Pending::None;
    return;
  }
  if (!haveText && awaitingTag_ != 0 && staged_.tag != awaitingTag_) {
    pending_ = Pending::None;
    return;
  }
  reply_ = staged_;
  counters_.rx++;
  pending_ = Pending::None;
  if (reply_.viaPath) learnPathFromFlood();
}

bool HostMesh::awaitReply(uint32_t tag, uint32_t timeoutMs, void (*pump)()) {
  reply_ = MeshReply();
  awaitingText_ = false;
  awaitingTag_ = tag;
  const uint32_t deadline = millis() + timeoutMs;
  while (static_cast<int32_t>(millis() - deadline) < 0) {
    loop();
    if (pump != nullptr) pump();
    if (reply_.kind != MeshReply::Kind::None) return true;
    if (pending_ == Pending::None) break;  // silence: there is nothing coming, so do not sit it out
    delay(1);
  }
  // A real silence costs the whole timeout on the air. Here it costs it too, up to a point: the
  // wait above ends as soon as it is known that nothing is coming, because a harness that sat out
  // thirteen four-second timeouts per dropped reply would make a cycle minutes of nothing. What is
  // *not* shortened is the airtime budget: that is the pacing the server set and the harness keeps
  // it to the second (docs/host.md).
  return false;
}

bool HostMesh::awaitText(uint32_t timeoutMs, void (*pump)()) {
  reply_ = MeshReply();
  awaitingText_ = true;
  awaitingTag_ = 0;
  const uint32_t deadline = millis() + timeoutMs;
  while (static_cast<int32_t>(millis() - deadline) < 0) {
    loop();
    if (pump != nullptr) pump();
    if (reply_.kind != MeshReply::Kind::None) return true;
    if (pending_ == Pending::None) break;
    delay(1);
  }
  return false;
}

// ------------------------------------------------------------------------------- the login

SendResult HostMesh::sendLogin(const char* password, uint32_t& tag) {
  // **The tag a login reply carries is the repeater's own clock and not the sender's**
  // (`handleLoginReq`, simple_repeater/MyMesh.cpp:129-131, against `handleRequest`'s
  // "reflect sender_timestamp back" at :214). So a login is matched on nothing, exactly as the
  // .NET agent matches `PUSH_CODE_LOGIN_SUCCESS` on nothing.
  tag = 0;
  const uint32_t senderTimestamp = uniqueTime();
  const bool flood = !hasPath_;

  if (peer_ == nullptr || !peer_->online) {
    note("login", "login", senderTimestamp, flood, false);
    return flood ? SendResult::Flood : SendResult::Direct;
  }
  if (peer_->drops("login")) {
    note("login", "login", senderTimestamp, flood, false);
    return flood ? SendResult::Flood : SendResult::Direct;
  }

  const char* material = password == nullptr ? "" : password;
  HostAclEntry* client = nullptr;
  if (material[0] == '\0') {
    // A blank password is "just check if sender is in ACL" (:89-96).
    client = ownEntry();
  }
  uint8_t perms = 0;
  bool wrotePermissions = false;
  if (client == nullptr) {
    // **The fall-through the spec does not describe.** A key the ACL does not hold does not get
    // silence here: it gets the password check, with the empty string as the password. A repeater
    // whose guest password has never been set holds "" (CommonCLI.h:191), so `strcmp("", "")` is a
    // match and a blank login from a stranger is answered **guest**. Silence is what a *wrong*
    // password gets (:106-110).
    if (peer_->acceptAnyPassword && material[0] != '\0') {
      // The compose-stack concession (HostScenario.h): the server will not tell a script what it
      // stored, so any password it sends is taken as the admin one.
      perms = kPermAdmin;
    } else if (std::strcmp(material, peer_->adminPassword) == 0 && peer_->adminPassword[0] != '\0') {
      perms = kPermAdmin;
    } else if (std::strcmp(material, peer_->guestPassword) == 0) {
      perms = kPermGuest;
    } else {
      note("login", "login", senderTimestamp, flood, false);
      return flood ? SendResult::Flood : SendResult::Direct;
    }
    // `acl.putClient(sender, 0)`: the entry is made if it is not there, and the table can be full.
    client = const_cast<HostAclEntry*>(peer_->aclEntry(exitNodeKey_));
    if (client == nullptr) {
      if (peer_->aclCount >= kMaxClients || peer_->aclCount >= kMaxHostAclEntries) {
        note("login", "login", senderTimestamp, flood, false);
        return flood ? SendResult::Flood : SendResult::Direct;
      }
      client = &peer_->acl[peer_->aclCount++];
      std::snprintf(client->key, sizeof client->key, "%s", exitNodeKey_);
      client->permissions = 0;
    }
    Client& state = clients_[peerIndex_ >= 0 ? peerIndex_ : 0];
    if (senderTimestamp <= state.lastTimestamp) {
      // A replayed login: silence (:112-116).
      note("login", "login", senderTimestamp, flood, false);
      return flood ? SendResult::Flood : SendResult::Direct;
    }
    state.lastTimestamp = senderTimestamp;
    client->permissions = static_cast<uint8_t>((client->permissions & ~kPermRoleMask) | perms);
    wrotePermissions = true;
  }

  uint8_t body[16] = {};
  // The tag: `getRTCClock()->getCurrentTimeUnique()` of the **repeater**.
  put32(body, (clock_ != nullptr ? clock_() : 0) + 1);
  body[4] = 0;  // RESP_SERVER_LOGIN_OK
  body[5] = 0;  // legacy keep-alive / 16
  body[6] = (client->permissions & kPermRoleMask) == kPermAdmin ? 1 : 0;
  body[7] = client->permissions;
  put32(&body[8], 0x5A5A5A5A);  // the random blob, fixed so a run is repeatable
  body[12] = kFirmwareVerLevel;
  queue(Pending::Response, body, 13, 0);

  (void)wrotePermissions;
  note("login", "login", senderTimestamp, flood, true);
  return flood ? SendResult::Flood : SendResult::Direct;
}

// ------------------------------------------------------------------------------- the requests

size_t HostMesh::handleRequest(uint8_t reqType, const uint8_t* data, uint8_t len, uint32_t tag, uint8_t* out,
                               size_t cap, char* what, size_t whatCap) {
  put32(out, tag);  // "reflect sender_timestamp back in response packet (kind of like a 'tag')"
  const HostAclEntry* client = ownEntry();
  const uint8_t role = client != nullptr ? static_cast<uint8_t>(client->permissions & kPermRoleMask) : kPermGuest;

  if (reqType == MeshPort::kReqTypeGetStatus) {
    std::snprintf(what, whatCap, "status");
    if (peer_->drops("status")) return 0;
    const HostStatus& s = peer_->status;
    size_t i = 4;
    put16(&out[i], static_cast<uint16_t>(s.batteryMv)); i += 2;
    put16(&out[i], static_cast<uint16_t>(s.txQueueLen)); i += 2;
    put16(&out[i], static_cast<uint16_t>(static_cast<int16_t>(s.noiseFloorDbm))); i += 2;
    put16(&out[i], static_cast<uint16_t>(static_cast<int16_t>(s.lastRssi))); i += 2;
    put32(&out[i], s.packetsRecv); i += 4;
    put32(&out[i], s.packetsSent); i += 4;
    put32(&out[i], s.airtimeS); i += 4;
    put32(&out[i], s.uptimeS); i += 4;
    put32(&out[i], s.sentFlood); i += 4;
    put32(&out[i], s.sentDirect); i += 4;
    put32(&out[i], s.recvFlood); i += 4;
    put32(&out[i], s.recvDirect); i += 4;
    put16(&out[i], static_cast<uint16_t>(s.errEvents)); i += 2;
    put16(&out[i], static_cast<uint16_t>(static_cast<int16_t>(std::lround(s.lastSnr * 4.0)))); i += 2;
    put16(&out[i], static_cast<uint16_t>(s.directDups)); i += 2;
    put16(&out[i], static_cast<uint16_t>(s.floodDups)); i += 2;
    if (!s.legacy) {
      put32(&out[i], s.rxAirtimeS); i += 4;
      put32(&out[i], s.recvErrors); i += 4;
    }
    (void)cap;
    return i;
  }

  if (reqType == MeshPort::kReqTypeGetTelemetry) {
    std::snprintf(what, whatCap, "telemetry");
    if (peer_->drops("telemetry")) return 0;
    // `perm_mask = ~payload[1]`, and a guest session is held to base telemetry whatever it asked
    // for (:243-250). Base telemetry is channel 1's voltage; everything else needs more.
    const bool baseOnly = role == kPermGuest || (len > 1 && data != nullptr && static_cast<uint8_t>(~data[1]) == 0);
    size_t i = 4;
    for (int r = 0; r < peer_->telemetryCount; r++) {
      const HostTelemetryReading& reading = peer_->telemetry[r];
      if (baseOnly && reading.typeCode != 116) continue;  // voltage alone
      if (i + 6 > cap) break;
      out[i++] = static_cast<uint8_t>(reading.channel);
      out[i++] = reading.typeCode;
      switch (reading.typeCode) {
        case 104:  // humidity, one byte, 0.5
          out[i++] = static_cast<uint8_t>(std::lround(reading.value / 0.5));
          break;
        case 131: {  // energy, four bytes, 0.001
          put32(&out[i], static_cast<uint32_t>(std::lround(reading.value / 0.001)));
          i += 4;
          break;
        }
        case 103:  // temperature, two bytes signed, 0.1
          put16(&out[i], static_cast<uint16_t>(static_cast<int16_t>(std::lround(reading.value / 0.1))));
          i += 2;
          break;
        case 117:  // current, two bytes signed, 0.001
          put16(&out[i], static_cast<uint16_t>(static_cast<int16_t>(std::lround(reading.value / 0.001))));
          i += 2;
          break;
        case 116:  // voltage, two bytes signed, 0.01
          put16(&out[i], static_cast<uint16_t>(static_cast<int16_t>(std::lround(reading.value / 0.01))));
          i += 2;
          break;
        case 115:  // pressure, two bytes, 0.1
          put16(&out[i], static_cast<uint16_t>(std::lround(reading.value / 0.1)));
          i += 2;
          break;
        default:
          put16(&out[i], static_cast<uint16_t>(std::lround(reading.value)));
          i += 2;
          break;
      }
    }
    return i;
  }

  if (reqType == MeshPort::kReqTypeGetAccessList) {
    std::snprintf(what, whatCap, "acl");
    if (peer_->drops("acl")) return 0;
    // Admin only, and a session below it is answered with nothing at all (:262-275, the
    // `sender->isAdmin()` guard returning 0).
    if (role != kPermAdmin) return 0;
    if (data != nullptr && len >= 2 && (data[0] != 0 || data[1] != 0)) return 0;  // the reserved bytes
    size_t i = 4;
    int entries = 0;
    for (int e = 0; e < peer_->aclCount; e++) {
      const HostAclEntry& a = peer_->acl[e];
      if (a.permissions == 0) continue;  // "skip deleted entries"
      // The firmware's own loop writes while `ofs + 7 <= sizeof(reply_data) - 4`, which is 25
      // entries - but a reply of 4 + 24 x 7 = 172 bytes is one `Mesh::createDatagram` refuses, so
      // 24 entries or more is **silence** and not a short list (mesh/ReplyCodec.h).
      if (i + kAccessListEntrySize > kMaxDatagramPlaintext) return 0;
      uint8_t key[32];
      hexToBytes(a.key, key, sizeof key);
      std::memcpy(&out[i], key, kAccessListPrefixBytes);
      i += kAccessListPrefixBytes;
      out[i++] = a.permissions;
      entries++;
    }
    (void)entries;
    return i;
  }

  if (reqType == MeshPort::kReqTypeGetNeighbours) {
    if (data == nullptr || len < 7 || data[0] != 0) return 0;  // request_version must be 0
    // `handleRequest` reads these out of `payload`, whose [0] is the request type - so payload[k] is
    // this `data[k - 1]`: count at payload[2], the offset at [3..4], order_by at [5] and the public
    // key prefix length at [6] (MyMesh.cpp:277-285). Reading the prefix length one byte late takes
    // it from the request's random blob instead, which the harness's own first run did: the pages
    // came back three entries long and the prefixes were somebody else's bytes.
    const int count = data[1];
    const int offset = data[2] | (data[3] << 8);
    int prefixLen = data[5];
    if (prefixLen > 32) prefixLen = 32;
    std::snprintf(what, whatCap, "neighbours@%d", offset);
    if (peer_->drops(what) || peer_->drops("neighbours")) return 0;
    size_t i = 4;
    const int total = peer_->neighbourCount;
    int returned = 0;
    uint8_t results[130];
    size_t at = 0;
    for (int index = 0; index < count && index + offset < total; index++) {
      const size_t entry = static_cast<size_t>(prefixLen) + 4 + 1;
      if (at + entry > sizeof results) break;
      const HostNeighbour& n = peer_->neighbours[index + offset];
      uint8_t key[32];
      hexToBytes(n.key, key, sizeof key);
      std::memcpy(&results[at], key, static_cast<size_t>(prefixLen));
      at += static_cast<size_t>(prefixLen);
      put32(&results[at], n.heardSecondsAgo);
      at += 4;
      results[at++] = static_cast<uint8_t>(static_cast<int8_t>(std::lround(n.snr * 4.0)));
      returned++;
    }
    put16(&out[i], static_cast<uint16_t>(total)); i += 2;
    put16(&out[i], static_cast<uint16_t>(returned)); i += 2;
    if (i + at > cap) return 0;
    std::memcpy(&out[i], results, at);
    i += at;
    return i;
  }

  if (reqType == MeshPort::kReqTypeGetOwnerInfo) {
    std::snprintf(what, whatCap, "owner-info");
    if (peer_->drops("owner-info")) return 0;
    const int n = std::snprintf(reinterpret_cast<char*>(&out[4]), cap - 4, "%s\n%s\n%s", peer_->firmware, peer_->name,
                                peer_->ownerInfo);
    if (n <= 0) return 0;
    return 4 + static_cast<size_t>(n);
  }

  std::snprintf(what, whatCap, "req 0x%02x", reqType);
  return 0;  // "unknown command"
}

SendResult HostMesh::sendRequest(uint8_t reqType, const uint8_t* data, uint8_t len, uint32_t& tag) {
  tag = uniqueTime();
  const bool flood = !hasPath_;
  char what[48];
  std::snprintf(what, sizeof what, "req 0x%02x", reqType);

  if (peer_ == nullptr || !peer_->online) {
    note("req", what, tag, flood, false);
    return flood ? SendResult::Flood : SendResult::Direct;
  }
  // A REQ is answered only to a client the ACL holds: `searchPeersByHash` looks in the ACL and
  // `onPeerDataRecv` needs the entry (:663-700). A key with no entry is never decrypted at all.
  const HostAclEntry* client = ownEntry();
  if (client == nullptr) {
    note("req", what, tag, flood, false);
    return flood ? SendResult::Flood : SendResult::Direct;
  }
  Client& state = clients_[peerIndex_ >= 0 ? peerIndex_ : 0];
  if (tag <= state.lastTimestamp) {
    // "prevent replay attacks": strictly greater for a REQ (:674).
    note("req", what, tag, flood, false);
    return flood ? SendResult::Flood : SendResult::Direct;
  }

  uint8_t body[kMaxReplyDataLen] = {};
  const size_t replyLen = handleRequest(reqType, data, len, tag, body, sizeof body, what, sizeof what);
  if (replyLen == 0) {
    note("req", what, tag, flood, false);
    return flood ? SendResult::Flood : SendResult::Direct;
  }
  state.lastTimestamp = tag;
  queue(Pending::Response, body, replyLen, tag);
  note("req", what, tag, flood, true);
  return flood ? SendResult::Flood : SendResult::Direct;
}

SendResult HostMesh::sendDiscovery(uint32_t& tag) {
  // The companion's CMD_SEND_PATH_DISCOVERY_REQ is a flooded base-telemetry REQ with the contact's
  // path forced to unknown, so it always floods.
  forgetPath();
  tag = uniqueTime();
  if (peer_ == nullptr || !peer_->online || peer_->drops("discover")) {
    note("discover", "discover", tag, true, false);
    return SendResult::Flood;
  }
  const HostAclEntry* client = ownEntry();
  if (client == nullptr) {
    note("discover", "discover", tag, true, false);
    return SendResult::Flood;
  }
  Client& state = clients_[peerIndex_ >= 0 ? peerIndex_ : 0];
  if (tag <= state.lastTimestamp) {
    note("discover", "discover", tag, true, false);
    return SendResult::Flood;
  }
  state.lastTimestamp = tag;
  uint8_t request[8] = {0, static_cast<uint8_t>(~0x01u), 0, 0, 0, 0, 0, 0};
  uint8_t body[kMaxReplyDataLen] = {};
  char what[48];
  const size_t replyLen =
      handleRequest(MeshPort::kReqTypeGetTelemetry, request, sizeof request, tag, body, sizeof body, what, sizeof what);
  if (replyLen == 0) {
    note("discover", "discover", tag, true, false);
    return SendResult::Flood;
  }
  queue(Pending::Response, body, replyLen, tag);
  note("discover", "discover", tag, true, true);
  return SendResult::Flood;
}

SendResult HostMesh::sendTrace(const uint8_t* roundTripHashes, uint8_t hashBytes, uint8_t hashSize, uint32_t& tag) {
  if (roundTripHashes == nullptr || hashBytes == 0) return SendResult::Failed;
  if (!isTraceableHashSize(hashSize)) return SendResult::Failed;
  tag = uniqueTime();
  if (peer_ == nullptr || !peer_->online || peer_->drops("trace")) {
    note("trace", "trace", tag, false, false);
    return SendResult::Direct;
  }
  staged_ = MeshReply();
  staged_.kind = MeshReply::Kind::Trace;
  staged_.tag = tag;
  staged_.traceFlags = traceFlagsFor(hashSize);
  staged_.traceHashBytes = hashBytes;
  std::memcpy(staged_.traceHashes, roundTripHashes, hashBytes);
  const uint8_t hops = static_cast<uint8_t>(hashBytes / hashSize);
  staged_.traceSnrCount = hops;
  // Every hop appends what it measured, SNR x 4: a plausible fall-off away from this exit node and
  // back, so the result's numbers are not all the same.
  for (uint8_t h = 0; h < hops; h++) {
    const double snr = 9.0 - 1.5 * (h < hops / 2 ? h : (hops - 1 - h));
    staged_.traceSnrs[h] = static_cast<uint8_t>(static_cast<int8_t>(std::lround(snr * 4.0)));
  }
  staged_.traceFinalSnr = 8.5f;
  staged_.snr = 8.5f;
  pending_ = Pending::Trace;
  pendingAtMs_ = millis() + peer_->latencyMs * static_cast<uint32_t>(peer_->hops + 1);
  note("trace", "trace", tag, false, true);
  return SendResult::Direct;
}

// ------------------------------------------------------------------------------- the CLI

SendResult HostMesh::sendCli(const char* text, uint32_t timestamp, bool sensitive) {
  (void)sensitive;
  if (text == nullptr || text[0] == '\0') return SendResult::Failed;
  const bool flood = !hasPath_;
  char what[sizeof(HostTransaction::what)];
  char command[kMaxCliCommandLen + 32];
  journalCommand(text, command, sizeof command);
  std::snprintf(what, sizeof what, "cli:%s", command);

  if (peer_ == nullptr || !peer_->online || peer_->drops(what)) {
    note("cli", what, timestamp, flood, false);
    return flood ? SendResult::Flood : SendResult::Direct;
  }
  // `onPeerDataRecv`: a text message is a CLI command only from a client the ACL holds **at
  // admin** (:702). Anybody below it is dropped without a word - which is why the runner refuses
  // to send one at all on a session below admin, and why a restore can never precede a command.
  const HostAclEntry* client = ownEntry();
  if (client == nullptr || (client->permissions & kPermRoleMask) != kPermAdmin) {
    note("cli", what, timestamp, flood, false);
    return flood ? SendResult::Flood : SendResult::Direct;
  }
  Client& state = clients_[peerIndex_ >= 0 ? peerIndex_ : 0];
  if (timestamp < state.lastTimestamp) {
    // Lower than the last: a replay, dropped (:762).
    note("cli", what, timestamp, flood, false);
    return flood ? SendResult::Flood : SendResult::Direct;
  }
  const bool isRetry = timestamp == state.lastTimestamp;
  state.lastTimestamp = timestamp;

  char reply[kCliReplyCap] = {0};
  if (isRetry) {
    // "if (is_retry) *reply = 0" (:736-740): the command is **not run** and nothing is answered.
    // An apply whose lines shared a second would be one command and a run of silences.
    note("cli", what, timestamp, flood, false);
    return flood ? SendResult::Flood : SendResult::Direct;
  }
  handleCommand(timestamp, text, reply, sizeof reply);
  if (reply[0] == '\0') {
    // A command the firmware answers with nothing: `reboot` (it does not return), and every line
    // of a region load but the last.
    note("cli", what, timestamp, flood, false);
    return flood ? SendResult::Flood : SendResult::Direct;
  }
  queueText(reply);
  note("cli", what, timestamp, flood, true);
  return flood ? SendResult::Flood : SendResult::Direct;
}

void HostMesh::handleCommand(uint32_t senderTimestamp, const char* commandIn, char* reply, size_t cap) {
  reply[0] = '\0';
  char buf[kMaxCliCommandLen + 64];
  std::snprintf(buf, sizeof buf, "%s", commandIn);

  // `MyMesh::handleCommand` (:1196): the region loader's mode comes first, and swallows every
  // command until a blank line ends it.
  if (regionLoadActive_) {
    if (isBlank(buf)) {
      regionLoadActive_ = false;
      std::snprintf(peer_->regionListing, sizeof peer_->regionListing, "%s", regionLoadBuffer_);
      std::snprintf(reply, cap, "OK - loaded %d regions", regionLoadCount_);
      return;
    }
    // A region line: the indent is the depth, and the trailing `F` is "flood allowed".
    const size_t at = std::strlen(regionLoadBuffer_);
    std::snprintf(regionLoadBuffer_ + at, sizeof regionLoadBuffer_ - at, "%s\n", buf);
    regionLoadCount_++;
    return;  // nothing is answered
  }

  char* command = skipSpaces(buf);

  // The companion-radio CLI's optional three-character prefix, reflected back (:1232-1236).
  char prefix[4] = {0};
  if (std::strlen(command) > 4 && command[2] == '|') {
    std::snprintf(prefix, sizeof prefix, "%.3s", command);
    command += 3;
  }
  char out[kCliReplyCap] = {0};

  if (std::strncmp(command, "setperm ", 8) == 0) {
    char* hex = command + 8;
    char* sp = std::strchr(hex, ' ');
    if (sp == nullptr) {
      std::snprintf(out, sizeof out, "Err - bad params");
    } else {
      *sp++ = '\0';
      const size_t hexLen = std::strlen(hex) < 2 * 32 ? std::strlen(hex) : 2 * 32;
      uint8_t key[32];
      const size_t bytes = hexToBytes(hex, key, hexLen / 2);
      if (bytes != hexLen / 2 || bytes == 0) {
        std::snprintf(out, sizeof out, "Err - bad pubkey");
      } else {
        const uint8_t perms = static_cast<uint8_t>(std::atoi(sp));
        bool applied = false;
        if ((perms & kPermRoleMask) == kPermGuest) {
          // `applyPermissions`: the guest role is not persisted, so the entry is **deleted** - and
          // a partial key that matches nothing is "Err - invalid params" (ClientACL.cpp:121-133).
          for (int i = 0; i < peer_->aclCount; i++) {
            char entryPrefix[2 * 32 + 1];
            std::snprintf(entryPrefix, sizeof entryPrefix, "%.*s", static_cast<int>(hexLen), peer_->acl[i].key);
            if (std::strncmp(entryPrefix, hex, hexLen) == 0) {
              for (int j = i; j + 1 < peer_->aclCount; j++) peer_->acl[j] = peer_->acl[j + 1];
              peer_->aclCount--;
              applied = true;
              break;
            }
          }
        } else if (bytes < 32) {
          applied = false;  // "need complete pubkey when adding/modifying"
        } else {
          char full[kPublicKeyHexLen + 1];
          std::snprintf(full, sizeof full, "%.*s", static_cast<int>(kPublicKeyHexLen), hex);
          HostAclEntry* entry = const_cast<HostAclEntry*>(peer_->aclEntry(full));
          if (entry == nullptr && peer_->aclCount < kMaxHostAclEntries && peer_->aclCount < kMaxClients) {
            entry = &peer_->acl[peer_->aclCount++];
            std::snprintf(entry->key, sizeof entry->key, "%s", full);
          }
          if (entry != nullptr) {
            entry->permissions = perms;
            applied = true;
          }
        }
        std::snprintf(out, sizeof out, "%s", applied ? "OK" : "Err - invalid params");
      }
    }
  } else if (std::strncmp(command, "discover.neighbors", 18) == 0) {
    const char* sub = command + 18;
    while (*sub == ' ') sub++;
    std::snprintf(out, sizeof out, "%s",
                  *sub != '\0' ? "Err - discover.neighbors has no options" : "OK - Discover sent");
  } else if (std::strncmp(command, "reboot", 6) == 0) {
    // `_board->reboot()` does not return (CommonCLI.cpp:185-186): nothing is answered, and the
    // repeater forgets every session. The uptime starts again, which is what confirms it.
    peer_->status.uptimeS = 1;
    for (Client& c : clients_) c = Client();
    out[0] = '\0';
  } else if (std::strncmp(command, "advert.zerohop", 14) == 0 &&
             (command[14] == '\0' || command[14] == ' ')) {
    std::snprintf(out, sizeof out, "OK - zerohop advert sent");
  } else if (std::strncmp(command, "advert", 6) == 0) {
    std::snprintf(out, sizeof out, "OK - Advert sent");
  } else if (std::strncmp(command, "clock sync", 10) == 0) {
    // The frame-field spelling of the same command, and the unparenthesised refusal (:199-208).
    std::snprintf(out, sizeof out, "ERR: clock cannot go backwards");
  } else if (std::strncmp(command, "clock", 5) == 0) {
    formatRepeaterClock(repeaterNow(), out, sizeof out);
  } else if (std::strncmp(command, "time ", 5) == 0) {
    // `time <epoch>` (CommonCLI.cpp:217-227): refused when it would move the clock back, and the
    // refusal is the parenthesised one - the unparenthesised spelling belongs to `clock sync`.
    // The comparison is against the **repeater's** clock, which is its own and not the agent's: a
    // scenario's `clockOffsetS` is how far behind (or ahead) it runs, and a repeater that is ahead
    // is how the refusal gets exercised.
    const uint32_t secs = firmwareAtoi(command + 5);
    const uint32_t curr = repeaterNow();
    if (secs > curr) {
      peer_->clockOffsetS += static_cast<int32_t>(secs) - static_cast<int32_t>(curr);
      char when[48];
      formatRepeaterClock(secs, when, sizeof when);
      std::snprintf(out, sizeof out, "OK - clock set: %s", when);
    } else {
      std::snprintf(out, sizeof out, "(ERR: clock cannot go backwards)");
    }
  } else if (std::strncmp(command, "password ", 9) == 0) {
    // The firmware keeps fifteen bytes and **echoes what it stored** (:256-261).
    std::snprintf(peer_->adminPassword, sizeof peer_->adminPassword, "%s", command + 9);
    std::snprintf(out, sizeof out, "password now: %s", peer_->adminPassword);
  } else if (std::strncmp(command, "clear stats", 11) == 0) {
    std::snprintf(out, sizeof out, "(OK - stats reset)");
  } else if (std::strncmp(command, "get ", 4) == 0) {
    handleGetCmd(command + 4, out, sizeof out);
  } else if (std::strncmp(command, "set ", 4) == 0) {
    handleSetCmd(command + 4, out, sizeof out);
  } else if (std::strncmp(command, "ver", 3) == 0) {
    std::snprintf(out, sizeof out, "%s (Build: %s)", peer_->firmware, peer_->build);
  } else if (std::strncmp(command, "board", 5) == 0) {
    std::snprintf(out, sizeof out, "%s", peer_->board);
  } else if (std::strncmp(command, "region", 6) == 0) {
    handleRegionCmd(command, out, sizeof out);
  } else if (std::strncmp(command, "neighbors", 9) == 0) {
    std::snprintf(out, sizeof out, "%d neighbour(s)", peer_->neighbourCount);
  } else {
    std::snprintf(out, sizeof out, "Unknown command");
  }
  (void)senderTimestamp;

  // Whatever CommonCLI wrote lands in its 160-byte buffer and `BufStream::write` simply stops
  // there, with no marker - which is the whole reason a region listing of 159 characters is no
  // evidence (agent/ConfigRead.h).
  std::snprintf(reply, cap, "%s%.*s", prefix, static_cast<int>(kMaxCliReplyLen), out);
}

void HostMesh::handleGetCmd(const char* config, char* reply, size_t cap) {
  // The firmware matches a **prefix**, in this order, and answers `> <value>`
  // (CommonCLI::handleGetCmd, CommonCLI.cpp:807-985). The order matters: `flood.max.unscoped` is
  // tested before `flood.max`, or every unscoped read would answer the other key.
  struct Entry {
    const char* prefix;
    const char* key;
  };
  static const Entry kOrder[] = {
      {"dutycycle", "dutycycle"},
      {"agc.reset.interval", "agc.reset.interval"},
      {"multi.acks", "multi.acks"},
      {"allow.read.only", "allow.read.only"},
      {"flood.advert.interval", "flood.advert.interval"},
      {"advert.interval", "advert.interval"},
      {"guest.password", "guest.password"},
      {"name", "name"},
      {"repeat", "repeat"},
      {"lat", "lat"},
      {"lon", "lon"},
      {"flood.max.advert", "flood.max.advert"},
      {"flood.max.unscoped", "flood.max.unscoped"},
      {"flood.max", "flood.max"},
      {"path.hash.mode", "path.hash.mode"},
      {"loop.detect", "loop.detect"},
      {"tx", "tx"},
      {"freq", "freq"},
      {"role", "role"},
  };
  for (const Entry& e : kOrder) {
    if (std::strncmp(config, e.prefix, std::strlen(e.prefix)) != 0) continue;
    if (std::strcmp(e.key, "tx") == 0 && config[2] != '\0' && config[2] != ' ') continue;  // the one guard
    if (std::strcmp(e.key, "role") == 0) {
      std::snprintf(reply, cap, "> %s", peer_->role);
      return;
    }
    const char* value = peer_->pref(e.key);
    if (value == nullptr) {
      // A key this repeater has no value for still answers: the firmware prints its own default.
      std::snprintf(reply, cap, "> ");
      return;
    }
    if (std::strcmp(e.key, "lat") == 0 || std::strcmp(e.key, "lon") == 0) {
      std::snprintf(reply, cap, "> %s", firmwareFtoa(static_cast<float>(std::atof(value))));
      return;
    }
    std::snprintf(reply, cap, "> %s", value);
    return;
  }
  // `??: <key>`, which the agent keeps verbatim under `raw` (:984).
  std::snprintf(reply, cap, "??: %s", config);
}

void HostMesh::handleSetCmd(const char* config, char* reply, size_t cap) {
  struct Entry {
    const char* prefix;  // with its trailing space, as the firmware spells it
    const char* key;
  };
  static const Entry kOrder[] = {
      {"dutycycle ", "dutycycle"},
      {"agc.reset.interval ", "agc.reset.interval"},
      {"multi.acks ", "multi.acks"},
      {"allow.read.only ", "allow.read.only"},
      {"flood.advert.interval ", "flood.advert.interval"},
      {"advert.interval ", "advert.interval"},
      {"guest.password ", "guest.password"},
      {"name ", "name"},
      {"repeat ", "repeat"},
      {"lat ", "lat"},
      {"lon ", "lon"},
      {"flood.max.advert ", "flood.max.advert"},
      {"flood.max.unscoped ", "flood.max.unscoped"},
      {"flood.max ", "flood.max"},
      {"path.hash.mode ", "path.hash.mode"},
      {"tx ", "tx"},
  };
  for (const Entry& e : kOrder) {
    const size_t n = std::strlen(e.prefix);
    if (std::strncmp(config, e.prefix, n) != 0) continue;
    const char* value = config + n;
    if (peer_->refuses(e.key)) {
      // The knob: this repeater refuses this key, in the firmware's own words for it.
      std::snprintf(reply, cap, "%s", refusalFor(e.key));
      return;
    }
    if (std::strcmp(e.key, "dutycycle") == 0) {
      const double dc = std::atof(value);
      if (dc < 1 || dc > 100) {
        std::snprintf(reply, cap, "ERROR: dutycycle must be 1-100");
        return;
      }
      const double af = (100.0 / dc) - 1.0;
      const double actual = 100.0 / (af + 1.0);
      const int whole = static_cast<int>(actual);
      const int frac = static_cast<int>((actual - whole) * 10.0 + 0.5);
      char stored[16];
      std::snprintf(stored, sizeof stored, "%d.%d", whole, frac);
      peer_->setPref(e.key, stored);
      std::snprintf(reply, cap, "OK - %d.%d%%", whole, frac);
      return;
    }
    if (std::strcmp(e.key, "advert.interval") == 0) {
      const uint32_t mins = firmwareAtoi(value);
      if ((mins > 0 && mins < 60) || mins > 240) {  // MIN_LOCAL_ADVERT_INTERVAL is 60
        std::snprintf(reply, cap, "Error: interval range is 60-240 minutes");
        return;
      }
      char stored[16];
      std::snprintf(stored, sizeof stored, "%lu", static_cast<unsigned long>((mins / 2) * 2));
      peer_->setPref(e.key, stored);
      std::snprintf(reply, cap, "OK");
      return;
    }
    if (std::strcmp(e.key, "flood.advert.interval") == 0) {
      const uint32_t hours = firmwareAtoi(value);
      if ((hours > 0 && hours < 3) || hours > 168) {
        std::snprintf(reply, cap, "Error: interval range is 3-168 hours");
        return;
      }
      char stored[16];
      std::snprintf(stored, sizeof stored, "%lu", static_cast<unsigned long>(hours));
      peer_->setPref(e.key, stored);
      std::snprintf(reply, cap, "OK");
      return;
    }
    if (std::strcmp(e.key, "agc.reset.interval") == 0) {
      const uint32_t rounded = (firmwareAtoi(value) / 4) * 4;
      char stored[16];
      std::snprintf(stored, sizeof stored, "%lu", static_cast<unsigned long>(rounded));
      peer_->setPref(e.key, stored);
      std::snprintf(reply, cap, "OK - interval rounded to %lu", static_cast<unsigned long>(rounded));
      return;
    }
    if (std::strcmp(e.key, "flood.max") == 0 || std::strcmp(e.key, "flood.max.unscoped") == 0 ||
        std::strcmp(e.key, "flood.max.advert") == 0) {
      const int m = std::atoi(value);
      if (m > 64 || m < 0) {
        std::snprintf(reply, cap, "Error, max 64");
        return;
      }
      char stored[16];
      std::snprintf(stored, sizeof stored, "%d", m);
      peer_->setPref(e.key, stored);
      std::snprintf(reply, cap, "OK");
      return;
    }
    if (std::strcmp(e.key, "path.hash.mode") == 0) {
      const int mode = std::atoi(value);
      if (mode >= 3 || mode < 0) {
        std::snprintf(reply, cap, "Error, must be 0,1, or 2");
        return;
      }
      char stored[8];
      std::snprintf(stored, sizeof stored, "%d", mode);
      peer_->setPref(e.key, stored);
      std::snprintf(reply, cap, "OK");
      return;
    }
    if (std::strcmp(e.key, "repeat") == 0 || std::strcmp(e.key, "allow.read.only") == 0) {
      const bool on = std::strncmp(value, "on", 2) == 0;
      peer_->setPref(e.key, on ? "on" : "off");
      if (std::strcmp(e.key, "repeat") == 0) {
        std::snprintf(reply, cap, "OK - repeat is now %s", on ? "ON" : "OFF");
      } else {
        std::snprintf(reply, cap, "OK");
      }
      return;
    }
    if (std::strcmp(e.key, "name") == 0) {
      if (!isValidName(value)) {
        std::snprintf(reply, cap, "Error, bad chars");
        return;
      }
      peer_->setPref(e.key, value);
      std::snprintf(peer_->name, sizeof peer_->name, "%s", value);
      std::snprintf(reply, cap, "OK");
      return;
    }
    if (std::strcmp(e.key, "guest.password") == 0) {
      std::snprintf(peer_->guestPassword, sizeof peer_->guestPassword, "%s", value);
      peer_->setPref(e.key, peer_->guestPassword);
      std::snprintf(reply, cap, "OK");
      return;
    }
    if (std::strcmp(e.key, "lat") == 0 || std::strcmp(e.key, "lon") == 0) {
      peer_->setPref(e.key, value);
      std::snprintf(reply, cap, "OK");
      return;
    }
    // tx, multi.acks: `atoi` and "OK", with no range at all on this firmware.
    char stored[16];
    std::snprintf(stored, sizeof stored, "%d", std::atoi(value));
    peer_->setPref(e.key, stored);
    std::snprintf(reply, cap, "OK");
    return;
  }
  std::snprintf(reply, cap, "unknown config: %s", config);
}

void HostMesh::handleRegionCmd(const char* command, char* reply, size_t cap) {
  // `CommonCLI::handleRegionCmd` (CommonCLI.cpp:1040-1179). Only the forms the agent sends are
  // here; anything else is the firmware's own "Err - ??".
  const char* rest = command + 6;
  while (*rest == ' ') rest++;
  if (*rest == '\0') {
    std::snprintf(reply, cap, "%.*s", static_cast<int>(kMaxCliReplyLen), peer_->regionListing);
    return;
  }
  if (std::strncmp(rest, "default", 7) == 0) {
    const char* arg = rest + 7;
    while (*arg == ' ') arg++;
    if (*arg == '\0') {
      std::snprintf(reply, cap, " default scope is %s",
                    peer_->regionDefault[0] != '\0' ? peer_->regionDefault : "<null>");
      return;
    }
    std::snprintf(peer_->regionDefault, sizeof peer_->regionDefault, "%s", arg);
    std::snprintf(reply, cap, " default scope is now %s", peer_->regionDefault);
    return;
  }
  if (std::strncmp(rest, "home", 4) == 0) {
    const char* arg = rest + 4;
    while (*arg == ' ') arg++;
    if (*arg == '\0') {
      std::snprintf(reply, cap, " home is %s", peer_->regionHome[0] != '\0' ? peer_->regionHome : "*");
      return;
    }
    std::snprintf(peer_->regionHome, sizeof peer_->regionHome, "%s", arg);
    std::snprintf(reply, cap, " home is now %s", peer_->regionHome);
    return;
  }
  if (std::strncmp(rest, "load", 4) == 0) {
    // `startRegionsLoad()` and **no reply at all**: from here every CLI command this repeater
    // hears is a region line until a blank one arrives (MyMesh.cpp:1197-1230).
    regionLoadActive_ = true;
    regionLoadBuffer_[0] = '\0';
    regionLoadCount_ = 0;
    std::snprintf(regionLoadBuffer_, sizeof regionLoadBuffer_, "*\n");
    reply[0] = '\0';
    return;
  }
  if (std::strncmp(rest, "save", 4) == 0) {
    std::snprintf(reply, cap, "OK");
    return;
  }
  if (std::strncmp(rest, "allowf", 6) == 0 || std::strncmp(rest, "denyf", 5) == 0) {
    std::snprintf(reply, cap, "OK");
    return;
  }
  std::snprintf(reply, cap, "Err - ??");
}

}  // namespace mm
