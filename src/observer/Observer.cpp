#include "Observer.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <Arduino.h>
#include <sys/time.h>

#include <agent/Log.h>

#include "version.h"

namespace mm {

namespace {

void microsNow(uint32_t (*clock)(), uint32_t& epoch, uint32_t& micros) {
  // One wall-clock read, as the observer firmware's gettimeofday (MQTTMessageBuilder.cpp:515-517):
  // the seconds are the board's clock (SNTP or the server, src/net/Clock.h), the sub-second is
  // the system clock's, which SNTP keeps and which is the same source once it is set.
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  epoch = clock != nullptr ? clock() : static_cast<uint32_t>(tv.tv_sec);
  micros = tv.tv_usec < 0 ? 0 : static_cast<uint32_t>(tv.tv_usec);
}

// Arduino's millis() is an unsigned long; the sweep's clock is a uint32_t function (Neighbours.h).
uint32_t uptimeMs() { return static_cast<uint32_t>(millis()); }

void readKey(SettingsStore& store, const char* name, char* out, size_t cap) {
  const KeyDef* key = findKey(name);
  if (key == nullptr) {
    out[0] = '\0';
    return;
  }
  store.get(*key, out, cap);
}

}  // namespace

void Observer::reportLine(void*, LogLevel level, const char* line) {
  // The connections' lines - counts and reasons, never a payload, a token or a password
  // (test/test_connection reads the ring back to hold that) - onto this board's log.
  logf(level, "%s", line);
}

void Observer::begin(SettingsStore& settings, const mesh::LocalIdentity* identity, const char* publicKeyHex,
                     const char* model, uint32_t (*clock)(), observer::Client* const clients[observer::kSlots],
                     const observer::Environment& env) {
  settings_ = &settings;
  signer_.setIdentity(identity);
  clock_ = clock;
  model_ = model != nullptr ? model : "";
  size_t i = 0;
  for (; publicKeyHex != nullptr && publicKeyHex[i] != '\0' && i < observer::kPublicKeyHexLen; i++) {
    publicKeyHex_[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(publicKeyHex[i])));
  }
  publicKeyHex_[i] = '\0';
  std::snprintf(defaultOrigin_, sizeof defaultOrigin_, "MeshMaster-%.4s", publicKeyHex_);
  observer::formatClientVersion(firmwareString(), clientVersion_, sizeof clientVersion_);
  link_.begin(clients, env, signer_, *this, &Observer::reportLine, this);
  reconfigure();
}

void Observer::readSettings() {
  readKey(*settings_, "mqtt.iata", iata_, sizeof iata_);
  readKey(*settings_, "mqtt.owner", owner_, sizeof owner_);
  readKey(*settings_, "mqtt.email", email_, sizeof email_);
  readKey(*settings_, "mqtt.origin", origin_, sizeof origin_);
  static const char* const kSlotKeys[observer::kSlots][6] = {
      {"mqtt1.preset", "mqtt1.server", "mqtt1.audience", "mqtt1.topic", "mqtt1.username", "mqtt1.password"},
      {"mqtt2.preset", "mqtt2.server", "mqtt2.audience", "mqtt2.topic", "mqtt2.username", "mqtt2.password"},
  };
  for (int s = 0; s < observer::kSlots; s++) {
    SlotStore& st = slot_[s];
    readKey(*settings_, kSlotKeys[s][0], st.preset, sizeof st.preset);
    readKey(*settings_, kSlotKeys[s][1], st.server, sizeof st.server);
    readKey(*settings_, kSlotKeys[s][2], st.audience, sizeof st.audience);
    readKey(*settings_, kSlotKeys[s][3], st.topic, sizeof st.topic);
    readKey(*settings_, kSlotKeys[s][4], st.username, sizeof st.username);
    readKey(*settings_, kSlotKeys[s][5], st.password, sizeof st.password);
    view_.slot[s].preset = st.preset;
    view_.slot[s].server = st.server;
    view_.slot[s].audience = st.audience;
    view_.slot[s].topic = st.topic;
    view_.slot[s].username = st.username;
    view_.slot[s].password = st.password;
  }
  view_.iata = iata_;
  view_.owner = owner_;
  view_.email = email_;
  view_.origin = origin_;

  // The neighbour sweep's two keys (task p1.8-33), with the table's fallbacks.
  char buf[16];
  settings_->effective(*findKey("mqtt.neighbors"), buf, sizeof buf);
  sweepSettings_.enabled = std::strcmp(buf, "off") != 0;
  settings_->effective(*findKey("mqtt.neighbors.interval"), buf, sizeof buf);
  const long hours = std::strtol(buf, nullptr, 10);
  sweepSettings_.intervalHours = hours < 12 ? 12u : (hours > 336 ? 336u : static_cast<uint32_t>(hours));
  sweep_.configure(sweepSettings_);
}

