// The two envelopes of the job protocol this firmware speaks: the claim's answer in, the result
// out (API §3, EXIT-NODES §6).
//
//   ClaimedJob(long Id, string Type, RepeaterRef Repeater, CredentialClass CredentialClass,
//              DateTime LeaseUntil, JobTimeouts Timeouts, JsonElement Payload,
//              JobCredential? Credential, PathInfo? Path, string? IdempotencyKey,
//              IReadOnlyList<long>? GroupWith)
//   ClaimResponse(DateTime ServerTime, IReadOnlyList<ClaimedJob> Jobs)
//   JobResultReport(string ClientId, Outcome Outcome, DateTime StartedAt, DateTime FinishedAt,
//                   IReadOnlyList<string>? PathUsed, int? Hops, int? RttMs, int? Transactions,
//                   JsonElement? Data, string? Log)
//
// (src/MeshMaster.Contracts/Envelopes.cs in twinoak/meshmaster.dk; the bytes are held against
// tests/fixtures/contracts/ClaimedJob.json and JobResultReport.json.)
//
// Host-portable, like the envelopes beside it: the claim is read with ArduinoJson and the result
// is written by JsonWriter.h, and both are what test/test_claim exercises without a board.

#pragma once

#include <cstddef>
#include <cstdint>

#include "mesh/ReplyCodec.h"

namespace mm {

// How a job ended, as the server spells it (spec/API.md §3). The names on the wire are the
// camelCase enum names ContractsJson uses.
enum class Outcome : uint8_t { Ok, Failed, Unreachable, Unauthorized, Unsupported, Timeout };
const char* outcomeName(Outcome o);

// The four types this firmware has a handler for, in the order EXIT-NODES §7 lists them. The
// heartbeat sends exactly this list, and routing sends nothing else (task p3-53).
constexpr int kJobTypeCount = 4;
extern const char* const kJobTypes[kJobTypeCount];
bool isKnownJobType(const char* type);

// A claim takes two at a time: one transaction is on the air at any moment anyway, and two is
// the status+telemetry pair the server schedules in one slot and claims together.
constexpr int kClaimMax = 2;

constexpr size_t kMaxJobTypeLen = 16;
constexpr size_t kMaxRepeaterLabelLen = 48;
constexpr size_t kMaxIdempotencyKeyLen = 80;
constexpr int kMaxClaimPathHops = 8;
constexpr size_t kMaxHopHexLen = 6;  // three bytes of hash
constexpr int kMaxGroupWith = 4;
constexpr size_t kPublicKeyHexLen = 64;

struct ClaimedJob {
  long long id = 0;
  char type[kMaxJobTypeLen + 1] = {0};

  long long repeaterId = 0;
  char repeaterLabel[kMaxRepeaterLabelLen + 1] = {0};
  char repeaterKey[kPublicKeyHexLen + 1] = {0};  // 64 lowercase hex, or empty

  CredentialClass credentialClass = CredentialClass::None;
  uint32_t leaseUntil = 0;  // UNIX seconds

  int perHopMs = 4000;
  int loginMs = 8000;

  // `acl` is the level the repeater's ACL grants this exit node's own key, and the only
  // credential material a job of these four types ever carries.
  bool hasAclLevel = false;
  int aclLevel = 0;

  // What reachability knows for this (agent, repeater): the hops to set as the contact's
  // out-path before sending. Absent while nothing has been learned, and then the mesh floods.
  bool hasPath = false;
  int pathCount = 0;
  char pathHops[kMaxClaimPathHops][kMaxHopHexLen + 1] = {};

  char idempotencyKey[kMaxIdempotencyKeyLen + 1] = {0};

  int groupCount = 0;
  long long groupWith[kMaxGroupWith] = {};
};

// Whether this firmware can run the job at all, and one sentence saying why not when it cannot.
// Its whole repertoire in phase 1 is the four read types at class `none` or `acl`: a login that
// carries a password is task 04's, and a job that asks for one is answered `unsupported` rather
// than `failed`, because "this firmware does not have what the job asked for" is what that
// outcome means - and because an outcome that moves nothing in reachability is the honest one
// for a transaction that never happened (EXIT-NODES §3).
bool canRun(const ClaimedJob& job, char* why, size_t whyCap);

// Jobs of one claim that share a login (EXIT-NODES §8): `groupWith` names them, and the repeater
// they are for is the session cache's own answer to the same question.
bool sharesLogin(const ClaimedJob& a, const ClaimedJob& b);

// POST /agent/v1/jobs/claim's body: `{"max":N}`.
size_t writeClaimRequest(int max, char* out, size_t cap);

// Reads the claim's answer. At most `max` jobs are kept - the server sends at most what was
// asked for, and a reply that says otherwise is not a reason to overrun a buffer. `count` is how
// many were read; `serverTime` is the claim's own clock. On refusal, `err` says why.
bool parseClaimResponse(const char* json, size_t len, ClaimedJob* jobs, int max, int& count, uint32_t& serverTime,
                        char* err, size_t errCap);

// The bound API §3 puts on a result's free text.
constexpr size_t kMaxJobLogChars = 4000;

struct JobReport {
  const char* clientId = "";
  Outcome outcome = Outcome::Failed;
  uint32_t startedAt = 0;
  uint32_t finishedAt = 0;

  bool hasPathUsed = false;
  const char* const* pathUsed = nullptr;
  int pathCount = 0;

  bool hasHops = false;
  int hops = 0;
  bool hasRttMs = false;
  int rttMs = 0;

  int transactions = 0;

  // The job type's result record, already written (JobResults.h), or nullptr.
  const char* data = nullptr;

  const char* log = nullptr;
};

size_t writeJobResult(const JobReport& r, char* out, size_t cap);

}  // namespace mm
