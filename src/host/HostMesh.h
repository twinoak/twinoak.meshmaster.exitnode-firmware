// A fake repeater behind the mesh interface: stock MeshCore firmware at `d9296435`, as a second
// reading of its source (docs/host.md).
//
// **Written from the firmware, not from the other fake.** meshmaster.dk has one of these already -
// `src/MeshMaster.MeshCore/Fake/FakeCompanion.cs`, which the .NET agent's end-to-end tests have
// run against since phase 2 - and this is deliberately *not* a port of it. It is written from
// `examples/simple_repeater/MyMesh.cpp` and `src/helpers/CommonCLI.cpp` at the pin, so that the two
// fakes can disagree with each other and one of them be wrong. Where they do, the difference is a
// row of the report's table: a firmware bug, a .NET bug, or two readings of one source.
//
// What it answers, each with the function it was read from:
//
//   the login        `MyMesh::handleLoginReq` (MyMesh.cpp:87-145). A blank password is the ACL
//                    login: the sender's entry decides, and a key the ACL does not hold **falls
//                    through to the password check** - where the empty string is compared with the
//                    admin and the guest password, and `NodePrefs()` leaves the guest password
//                    empty (CommonCLI.h:191). So a stranger's blank login to a repeater whose
//                    guest password has never been set is answered **guest**, not with silence.
//                    A password login rewrites the sender's entry to the password's level, which
//                    is why every admin session ends with the restore (EXIT-NODES §7).
//                    The reply is prefixed with the **repeater's own** clock and not with the
//                    sender's timestamp, unlike every other reply in the file (:130).
//   the requests     `MyMesh::handleRequest` (:211-376): status (0x01), telemetry (0x03) with the
//                    guest permission mask, the access list (0x05, admin only, and silence rather
//                    than a short list once it would not fit one datagram), the neighbours (0x06)
//                    a page at a time, and the owner info (0x07). Each reply reflects the sender's
//                    timestamp as its tag, and each is zero-padded to a cipher block
//                    (`Utils::encrypt`, Utils.cpp:85-105).
//   the CLI          `MyMesh::handleCommand` (:1196-1285) and `CommonCLI::handleCommand`
//                    (CommonCLI.cpp:182-444), including `setperm`, the `get`/`set` table with its
//                    ranges and its reply spellings, `region` and its load mode with the
//                    blank-line terminator, `password`, `time`, `advert`, and `reboot` as silence.
//                    A sender timestamp equal to the last one is a **retry** and runs nothing
//                    (:707-711), which is why every line of an apply needs its own second.
//   the trace        a per-hop SNR along the round trip the agent asked for.
//
// It also keeps the journal the harness asserts on: every frame this exit node sent, in order,
// with the sender timestamp it carried and whether it was answered - which is how "every command
// was sent once" and "timestamps strictly increase" become assertions rather than hopes. A line
// made from a password is journalled with the value starred, by the same rule the frame log
// follows (agent/Credentials.h).
//
// No MeshCore: the bytes it writes are read back by the same mesh/ReplyCodec.cpp the board uses,
// which is the second half of the arrangement - a reply this file gets wrong is a parse failure in
// the agent's own code and not a quiet agreement between a fake and its twin.

#pragma once

#include <cstddef>
#include <cstdint>

#include "HostScenario.h"
#include "mesh/MeshPort.h"

namespace mm {

// One frame this exit node sent, as the fake saw it arrive.
struct HostTransaction {
  int seq = 0;
  char repeater[13] = {0};  // the first twelve hex characters of the repeater's key
  char kind[12] = {0};      // "login" | "req" | "cli" | "trace" | "discover"
  // "login", "status", "cli:get name", ... - what the `drop` knob of a scenario names, and long
  // enough for the longest CLI line a claim can carry (JobEnvelopes.h's kMaxCliCommandLen) with
  // the "cli:" in front of it, because a journal that cut a command could not prove it was sent
  // once.
  char what[168] = {0};
  uint32_t timestamp = 0;   // the sender timestamp (or tag) the frame carried
  bool flood = false;
  bool answered = false;
};

constexpr int kMaxHostTransactions = 512;

class HostMesh : public MeshPort {
 public:
  HostMesh() = default;

  // The scenario and this exit node's own key. The key is what the fake's ACL is about: the grant
  // an association left behind, and what `setperm <own key> <level>` puts back.
  void begin(HostScenario& scenario, const char* exitNodeKeyHex);

  // The time source: UNIX seconds. The fake's clocks - the reply tags, the `time` command's
  // verdict, the neighbours' "heard N seconds ago" - all come from it, and so does uniqueTime().
  void setClock(uint32_t (*clock)());

  // ------------------------------------------------------------------ MeshPort

  void loop() override;
  const MeshCounters& counters() const override { return counters_; }
  uint32_t overheard() const override { return 0; }  // there is no mesh to overhear
  void setRing(ObservationRing* ring) override { ring_ = ring; }
  void setFrameLog(bool on) override { frameLog_ = on; }
  bool frameLog() const override { return frameLog_; }
  const HeardAdvert& recent(int index) const override;
  int recentCount() const override { return recentCount_; }