void Observer::reconfigure() {
  if (settings_ == nullptr) return;
  readSettings();
  // A slot this board refuses (observer/Board.h) does not turn the observer on by itself: for
  // the question "is anything configured", it is as if empty.
  observer::ObserverSettings forEnabled = view_;
  for (int s = 0; s < observer::kSlots; s++) {
    if (observer::slotRefusal(s, link_.board()) != nullptr) forEnabled.slot[s] = observer::SlotSettings();
  }
  enabled_ = observer::observerEnabled(forEnabled, why_, sizeof why_) && publicKeyHex_[0] != '\0';
  if (publicKeyHex_[0] == '\0') std::snprintf(why_, sizeof why_, "no identity");
  // The connections read the same view: a slot whose URL, sign-in or topics changed starts
  // over, the others keep their sessions (Connections::configure).
  link_.configure(view_, enabled_, publicKeyHex_, clientVersion_);
}

void Observer::loop() {
  link_.loop();
  // The sweep's next step, which may be one regions request and its wait - pumped through the
  // mesh's pump, which calls this loop again: the connections run through the wait, the sweep
  // does not re-enter itself.
  if (inSweep_) return;
  inSweep_ = true;
  if (sweep_.loop(link_.connectedSlots() > 0)) reportNeighbours();
  if (reportOffered_ && link_.neighboursPending() == 0) settleNeighbours();
  inSweep_ = false;
}

void Observer::beginNeighbours(observer::NeighbourLink& link, AirtimeGate* gate) {
  sweep_.begin(table_, link, gate, &uptimeMs);
  sweep_.configure(sweepSettings_);  // again, now that the sweep has a clock
}

void Observer::reportNeighbours() {
  // The report of the cycle that just finished (Dialect.h, formatNeighbours), built once here
  // and offered to the slots; what becomes of it is recorded when the hand-over ends.
  observer::NeighbourField fields[observer::kNeighbourCap];
  char keys[observer::kNeighbourCap][2 * observer::kNeighbourKeyLen + 1];
  const uint32_t now = millis();
  const int count = sweep_.snapshotCount();
  for (int i = 0; i < count; i++) {
    const observer::SweepEntry& e = sweep_.entry(i);
    observer::hexUpper(e.key, observer::kNeighbourKeyLen, keys[i], sizeof keys[i]);
    fields[i].pubkeyHex = keys[i];
    fields[i].snr = static_cast<float>(e.snrQ4) / 4.0f;
    fields[i].heardSecsAgo = (now - e.heardAtMs) / 1000u;
    fields[i].scopes = e.scopes;
    fields[i].status = observer::queryStatusName(e.status);
  }
  observer::NeighboursFields f;
  f.origin = origin();
  f.originId = publicKeyHex_;
  microsNow(clock_, f.atEpoch, f.atMicros);
  f.totalNeighbours = count;
  f.queriedNeighbours = sweep_.queriedCount();
  f.selfScopes = "";  // an exit node forwards no flood: it has no regions of its own, as a repeater with none says
  f.defaultScope = floodScope_ != nullptr && floodScope_[0] != '\0' ? floodScope_ : "*";
  f.entries = fields;
  f.count = count;
  neighboursLen_ = observer::formatNeighbours(f, neighbours_, sizeof neighbours_);
  if (neighboursLen_ == 0) {
    sweep_.noteReport(observer::ReportResult::Failed);
    logf(LogLevel::Warn, "neighbours: the report could not be built");
    return;
  }
  char why[observer::kReasonCap];
  switch (link_.offerNeighbours(neighbours_, neighboursLen_, why, sizeof why)) {
    case observer::Connections::ReportOffer::NoSlot:
      sweep_.noteReport(observer::ReportResult::NoSlot);
      logf(LogLevel::Info, "neighbours: report built (%u bytes, %d neighbours, %d responded); %s", static_cast<unsigned>(neighboursLen_),
           count, sweep_.respondedCount(), why);
      return;
    case observer::Connections::ReportOffer::SkippedHeap:
      sweep_.noteReport(observer::ReportResult::SkippedHeap);
      logf(LogLevel::Warn, "neighbours: report built (%u bytes, %d neighbours, %d responded) and skipped this cycle: %s",
           static_cast<unsigned>(neighboursLen_), count, sweep_.respondedCount(), why);
      return;
    case observer::Connections::ReportOffer::Queued:
      reportOffered_ = true;
      logf(LogLevel::Info, "neighbours: report built (%u bytes, %d neighbours, %d responded), going to %d slot(s)",
           static_cast<unsigned>(neighboursLen_), count, sweep_.respondedCount(), link_.neighboursPending());
      return;
  }
}

