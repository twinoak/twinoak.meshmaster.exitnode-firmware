#include "Neighbours.h"

#include <cstdio>
#include <cstring>

#include <agent/Envelopes.h>  // validUtf8PrefixLength

namespace mm {
namespace observer {

namespace {

// Wrap-safe "now is at or past `at`" and "a is newer than b" on the uptime clock.
bool reached(uint32_t now, uint32_t at) { return static_cast<int32_t>(now - at) >= 0; }
bool newer(uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b) > 0; }

// "23h 59m", "4m 58s", "12 s": how far away or how long ago, for the `nbr:` line.
void formatSpan(uint32_t ms, char* out, size_t cap) {
  const uint32_t s = ms / 1000;
  if (s >= 3600) {
    std::snprintf(out, cap, "%luh %lum", static_cast<unsigned long>(s / 3600), static_cast<unsigned long>((s % 3600) / 60));
  } else if (s >= 60) {
    std::snprintf(out, cap, "%lum %lus", static_cast<unsigned long>(s / 60), static_cast<unsigned long>(s % 60));
  } else {
    std::snprintf(out, cap, "%lu s", static_cast<unsigned long>(s));
  }
}

}  // namespace

// ---- the table ---------------------------------------------------------------------------------

bool NeighbourTable::offer(const uint8_t key[kNeighbourKeyLen], uint8_t advType, uint8_t hops, int8_t snrQ4, int16_t rssi,
                           uint32_t nowMs) {
  // MyMesh.cpp:788-791: a zero-hop advert of a repeater, and nothing else.
  if (advType != kAdvTypeRepeater || hops != 0 || key == nullptr) return false;
  put(key, snrQ4, rssi, nowMs);
  return true;
}

void NeighbourTable::put(const uint8_t key[kNeighbourKeyLen], int8_t snrQ4, int16_t rssi, uint32_t nowMs) {
  if (key == nullptr) return;
  // putNeighbour (MyMesh.cpp:86-109): the entry already known, else an empty one, else the one
  // heard longest ago.
  Neighbour* slot = nullptr;
  Neighbour* oldest = nullptr;
  for (Neighbour& n : entries_) {
    if (n.used && std::memcmp(n.key, key, kNeighbourKeyLen) == 0) {
      slot = &n;
      break;
    }
    if (!n.used) {
      if (slot == nullptr) slot = &n;
      continue;
    }
    if (oldest == nullptr || newer(oldest->heardAtMs, n.heardAtMs)) oldest = &n;
  }
  if (slot == nullptr) slot = oldest;
  if (slot == nullptr) return;
  std::memcpy(slot->key, key, kNeighbourKeyLen);
  slot->snrQ4 = snrQ4;
  slot->rssi = rssi;
  slot->heardAtMs = nowMs;
  slot->used = true;
}

void NeighbourTable::clear() {
  for (Neighbour& n : entries_) n = Neighbour();
}

int NeighbourTable::count() const {
  int n = 0;
  for (const Neighbour& e : entries_) {
    if (e.used) n++;
  }
  return n;
}

const Neighbour* NeighbourTable::find(const uint8_t key[kNeighbourKeyLen]) const {
  if (key == nullptr) return nullptr;
  for (const Neighbour& n : entries_) {
    if (n.used && std::memcmp(n.key, key, kNeighbourKeyLen) == 0) return &n;
  }
  return nullptr;
}

// ---- the bytes ---------------------------------------------------------------------------------

size_t discoveryRequest(uint32_t tag, uint8_t out[kDiscoveryRequestLen]) {
  // sendNodeDiscoverReq (MyMesh.cpp:1000-1013): prefix_only 0 in the type byte, the filter bit
  // for repeaters, the tag, and `since` 0 - every repeater answers, whatever its discovery
  // timestamp.
  out[0] = kCtlNodeDiscoverReq;
  out[1] = static_cast<uint8_t>(1u << kAdvTypeRepeater);
  std::memcpy(&out[2], &tag, 4);
  std::memset(&out[6], 0, 4);
  return kDiscoveryRequestLen;
}

bool readDiscoveryResponse(const uint8_t* payload, size_t len, uint32_t tag, uint8_t key[kNeighbourKeyLen]) {
  // onControlDataRecv (MyMesh.cpp:972-991): the type nibble, a repeater's node type in the low
  // nibble, the full key (a prefix-only answer is too short and is not taken), and this tag.
  if (payload == nullptr || len < 6 + kNeighbourKeyLen) return false;
  if ((payload[0] & 0xF0) != kCtlNodeDiscoverResp || (payload[0] & 0x0F) != kAdvTypeRepeater) return false;
  uint32_t theirs = 0;
  std::memcpy(&theirs, &payload[2], 4);
  if (theirs != tag) return false;
  if (key != nullptr) std::memcpy(key, &payload[6], kNeighbourKeyLen);
  return true;
}

size_t regionsRequest(uint32_t tag, uint8_t out[kRegionsRequestLen]) {
  std::memcpy(out, &tag, 4);
  out[4] = kAnonReqTypeRegions;
  out[5] = 0x00;  // a zero-length reply path: a zero-hop reply (MyMesh.cpp:1971)
  return kRegionsRequestLen;
}

size_t readRegionsReply(const uint8_t* data, size_t len, char* scopes, size_t cap) {
  if (scopes == nullptr || cap == 0) return 0;
  scopes[0] = '\0';
  // The reply's data after its tag: the repeater's clock (four bytes), then the names. A reply
  // shorter than the clock has no names (handleNeighborDiscoverResponse, :2076-2086, reads the
  // same prefix).
  if (data == nullptr || len < 4) return 0;
  const char* names = reinterpret_cast<const char*>(data + 4);
  size_t n = len - 4;
  if (n > kScopesCap) n = kScopesCap;
  if (n > cap - 1) n = cap - 1;
  // The datagram was padded to its cipher block with zeros: the names end at the first NUL.
  size_t end = 0;
  while (end < n && names[end] != '\0') end++;
  const size_t valid = validUtf8PrefixLength(names, end);
  std::memcpy(scopes, names, valid);
  scopes[valid] = '\0';
  return valid;
}

const char* queryStatusName(QueryStatus s) {
  switch (s) {
    case QueryStatus::Responded: return "responded";
    case QueryStatus::Timeout: return "timeout";
    case QueryStatus::SendFailed: return "send_failed";
    case QueryStatus::Unsent: break;
  }
  return "";
}

const char* reportResultName(ReportResult r) {
  switch (r) {
    case ReportResult::Ok: return "ok";
    case ReportResult::SkippedHeap: return "skipped (heap)";
    case ReportResult::Failed: return "failed";
    case ReportResult::NoSlot: return "no slot";
    case ReportResult::None: break;
  }
  return "none yet";
}

// ---- the sweep ---------------------------------------------------------------------------------

void NeighbourSweep::begin(NeighbourTable& table, NeighbourLink& link, AirtimeGate* gate, uint32_t (*millis)()) {
  table_ = &table;
  link_ = &link;
  gate_ = gate;
  millis_ = millis;
  phase_ = SweepPhase::Idle;
}

void NeighbourSweep::configure(const SweepSettings& s) {
  const uint32_t now = millis_ != nullptr ? millis_() : 0;
  const bool wasEnabled = settings_.enabled;
  settings_ = s;
  if (s.intervalHours < 12) settings_.intervalHours = 12;
  if (s.intervalHours > 336) settings_.intervalHours = 336;
  if (!settings_.enabled) {
    // Off: no schedule, and a periodic cycle that is running is dropped where it stands; a
    // manual one (discover.scopes) runs to its end, because somebody asked for it.
    nextDueAt_ = 0;
    scheduled_ = false;
    if (phase_ != SweepPhase::Idle && !manual_) {
      phase_ = SweepPhase::Idle;
      snapshotCount_ = 0;
    }
    return;
  }
  if (!wasEnabled || !scheduled_) {
    // Just switched on (or the first configure): the first cycle after a settling delay, so the
    // table has heard a round of adverts and the slots have had their handshakes.
    nextDueAt_ = now + kFirstCycleDelayMs;
    scheduled_ = true;
  } else if (lastCycleEndAt_ != 0) {
    // A new interval counts from the last cycle, not from now.
    nextDueAt_ = lastCycleEndAt_ + settings_.intervalHours * 3600000u;
  }
}

bool NeighbourSweep::requestNow() {
  if (phase_ != SweepPhase::Idle || wantNow_) return false;
  wantNow_ = true;
  return true;
}

void NeighbourSweep::startCycle(bool manual) {
  manual_ = manual;
  phase_ = SweepPhase::Discovering;
  discoverySent_ = false;
  windowEndsAt_ = 0;
  snapshotCount_ = 0;
  queried_ = 0;
  responded_ = 0;
  next_ = 0;
}

void NeighbourSweep::takeSnapshot() {
  // startNeighborDiscover (MyMesh.cpp:2298-2323): the table as it stands, newest first, then the
  // stronger SNR, then the key - so that when the report is cut, it is cut at the least useful end.
  snapshotCount_ = 0;
  for (int i = 0; i < kNeighbourCap; i++) {
    const Neighbour& n = table_->at(i);
    if (!n.used) continue;
    SweepEntry& e = snapshot_[snapshotCount_++];
    e = SweepEntry();
    std::memcpy(e.key, n.key, kNeighbourKeyLen);
    e.snrQ4 = n.snrQ4;
    e.heardAtMs = n.heardAtMs;
  }
  for (int i = 1; i < snapshotCount_; i++) {
    SweepEntry e = snapshot_[i];
    int j = i;
    while (j > 0) {
      const SweepEntry& r = snapshot_[j - 1];
      const bool before = newer(e.heardAtMs, r.heardAtMs) ||
                          (e.heardAtMs == r.heardAtMs && e.snrQ4 > r.snrQ4) ||
                          (e.heardAtMs == r.heardAtMs && e.snrQ4 == r.snrQ4 && std::memcmp(e.key, r.key, kNeighbourKeyLen) < 0);
      if (!before) break;
      snapshot_[j] = snapshot_[j - 1];
      j--;
    }
    snapshot_[j] = e;
  }
}

void NeighbourSweep::finishCycle() {
  const uint32_t now = millis_();
  phase_ = SweepPhase::Idle;
  cycles_++;
  lastCycleEndAt_ = now == 0 ? 1 : now;
  if (settings_.enabled) {
    nextDueAt_ = now + settings_.intervalHours * 3600000u;
    scheduled_ = true;
  }
  manual_ = false;
}

bool NeighbourSweep::loop(bool slotConnected) {
  if (table_ == nullptr || link_ == nullptr || millis_ == nullptr) return false;
  const uint32_t now = millis_();
  switch (phase_) {
    case SweepPhase::Idle:
      if (wantNow_) {
        wantNow_ = false;
        startCycle(true);
        return false;
      }
      if (settings_.enabled && scheduled_ && reached(now, nextDueAt_) && slotConnected) startCycle(false);
      return false;

    case SweepPhase::Discovering:
      if (!discoverySent_) {
        // The one request of this phase: not while a job is open, and only when the airtime
        // budget has a slot - the same gate every job's transaction goes through.
        if (link_->busy()) return false;
        if (gate_ != nullptr && gate_->waitMs(now) > 0) return false;
        if (gate_ != nullptr) gate_->took(now);
        if (!link_->sendDiscovery(kDiscoveryWindowMs)) discoveryFailures_++;
        // A request that could not be made still opens the window: the table has what the
        // adverts put in it, and that is a report worth sending.
        discoverySent_ = true;
        windowEndsAt_ = now + kDiscoveryWindowMs;
        return false;
      }
      if (!reached(now, windowEndsAt_)) return false;
      takeSnapshot();
      phase_ = SweepPhase::Querying;
      next_ = 0;
      return false;

    case SweepPhase::Querying: {
      // Nothing while a job is open - not a query, and not the cycle's end either: the report is
      // built by the caller when this returns true, and that is the main loop's work, not a
      // job's pump's.
      if (link_->busy()) return false;
      if (next_ >= snapshotCount_) {
        finishCycle();
        return true;
      }
      if (gate_ != nullptr && gate_->waitMs(now) > 0) return false;
      if (gate_ != nullptr) gate_->took(now);
      SweepEntry& e = snapshot_[next_];
      e.status = link_->queryRegions(e.key, e.scopes, sizeof e.scopes);
      if (e.status != QueryStatus::SendFailed) queried_++;  // :591, "requests confirmed transmitted"
      if (e.status == QueryStatus::Responded) {
        responded_++;
        // A zero-hop reply is proof the neighbour is heard now (:2082-2084): the table says so.
        if (const Neighbour* n = table_->find(e.key)) table_->put(e.key, n->snrQ4, n->rssi, millis_());
      }
      next_++;
      return false;
    }
  }
  return false;
}

void NeighbourSweep::noteReport(ReportResult r) {
  lastReport_ = r;
  lastReportAtMs_ = millis_ != nullptr ? millis_() : 0;
}

void NeighbourSweep::describe(char* out, size_t cap) const {
  if (out == nullptr || cap == 0) return;
  const uint32_t now = millis_ != nullptr ? millis_() : 0;
  char schedule[64];
  char span[24];
  switch (phase_) {
    case SweepPhase::Discovering:
      if (!discoverySent_) {
        std::snprintf(schedule, sizeof schedule, "running (discovery, waiting for the air)");
      } else {
        formatSpan(reached(now, windowEndsAt_) ? 0 : windowEndsAt_ - now, span, sizeof span);
        std::snprintf(schedule, sizeof schedule, "running (discovery, %s left)", span);
      }
      break;
    case SweepPhase::Querying:
      std::snprintf(schedule, sizeof schedule, "running (query %d of %d)", next_ + 1 > snapshotCount_ ? snapshotCount_ : next_ + 1,
                    snapshotCount_);
      break;
    case SweepPhase::Idle:
    default:
      if (!settings_.enabled) {
        std::snprintf(schedule, sizeof schedule, "off (mqtt.neighbors off)");
      } else if (!scheduled_) {
        std::snprintf(schedule, sizeof schedule, "not scheduled");
      } else if (reached(now, nextDueAt_)) {
        std::snprintf(schedule, sizeof schedule, "due, waiting for a slot");
      } else {
        formatSpan(nextDueAt_ - now, span, sizeof span);
        std::snprintf(schedule, sizeof schedule, "next in %s", span);
      }
      break;
  }
  char last[48];
  if (lastReport_ == ReportResult::None) {
    std::snprintf(last, sizeof last, "%s", reportResultName(lastReport_));
  } else {
    formatSpan(now - lastReportAtMs_, span, sizeof span);
    std::snprintf(last, sizeof last, "%s %s ago", reportResultName(lastReport_), span);
  }
  std::snprintf(out, cap, "nbr: %s, last %s, %d neighbours, %d responded", schedule, last, table_ != nullptr ? table_->count() : 0,
                responded_);
}

}  // namespace observer
}  // namespace mm