  bool beginJob(const char* publicKeyHex, const uint8_t* pathHashes, uint8_t hopCount, uint8_t hashSize) override;
  void endJob() override;
  void setDirectPath() override;
  void forgetPath() override;
  bool jobInFlight() const override { return peer_ != nullptr; }
  uint8_t pathHopCount() const override { return hasPath_ ? outHops_ : 0; }
  bool hasPath() const override { return hasPath_; }
  int describePath(char hops[][8], int max) const override;

  SendResult sendLogin(const char* password, uint32_t& tag) override;
  SendResult sendRequest(uint8_t reqType, const uint8_t* data, uint8_t len, uint32_t& tag) override;
  SendResult sendCli(const char* text, uint32_t timestamp, bool sensitive = false) override;
  SendResult sendDiscovery(uint32_t& tag) override;
  SendResult sendTrace(const uint8_t* roundTripHashes, uint8_t hashBytes, uint8_t hashSize, uint32_t& tag) override;
  bool awaitReply(uint32_t tag, uint32_t timeoutMs, void (*pump)()) override;
  bool awaitText(uint32_t timeoutMs, void (*pump)()) override;
  const MeshReply& reply() const override { return reply_; }
  uint32_t uniqueTime() override;

  // ------------------------------------------------------------------ the journal

  int transactionCount() const { return journalCount_; }
  const HostTransaction& transaction(int i) const;
  // The journal as JSON, for tools/host-run.ps1 and tools/host-stub.py to assert on. Returns the
  // length, or 0 when it did not fit.
  size_t writeJournal(char* out, size_t cap) const;
  // What the fake made of a repeater, after the run: the harness asserts on these too - a
  // `password.set` must have changed the password, an apply its key, a `setperm` the ACL entry.
  const HostScenario& scenario() const { return *scenario_; }

 private:
  // The repeater's own view of one client, which on stock firmware is the ACL entry
  // (`ClientInfo`): the permissions the last login wrote, and the newest sender timestamp it has
  // accepted from that key.
  struct Client {
    uint32_t lastTimestamp = 0;  // the newest the repeater has accepted from this key
  };

  enum class Pending : uint8_t { None, Response, Text, Trace };

  HostRepeaterSpec* peer_ = nullptr;
  int peerIndex_ = -1;
  // The repeater's own `last_timestamp` per key, which outlives a job: the replay rule is the
  // repeater's state and the reason an apply's lines each need their own second.
  Client clients_[kMaxHostRepeaters];

  // The out-path, as the claim gave it or as a flooded reply taught it.
  bool hasPath_ = false;
  uint8_t outHops_ = 0;
  uint8_t outHashSize_ = 1;
  uint8_t outPath_[kMaxMeshPathBytes] = {};

  // The repeater's own clock: the host's, plus the scenario's `clockOffsetS`. A repeater that has
  // been up for a month is minutes out, which is why `clock.sync` exists.
  uint32_t repeaterNow() const;
  void note(const char* kind, const char* what, uint32_t timestamp, bool flood, bool answered);
  void queue(Pending kind, const uint8_t* body, size_t len, uint32_t tag);
  void queueText(const char* text);
  void learnPathFromFlood();
  // The repeater's entry for this exit node, and its role; nullptr when the ACL does not hold it.
  HostAclEntry* ownEntry() const;
  // `handleRequest`, by request type. Returns the reply length written into `out` (before the
  // cipher block's padding), or 0 for the firmware's own silence.
  size_t handleRequest(uint8_t reqType, const uint8_t* data, uint8_t len, uint32_t tag, uint8_t* out, size_t cap,
                       char* what, size_t whatCap);
  // `MyMesh::handleCommand` then `CommonCLI::handleCommand`; `reply` is CommonCLI's 160-byte
  // buffer and an empty one is the firmware's silence.
  void handleCommand(uint32_t senderTimestamp, const char* command, char* reply, size_t cap);
  void handleGetCmd(const char* config, char* reply, size_t cap);
  void handleSetCmd(const char* config, char* reply, size_t cap);
  void handleRegionCmd(const char* command, char* reply, size_t cap);

  HostScenario* scenario_ = nullptr;
  char exitNodeKey_[kPublicKeyHexLen + 1] = {0};
  // The repeater the job is for, as the journal names it: the first twelve hex characters of the
  // claim's key, which is a repeater the scenario may not hold at all.
  char journalKey_[13] = {0};
  uint32_t (*clock_)() = nullptr;
  uint32_t lastUnique_ = 0;

  MeshCounters counters_;
  ObservationRing* ring_ = nullptr;
  bool frameLog_ = false;
  HeardAdvert recent_[1];
  int recentCount_ = 0;

  MeshReply reply_;
  MeshReply staged_;
  Pending pending_ = Pending::None;
  uint32_t pendingAtMs_ = 0;
  bool awaitingText_ = false;
  uint32_t awaitingTag_ = 0;

  // The region loader's mode, which is a repeater's state and not a command's: until the blank
  // line arrives, every CLI command it hears is read as a region (MyMesh.cpp:1197-1230).
  bool regionLoadActive_ = false;
  char regionLoadBuffer_[kMaxHostRegionListing + 1] = {0};
  int regionLoadCount_ = 0;

  HostTransaction journal_[kMaxHostTransactions];
  int journalCount_ = 0;
};

}  // namespace mm