void Observer::settleNeighbours() {
  reportOffered_ = false;
  const int taken = link_.neighboursTaken();
  sweep_.noteReport(taken > 0 ? observer::ReportResult::Ok : observer::ReportResult::Failed);
  logf(taken > 0 ? LogLevel::Info : LogLevel::Warn, "neighbours: the report went to %d slot(s), %d would not take it", taken,
       link_.neighboursFailed());
}

void Observer::setLinkUp(bool up) { link_.setLinkUp(up); }

void Observer::onUplinkMemoryFailure() { link_.onUplinkMemoryFailure(); }

const char* Observer::origin() const { return origin_[0] != '\0' ? origin_ : defaultOrigin_; }

void Observer::onRxFrame(const RxFrame& frame) {
  if (!enabled_) {
    heardWhileOff_++;
    return;
  }
  observer::PacketFields f;
  f.origin = origin();
  f.originId = publicKeyHex_;
  microsNow(clock_, f.atEpoch, f.atMicros);
  f.raw = frame.raw;
  f.rawLen = static_cast<size_t>(frame.len);
  f.hash = frame.hash;
  f.snr = frame.snr;
  f.rssi = frame.rssi;
  f.score = frame.score;
  packetLen_ = observer::formatPacket(f, packet_, sizeof packet_);
  if (packetLen_ == 0) {
    notFitting_++;
    return;
  }
  formatted_++;
  link_.onPacketMessage(packet_, packetLen_);
}

size_t Observer::formatStatus(const char* status, char* out, size_t cap) {
  observer::StatusFields f;
  char radio[48];
  f.status = status;
  f.origin = origin();
  f.originId = publicKeyHex_;
  f.model = model_;
  f.firmwareVersion = firmwareString();
  observer::formatRadio(settings_->number(*findKey("radio.freq")), settings_->number(*findKey("radio.bw")),
                        static_cast<int>(settings_->number(*findKey("radio.sf"))),
                        static_cast<int>(settings_->number(*findKey("radio.cr"))), radio, sizeof radio);
  f.radio = radio;
  f.clientVersion = clientVersion_;
  microsNow(clock_, f.atEpoch, f.atMicros);
  f.repeat = "off";  // an exit node forwards nothing (AgentMesh::allowPacketForward)
  f.uptimeSecs = static_cast<int>(millis() / 1000);
  // The fields MeshView showed as "-" (task p1.8-33): the board's, through the statIfKnown path.
  if (boardStats_ != nullptr) {
    BoardStats s;
    boardStats_(s);
    f.noiseFloor = s.noiseFloor;
    f.txAirSecs = s.txAirSecs;
    f.rxAirSecs = s.rxAirSecs;
    f.batteryMv = s.batteryMv;
  }
  f.internalHeap = static_cast<int>(ESP.getFreeHeap());
  f.heapLargest = static_cast<int>(ESP.getMaxAllocHeap());
  f.mqttSlotsUp = link_.connectedSlots();
  f.mqttSlotsTotal = link_.configuredSlots();
  const uint32_t failures = link_.connectFailures();
  f.mqttConnectFailures = failures > 0 ? static_cast<int>(failures) : -1;  // left out when 0, as the observer firmware's "> 0" rule
  return observer::formatStatus(f, out, cap);
}

