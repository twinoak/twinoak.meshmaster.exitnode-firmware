#include "Connection.h"

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include <agent/Backoff.h>
#include <agent/Iso8601.h>

namespace mm {
namespace observer {

namespace {

// FNV-1a over a string, with a separator so that ("ab","c") and ("a","bc") differ: the hash of
// a slot's settings, compared on reconfigure to decide whether its connection has to start over.
uint32_t hashString(uint32_t h, const char* s) {
  for (; s != nullptr && *s != '\0'; s++) {
    h ^= static_cast<uint8_t>(*s);
    h *= 16777619u;
  }
  h ^= 0xFFu;
  h *= 16777619u;
  return h;
}

const char* rootName(bool bundle) { return bundle ? "the core's bundle" : "ISRG Root X1/X2"; }

const char* authName(Auth a) {
  switch (a) {
    case Auth::Jwt: return "token";
    case Auth::UserPass: return "username and password";
    case Auth::None: break;
  }
  return "no sign-in";
}

void copyText(char* out, size_t cap, const char* in) {
  if (out == nullptr || cap == 0) return;
  std::snprintf(out, cap, "%s", in != nullptr ? in : "");
}

void wipe(char* buf, size_t len) {
  volatile char* p = buf;
  for (size_t i = 0; i < len; i++) p[i] = 0;
}

// Wrap-safe "now is at or past `at`".
bool reached(uint32_t now, uint32_t at) { return static_cast<int32_t>(now - at) >= 0; }

}  // namespace

const char* slotStateName(SlotState s) {
  switch (s) {
    case SlotState::Off: return "off";
    case SlotState::WaitingForLink: return "waiting-for-wifi";
    case SlotState::WaitingForClock: return "waiting-for-clock";
    case SlotState::WaitingForHeap: return "waiting-for-heap";
    case SlotState::Connecting: return "connecting";
    case SlotState::Connected: return "connected";
    case SlotState::BackingOff: return "backing-off";
  }
  return "?";
}

// ---- wiring ------------------------------------------------------------------------------------

void Connections::begin(Client* const clients[kSlots], const Environment& env, Signer& signer, StatusSource& status,
                        ReportLine report, void* reportCtx) {
  for (int i = 0; i < kSlots; i++) clients_[i] = clients[i];
  env_ = env;
  signer_ = &signer;
  status_ = &status;
  report_ = report;
  reportCtx_ = reportCtx;
  for (Slot& s : slots_) s.state = SlotState::Off;
}

void Connections::report(LogLevel level, const char* fmt, ...) {
  if (report_ == nullptr) return;
  char line[200];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(line, sizeof line, fmt, args);
  va_end(args);
  report_(reportCtx_, level, line);
}

void Connections::enter(Slot& s, SlotState state) {
  s.state = state;
  s.sinceMillis = env_.millis != nullptr ? env_.millis() : 0;
  s.sinceEpoch = env_.epoch != nullptr ? env_.epoch() : 0;
}

// ---- configuration ----------------------------------------------------------------------------

void Connections::configure(const ObserverSettings& settings, bool enabled, const char* publicKeyHex,
                            const char* clientVersion) {
  enabled_ = enabled;
  size_t n = 0;
  for (; publicKeyHex != nullptr && publicKeyHex[n] != '\0' && n < kPublicKeyHexLen; n++) {
    publicKeyHex_[n] = static_cast<char>(std::toupper(static_cast<unsigned char>(publicKeyHex[n])));
  }
  publicKeyHex_[n] = '\0';
  copyText(owner_, sizeof owner_, settings.owner);
  copyText(email_, sizeof email_, settings.email);
  copyText(clientVersion_, sizeof clientVersion_, clientVersion);

  for (int i = 0; i < kSlots; i++) {
    Slot& s = slots_[i];
    ResolvedSlot r;
    if (enabled) {
      r = resolveSlot(settings.slot[i]);
    } else {
      r.why = "the observer is off";
    }
    char topicStatus[kTopicCap] = {0};
    char topicPackets[kTopicCap] = {0};
    if (r.active) {
      const bool ok = buildTopic(r.topic, settings.iata, publicKeyHex_, MessageType::Status, topicStatus, sizeof topicStatus) &&
                      buildTopic(r.topic, settings.iata, publicKeyHex_, MessageType::Packets, topicPackets, sizeof topicPackets);
      if (!ok) {
        r.active = false;
        r.why = "the topic cannot be built";
      }
    }
    // What the connection is made of: a change to any of it means a new session.
    uint32_t hash = 2166136261u;
    hash = hashString(hash, r.active ? "on" : "off");
    hash = hashString(hash, r.name);
    hash = hashString(hash, r.serverUrl);
    hash = hashString(hash, authName(r.auth));
    hash = hashString(hash, r.audience);
    hash = hashString(hash, r.username);
    hash = hashString(hash, r.password);
    hash = hashString(hash, topicStatus);
    hash = hashString(hash, topicPackets);
    hash = hashString(hash, r.auth == Auth::Jwt ? owner_ : "");
    hash = hashString(hash, r.auth == Auth::Jwt ? email_ : "");
    hash = hashString(hash, r.allowRetain ? "retain" : "");

    const bool first = s.configHash == 0;
    const bool changed = hash != s.configHash;
    s.cfg = r;  // the pointers may have moved even when the text has not
    std::memcpy(s.topicStatus, topicStatus, sizeof topicStatus);
    std::memcpy(s.topicPackets, topicPackets, sizeof topicPackets);
    if (!changed) continue;
    s.configHash = hash;

    if (s.state == SlotState::Connecting || s.state == SlotState::Connected) {
      clients_[i]->close();
      report(LogLevel::Info, "observer: slot %d: settings changed, the connection starts over", i + 1);
    }
    s.backoffMs = 0;
    s.holdUntil = 0;
    s.lastHeapCheckAt = 0;
    s.useBundle = false;
    s.tokenExpiresAt = 0;
    s.statusPending = false;
    s.lastError[0] = '\0';
    s.queueSize = 0;
    if (r.active) {
      enter(s, SlotState::WaitingForLink);
      report(LogLevel::Info, "observer: slot %d %s: %s, %s, topics %s", i + 1, r.name, r.serverUrl, authName(r.auth),
             s.topicPackets);
    } else {
      enter(s, SlotState::Off);
      if (!first || enabled) report(LogLevel::Info, "observer: slot %d off (%s)", i + 1, r.why);
    }
  }
}

void Connections::setLinkUp(bool up) { linkUp_ = up; }

void Connections::stopAll() {
  for (int i = 0; i < kSlots; i++) {
    Slot& s = slots_[i];
    if (s.state == SlotState::Connecting || s.state == SlotState::Connected) clients_[i]->close();
    s.queueSize = 0;
    s.configHash = 0;
    enter(s, SlotState::Off);
  }
}

// ---- the queue -------------------------------------------------------------------------------

void Connections::queuePush(Slot& s, const char* json, size_t len) {
  const size_t need = len + 2;
  if (need > kQueueBytes || len > 0xFFFF) {
    s.stats.dropped++;
    return;
  }
  while (s.queueSize + need > kQueueBytes) {
    queuePop(s);
    s.stats.dropped++;
  }
  s.queue[s.queueSize] = static_cast<uint8_t>(len & 0xFF);
  s.queue[s.queueSize + 1] = static_cast<uint8_t>(len >> 8);
  std::memcpy(s.queue + s.queueSize + 2, json, len);
  s.queueSize += need;
}

void Connections::queuePop(Slot& s) {
  if (s.queueSize < 2) {
    s.queueSize = 0;
    return;
  }
  const size_t len = static_cast<size_t>(s.queue[0]) | (static_cast<size_t>(s.queue[1]) << 8);
  const size_t total = len + 2;
  if (total >= s.queueSize) {
    s.queueSize = 0;
    return;
  }
  std::memmove(s.queue, s.queue + total, s.queueSize - total);
  s.queueSize -= total;
}

void Connections::onPacketMessage(const char* json, size_t len) {
  if (json == nullptr || len == 0) return;
  for (Slot& s : slots_) {
    if (s.state == SlotState::Off) continue;
    if (s.state != SlotState::Connected) {
      s.stats.dropped++;  // not connected: dropped, not hoarded
      continue;
    }
    queuePush(s, json, len);
  }
}

// ---- memory -----------------------------------------------------------------------------------

bool Connections::heapAllows(Slot& s) {
  const uint32_t free = env_.freeHeap != nullptr ? env_.freeHeap() : 0xFFFFFFFFu;
  const uint32_t largest = env_.largestBlock != nullptr ? env_.largestBlock() : 0xFFFFFFFFu;
  if (free >= kHeapReserveFree && largest >= kHeapReserveLargest) return true;
  std::snprintf(s.lastError, sizeof s.lastError, "heap %lu free, largest block %lu; needs %lu and %lu",
                static_cast<unsigned long>(free), static_cast<unsigned long>(largest),
                static_cast<unsigned long>(kHeapReserveFree), static_cast<unsigned long>(kHeapReserveLargest));
  return false;
}

void Connections::onUplinkMemoryFailure() {
  const uint32_t now = env_.millis();
  for (int i = kSlots - 1; i >= 0; i--) {
    Slot& s = slots_[i];
    if (s.state != SlotState::Connected && s.state != SlotState::Connecting) continue;
    clients_[i]->close();
    s.stats.heapYields++;
    s.holdUntil = now + kHeapYieldMs;
    s.lastHeapCheckAt = now;
    copyText(s.lastError, sizeof s.lastError, "gave way to the uplink, whose call failed for want of memory");
    enter(s, SlotState::WaitingForHeap);
    report(LogLevel::Warn, "observer: slot %d %s: closed for %lu min, the MeshMaster call failed for want of memory (%lu time(s))",
           i + 1, s.cfg.name, static_cast<unsigned long>(kHeapYieldMs / 60000), static_cast<unsigned long>(s.stats.heapYields));
    return;
  }
}

// ---- the token -------------------------------------------------------------------------------

bool Connections::makeSlotToken(int i, char* out, size_t cap) {
  Slot& s = slots_[i];
  const uint32_t now = env_.epoch();
  TokenClaims claims;
  claims.publicKeyHex = publicKeyHex_;
  claims.audience = s.cfg.audience;
  claims.issuedAt = now;
  claims.expiresIn = tokenLifetime(s.cfg.tokenLifetime, static_cast<uint8_t>(i));
  claims.owner = owner_;
  claims.client = clientVersion_;
  claims.email = email_;
  if (!makeToken(claims, *signer_, out, cap)) {
    s.tokenExpiresAt = 0;
    return false;
  }
  s.tokenExpiresAt = now + claims.expiresIn;
  return true;
}

// ---- opening and closing ---------------------------------------------------------------------

void Connections::tryToOpen(int i) {
  Slot& s = slots_[i];
  const uint32_t now = env_.millis();
  if (!linkUp_) {
    if (s.state != SlotState::WaitingForLink) enter(s, SlotState::WaitingForLink);
    return;
  }
  if (env_.epoch() == 0) {
    if (s.state != SlotState::WaitingForClock) {
      enter(s, SlotState::WaitingForClock);
      report(LogLevel::Info, "observer: slot %d %s: waiting for the clock", i + 1, s.cfg.name);
    }
    return;
  }
  // One handshake at a time on this board: the other slot's first.
  for (int j = 0; j < kSlots; j++) {
    if (j != i && slots_[j].state == SlotState::Connecting) {
      if (s.state != SlotState::WaitingForHeap) enter(s, SlotState::WaitingForHeap);
      copyText(s.lastError, sizeof s.lastError, "waiting for the other slot's handshake");
      s.lastHeapCheckAt = now;
      return;
    }
  }
  if (!heapAllows(s)) {
    if (s.state != SlotState::WaitingForHeap) {
      enter(s, SlotState::WaitingForHeap);
      report(LogLevel::Warn, "observer: slot %d %s: waiting for heap (%s)", i + 1, s.cfg.name, s.lastError);
    }
    s.lastHeapCheckAt = now;
    return;
  }

  ClientConfig cfg;
  cfg.uri = s.cfg.serverUrl;
  cfg.caPem = s.useBundle ? nullptr : env_.caPem;
  cfg.keepaliveS = kKeepaliveS;
  if (s.cfg.auth == Auth::Jwt) {
    if (!makeSlotToken(i, token_, sizeof token_)) {
      wipe(token_, sizeof token_);
      closeAndBackOff(i, "the token could not be made", false);
      return;
    }
    usernameFor(publicKeyHex_, username_, sizeof username_);
    cfg.username = username_;
    cfg.password = token_;
  } else if (s.cfg.auth == Auth::UserPass) {
    cfg.username = s.cfg.username;
    cfg.password = s.cfg.password;
  }
  const size_t willLen = status_->formatStatus("offline", status_scratch_, sizeof status_scratch_);
  if (willLen > 0) {
    cfg.willTopic = s.topicStatus;
    cfg.willPayload = status_scratch_;
    cfg.willLen = willLen;
    cfg.willRetain = s.cfg.allowRetain;
  }

  char reason[kReasonCap] = {0};
  const bool opened = clients_[i]->open(cfg, reason, sizeof reason);
  wipe(token_, sizeof token_);  // the client has its copy; this one is not kept
  if (!opened) {
    closeAndBackOff(i, reason[0] != '\0' ? reason : "the client could not be started", false);
    return;
  }
  s.attemptStartedAt = now;
  s.lastError[0] = '\0';
  enter(s, SlotState::Connecting);
  report(LogLevel::Info, "observer: slot %d %s: connecting to %s with %s, root %s", i + 1, s.cfg.name, s.cfg.serverUrl,
         authName(s.cfg.auth), rootName(s.useBundle));
}

void Connections::closeAndBackOff(int i, const char* why, bool refused) {
  Slot& s = slots_[i];
  clients_[i]->close();
  if (refused) {
    s.stats.refusals++;
  } else {
    s.stats.errors++;
  }
  copyText(s.lastError, sizeof s.lastError, why);
  s.backoffMs = nextBackoffMs(s.backoffMs, kFirstBackoffMs, kMaxBackoffMs);
  const uint32_t wait = withJitter(s.backoffMs, env_.random != nullptr ? env_.random() : 0);
  s.nextAttemptAt = env_.millis() + wait;
  enter(s, SlotState::BackingOff);
  report(LogLevel::Warn, "observer: slot %d %s: %s%s; next attempt in %lu s (%lu refusal(s), %lu error(s))", i + 1, s.cfg.name,
         refused ? "sign-in refused: " : "", why, static_cast<unsigned long>((wait + 999) / 1000),
         static_cast<unsigned long>(s.stats.refusals), static_cast<unsigned long>(s.stats.errors));
}

void Connections::pollEvents(int i) {
  Slot& s = slots_[i];
  ErrorKind kind = ErrorKind::Other;
  char reason[kReasonCap] = {0};
  const ClientEvent e = clients_[i]->takeEvent(kind, reason, sizeof reason);
  switch (e) {
    case ClientEvent::None:
      return;
    case ClientEvent::Connected:
      if (s.state != SlotState::Connecting) return;
      s.stats.connects++;
      s.connectedAt = env_.millis();
      s.statusPending = true;
      s.lastError[0] = '\0';
      enter(s, SlotState::Connected);
      report(LogLevel::Info, "observer: slot %d %s: connected (root %s; %lu connects)", i + 1, s.cfg.name, rootName(s.useBundle),
             static_cast<unsigned long>(s.stats.connects));
      return;
    case ClientEvent::Disconnected:
      closeAndBackOff(i, s.state == SlotState::Connected ? "connection lost" : "closed before the broker answered", false);
      return;
    case ClientEvent::Refused:
      closeAndBackOff(i, reason[0] != '\0' ? reason : "the broker refused the sign-in", true);
      return;
    case ClientEvent::Error:
      if (kind == ErrorKind::ChainNotVerified && !s.useBundle) {
        // The chain did not end at ISRG Root X1 or X2. Try the core's own bundle - the Mozilla
        // roots - once, and remember: verification both times, a bypass neither
        // (src/agent/AgentClient.cpp does the same for meshmaster.dk).
        s.useBundle = true;
        s.stats.errors++;
        clients_[i]->close();
        report(LogLevel::Warn, "observer: slot %d %s: the certificate chain did not end at ISRG Root X1/X2; trying %s", i + 1,
               s.cfg.name, rootName(true));
        tryToOpen(i);
        return;
      }
      if (kind == ErrorKind::ChainNotVerified) {
        closeAndBackOff(i, "refused: the certificate chain ends at no root this firmware carries nor the core's bundle", false);
        return;
      }
      if (kind == ErrorKind::NoMemory) {
        clients_[i]->close();
        s.stats.errors++;
        copyText(s.lastError, sizeof s.lastError, reason[0] != '\0' ? reason : "no memory for the connection");
        s.lastHeapCheckAt = env_.millis();
        enter(s, SlotState::WaitingForHeap);
        report(LogLevel::Warn, "observer: slot %d %s: %s; waiting for heap", i + 1, s.cfg.name, s.lastError);
        return;
      }
      closeAndBackOff(i, reason[0] != '\0' ? reason : "transport error", false);
      return;
  }
}

// ---- publishing -------------------------------------------------------------------------------

void Connections::publishStatus(int i) {
  Slot& s = slots_[i];
  const uint32_t now = env_.millis();
  const size_t len = status_->formatStatus("online", status_scratch_, sizeof status_scratch_);
  if (len == 0) {
    s.statusPending = false;
    s.statusDueAt = now + kStatusIntervalMs;
    return;
  }
  if (clients_[i]->outboxBytes() + len > kOutboxBytes) {
    s.statusPending = true;
    return;
  }
  if (clients_[i]->publish(s.topicStatus, status_scratch_, len, 1, s.cfg.allowRetain)) {
    s.stats.published++;
    s.statusPending = false;
    s.statusDueAt = now + kStatusIntervalMs;
  } else {
    s.statusPending = true;
  }
}

void Connections::pumpQueue(int i) {
  Slot& s = slots_[i];
  // A few per pass: each hand-over is a copy into the client's outbox, and the mesh loop is
  // waiting its turn.
  for (int n = 0; n < 4 && s.queueSize >= 2; n++) {
    const size_t len = static_cast<size_t>(s.queue[0]) | (static_cast<size_t>(s.queue[1]) << 8);
    if (len + 2 > s.queueSize) {
      s.queueSize = 0;
      return;
    }
    if (clients_[i]->outboxBytes() + len > kOutboxBytes) return;
    if (!clients_[i]->publish(s.topicPackets, reinterpret_cast<const char*>(s.queue + 2), len, 0, false)) return;
    s.stats.published++;
    queuePop(s);
  }
}

// ---- the loop ---------------------------------------------------------------------------------

void Connections::loop() {
  const uint32_t now = env_.millis();
  for (int i = 0; i < kSlots; i++) {
    Slot& s = slots_[i];
    if (s.state == SlotState::Off) continue;

    if (!linkUp_) {
      if (s.state == SlotState::Connecting || s.state == SlotState::Connected) {
        clients_[i]->close();
        report(LogLevel::Warn, "observer: slot %d %s: wifi is down, connection closed", i + 1, s.cfg.name);
      }
      if (s.state != SlotState::WaitingForLink) enter(s, SlotState::WaitingForLink);
      continue;
    }

    switch (s.state) {
      case SlotState::Off:
        break;
      case SlotState::WaitingForLink:
        tryToOpen(i);
        break;
      case SlotState::WaitingForClock:
        if (env_.epoch() != 0) tryToOpen(i);
        break;
      case SlotState::WaitingForHeap:
        if (s.holdUntil != 0 && !reached(now, s.holdUntil)) break;
        s.holdUntil = 0;
        if (s.lastHeapCheckAt == 0 || now - s.lastHeapCheckAt >= kHeapRecheckMs) tryToOpen(i);
        break;
      case SlotState::BackingOff:
        if (reached(now, s.nextAttemptAt)) tryToOpen(i);
        break;
      case SlotState::Connecting:
        pollEvents(i);
        if (s.state == SlotState::Connecting && now - s.attemptStartedAt > kConnectTimeoutMs) {
          closeAndBackOff(i, "no answer from the broker in 60 s", false);
        } else if (s.state == SlotState::Connected) {
          serveConnected(i);  // the status at connect goes out in the same pass the broker said yes
        }
        break;
      case SlotState::Connected:
        pollEvents(i);
        if (s.state == SlotState::Connected) serveConnected(i);
        break;
    }
  }
}

void Connections::serveConnected(int i) {
  Slot& s = slots_[i];
  const uint32_t now = env_.millis();
  if (s.backoffMs != 0 && now - s.connectedAt >= kStableMs) s.backoffMs = 0;
  if (s.cfg.auth == Auth::Jwt) {
    const uint32_t epoch = env_.epoch();
    const uint32_t lifetime = tokenLifetime(s.cfg.tokenLifetime, static_cast<uint8_t>(i));
    if (tokenNeedsRenewal(epoch != 0, epoch, s.tokenExpiresAt, renewalBuffer(lifetime))) {
      s.stats.renewals++;
      clients_[i]->close();
      report(LogLevel::Info, "observer: slot %d %s: the token is renewed before it expires (%lu renewal(s))", i + 1, s.cfg.name,
             static_cast<unsigned long>(s.stats.renewals));
      tryToOpen(i);
      return;
    }
  }
  if (s.statusPending || reached(now, s.statusDueAt)) publishStatus(i);
  pumpQueue(i);
}

// ---- reading it back --------------------------------------------------------------------------

int Connections::configuredSlots() const {
  int n = 0;
  for (const Slot& s : slots_) {
    if (s.state != SlotState::Off) n++;
  }
  return n;
}

int Connections::connectedSlots() const {
  int n = 0;
  for (const Slot& s : slots_) {
    if (s.state == SlotState::Connected) n++;
  }
  return n;
}

uint32_t Connections::connectFailures() const {
  uint32_t n = 0;
  for (const Slot& s : slots_) n += s.stats.refusals + s.stats.errors;
  return n;
}

SlotView Connections::view(int slot) const {
  SlotView v;
  if (slot < 0 || slot >= kSlots) return v;
  const Slot& s = slots_[slot];
  const uint32_t now = env_.millis != nullptr ? env_.millis() : 0;
  v.configured = s.state != SlotState::Off;
  v.name = s.cfg.name;
  v.host = s.cfg.serverUrl;
  v.state = s.state;
  v.sinceMillis = s.sinceMillis;
  v.sinceEpoch = s.sinceEpoch;
  v.root = rootName(s.useBundle);
  v.lastError = s.lastError;
  v.queued = s.queueSize;
  v.stats = s.stats;
  if (s.state == SlotState::BackingOff) {
    v.waitMs = reached(now, s.nextAttemptAt) ? 0 : s.nextAttemptAt - now;
  } else if (s.state == SlotState::WaitingForHeap) {
    if (s.holdUntil != 0 && !reached(now, s.holdUntil)) {
      v.waitMs = s.holdUntil - now;
    } else {
      const uint32_t next = s.lastHeapCheckAt + kHeapRecheckMs;
      v.waitMs = reached(now, next) ? 0 : next - now;
    }
  }
  return v;
}

void Connections::describe(int slot, char* out, size_t cap) const {
  if (out == nullptr || cap == 0) return;
  if (slot < 0 || slot >= kSlots) {
    out[0] = '\0';
    return;
  }
  const Slot& s = slots_[slot];
  const SlotView v = view(slot);
  if (!v.configured) {
    std::snprintf(out, cap, "slot %d %s: off (%s)", slot + 1, s.cfg.name, s.cfg.why);
    return;
  }
  char since[24];
  if (v.sinceEpoch != 0) {
    formatClock(v.sinceEpoch, since, sizeof since);
  } else {
    std::snprintf(since, sizeof since, "+%lus", static_cast<unsigned long>(v.sinceMillis / 1000));
  }
  char state[128];
  switch (v.state) {
    case SlotState::Connected:
      std::snprintf(state, sizeof state, "connected since %s, root %s", since, v.root);
      break;
    case SlotState::Connecting:
      std::snprintf(state, sizeof state, "connecting since %s, root %s", since, v.root);
      break;
    case SlotState::BackingOff:
      std::snprintf(state, sizeof state, "backing off since %s, next attempt in %lu s", since,
                    static_cast<unsigned long>((v.waitMs + 999) / 1000));
      break;
    case SlotState::WaitingForHeap:
      std::snprintf(state, sizeof state, "waiting for heap since %s, next look in %lu s", since,
                    static_cast<unsigned long>((v.waitMs + 999) / 1000));
      break;
    case SlotState::WaitingForClock:
      std::snprintf(state, sizeof state, "waiting for the clock since %s", since);
      break;
    case SlotState::WaitingForLink:
      std::snprintf(state, sizeof state, "waiting for wifi since %s", since);
      break;
    case SlotState::Off:
    default:
      std::snprintf(state, sizeof state, "off");
      break;
  }
  std::snprintf(out, cap, "slot %d %s (%s): %s; %lu published, %lu dropped, %u queued; %lu connects, %lu refusal(s), %lu error(s), %lu renewal(s), gave way %lu time(s); last error: %s",
                slot + 1, v.name, v.host, state, static_cast<unsigned long>(v.stats.published),
                static_cast<unsigned long>(v.stats.dropped), static_cast<unsigned>(v.queued),
                static_cast<unsigned long>(v.stats.connects), static_cast<unsigned long>(v.stats.refusals),
                static_cast<unsigned long>(v.stats.errors), static_cast<unsigned long>(v.stats.renewals),
                static_cast<unsigned long>(v.stats.heapYields), v.lastError[0] != '\0' ? v.lastError : "none");
}

}  // namespace observer
}  // namespace mm