bool Observer::describeObserver(HeartbeatObserver& out) {
  // Off is off: no object at all, and the heartbeat is byte for byte the one a board that was
  // never an observer sends (task p1.7-28; AgentHeartbeatNoWake.json in test/test_envelopes).
  // `enabled_` is the console's own answer - at least one slot resolved and a region code to
  // build its topic from - and `get mqtt.status` is where its `why_` is read when it is false.
  if (!enabled_) return false;
  // The board's own slots: two on either board since task p1.9-37 (observer/Board.h). A slot
  // beyond them is nothing MeshMaster needs a row for.
  const int shown = slotsOnBoard();
  for (int s = 0; s < shown; s++) {
    const observer::SlotView v = link_.view(s);
    HeartbeatObserverSlot& hb = hbSlots_[s];
    hb = HeartbeatObserverSlot();
    hb.slot = s + 1;  // as the console numbers them, and as AgentObserverSlot.Slot is read
    // A slot that is off names no preset and no broker: the two fields are left out, which is
    // the record's own "absent" (AgentObserverSlot.Preset, .Host) and what the exit node's page
    // reads as *no broker*.
    hb.preset = v.configured ? v.name : nullptr;
    hb.host = v.host != nullptr && v.host[0] != '\0' ? v.host : nullptr;
    hb.state = observer::slotStateName(v.state);
    hb.since = v.sinceEpoch;  // 0 while the board has no clock: the field is left out
    hb.published = v.stats.published;
    hb.dropped = v.stats.dropped;
    hb.lastError = v.lastError != nullptr && v.lastError[0] != '\0' ? v.lastError : nullptr;
  }
  out.iata = iata_[0] != '\0' ? iata_ : nullptr;
  out.hasSlots = true;
  out.slots = hbSlots_;
  out.slotCount = shown;
  return true;
}

bool Observer::slotShown(int slot) const {
  if (slot < 0 || slot >= observer::kSlots) return false;
  if (slot < slotsOnBoard()) return true;
  const char* preset = view_.slot[slot].preset;
  return preset != nullptr && preset[0] != '\0' && std::strcmp(preset, observer::kPresetNone) != 0;
}

void Observer::describe(char* out, size_t cap) const {
  if (out == nullptr || cap == 0) return;
  char slots[128];
  size_t n = 0;
  slots[0] = '\0';
  for (int s = 0; s < observer::kSlots; s++) {
    if (!slotShown(s)) continue;
    const observer::ResolvedSlot r = observer::resolveSlot(view_.slot[s]);
    const observer::SlotView v = link_.view(s);
    // A slot the board refuses (Board.h) resolves, but its connection is off with the reason.
    const char* refusal = observer::slotRefusal(s, link_.board());
    const bool active = r.active && refusal == nullptr;
    const char* why = r.active ? refusal : r.why;
    const int w = std::snprintf(slots + n, sizeof slots - n, "%sslot %d %s%s%s%s", s > 0 ? ", " : "", s + 1, r.name,
                                active ? " " : " (", active ? observer::slotStateName(v.state) : why, active ? "" : ")");
    if (w < 0) break;
    n += static_cast<size_t>(w);
    if (n >= sizeof slots) break;
  }
  if (!enabled_) {
    std::snprintf(out, cap, "off: %s; %s; %lu frame(s) heard while off", why_, slots, static_cast<unsigned long>(heardWhileOff_));
    return;
  }
  std::snprintf(out, cap, "%s; iata %s, origin \"%s\"; %lu packet message(s) made, %lu did not fit; %d of %d slot(s) connected (get mqtt.status)",
                slots, iata_, origin(), static_cast<unsigned long>(formatted_), static_cast<unsigned long>(notFitting_),
                link_.connectedSlots(), link_.configuredSlots());
}

}  // namespace mm
