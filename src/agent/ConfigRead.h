// The config read: the `get` list, the two keys that are not a `get`, the two extras, and the
// snapshot a read makes (CONFIGURATION §3, §8; EXIT-NODES §4.3, §7; tasks p3.5-43, p4-49,
// p4-57, p4.1-60).
//
// This is the .NET agent's ConfigReadHandler, RegionRead and ConfigReadPayload as bounded
// structs: the list is the server's projection of its key table (`ConfigReadPayload.DefaultKeys`
// in twinoak/meshmaster.dk), the price is `ConfigReadPayload.Transactions`, and the result is
// `ConfigReadResult(Values, Raw, Firmware, Board, AclRestored)` written byte for byte as
// MeshMaster.Contracts writes it (tests/fixtures/contracts/ConfigReadResult.json).
//
// Host-portable: no Arduino, no allocation; the snapshot is a fixed table the runner fills.

#pragma once

#include <cstddef>
#include <cstdint>

#include "mesh/ReplyCodec.h"

namespace mm {

// The default `get` list, in the server's order: what is readable and is not a secret, with
// `regions` and `acl` last - the two keys that are not a `get` at all.
constexpr int kDefaultConfigKeyCount = 15;
extern const char* const kDefaultConfigKeys[kDefaultConfigKeyCount];

// What is asked for after the keys, whatever the list: the firmware and the board, which the
// snapshot records and no `get` carries.
constexpr int kConfigReadExtraCount = 2;
extern const char* const kConfigReadExtras[kConfigReadExtraCount];  // "ver", "board"

extern const char kAclKey[];      // "acl": REQ_TYPE_GET_ACCESS_LIST, admin only, last
extern const char kRegionsKey[];  // "regions": `region` and `region default`, two commands

// The price of a read of `count` keys (nullptr or 0 is the default list): one transaction a key,
// the two extras, and one more when `regions` is on the list. The login is not in it.
int configReadTransactions(const char* const* keys, int count);

// The bounds of a claim's `keys` and of a reply. A `get` reply is CommonCLI's 160-byte buffer;
// the regions value is that listing and one line MeshMaster adds; the ACL is up to 23 tokens of
// twelve hex characters, a colon and a level.
constexpr int kMaxConfigKeys = 20;
constexpr size_t kMaxConfigKeyLen = 32;
constexpr size_t kMaxConfigValueLen = 208;
constexpr size_t kMaxAclValueLen = static_cast<size_t>(kAccessListMaxEntries) * 15;
constexpr int kMaxConfigRawLines = 6;
constexpr size_t kMaxConfigRawLen = 200;
constexpr size_t kMaxConfigTextLen = 64;  // `ver` and `board`

struct ConfigValue {
  char key[kMaxConfigKeyLen + 1] = {0};
  char value[kMaxConfigValueLen + 1] = {0};
};

// What a read made: the values in the order they were read (the .NET handler reads `regions`
// first, then the `get`s in the list's order, and the ACL after `ver` and `board`), the replies
// that did not parse, the two extras, and what became of our own ACL entry.
struct ConfigSnapshot {
  int count = 0;
  ConfigValue values[kMaxConfigKeys];

  bool hasAcl = false;
  char acl[kMaxAclValueLen + 1] = {0};

  int rawCount = 0;
  char raw[kMaxConfigRawLines][kMaxConfigRawLen + 1] = {};

  char firmware[kMaxConfigTextLen + 1] = {0};
  char board[kMaxConfigTextLen + 1] = {0};

  // `aclRestored` is null when there was nothing to restore (no grant on this pair), true when
  // the repeater confirmed the restore, false when it was sent and not confirmed.
  bool hasAclRestored = false;
  bool aclRestored = false;

  void clear();
  bool put(const char* key, const char* value);  // false when the table is full
  bool addRaw(const char* line);                 // false when the lines are full
  // How many keys were read, for the rule that silence on the ACL after keys were read is not
  // a failure (task p4-49): the values, the ACL not among them yet.
  int valuesRead() const { return count; }
};

// The result as ContractsJson writes ConfigReadResult: `values`, `raw`, then `firmware` and
// `board` when read and `aclRestored` when it has a value. Returns the length, or 0 when it did
// not fit.
size_t writeConfigReadResult(const ConfigSnapshot& s, char* out, size_t cap);

// ---------------------------------------------------------------------------------------------
// A `get` reply

// `CommonCLI::handleGetCmd` answers `> <value>`; anything else is a refusal or an unknown key
// (`??: <key>`), which the snapshot keeps verbatim under `raw` as `<command>: <reply>`. Returns
// the value's start, or nullptr when the reply is not that shape.
const char* cliReplyValue(const char* reply);

// The reply's text without leading and trailing whitespace, in place; returns `text`.
char* trimInPlace(char* text);

// ---------------------------------------------------------------------------------------------
// The `regions` key (task p4-57)

// `CommonCLI::handleRegionCmd` at d9296435 exports the map into 160 characters and
// `BufStream::write` simply stops there, with no marker: a reply of 159 characters, or one that
// does not end in a newline, ran out of room and is no evidence.
constexpr size_t kRegionReplyLimit = 160;
extern const char kRegionListCommand[];     // "region"
extern const char kRegionDefaultCommand[];  // "region default"
extern const char kRegionTruncatedLine[];   // the raw line a cut-off listing gets

bool regionListingIsTruncated(const char* listing);

// The default scope out of what `region default` answered (` default scope is dk`): false when
// the reply is not that sentence, which includes the *setting* command's `default scope is now`.
bool regionDefaultOf(const char* reply, char* out, size_t cap);

// The two replies as the one value a snapshot stores: the listing as printed, without its
// trailing newline, then `\ndefault <name>` (`<null>` when there is none). False when the
// listing is no evidence (empty, or cut off), and then nothing is stored under `regions`.
bool regionValue(const char* listing, const char* defaultReply, char* out, size_t cap);

// ---------------------------------------------------------------------------------------------
// The `acl` key (task p3.5-43, p4-49)

// The snapshot's value: `<prefix>:<role>` tokens separated by one space, which the server's
// AclEvidence.Parse reads (twelve hex characters is above its minimum prefix).
size_t accessListTokens(const AccessListEntry* entries, int count, char* out, size_t cap);

// The raw line of a list at the packet's ceiling, and of a request that got no reply after
// keys were read - the two cases where the snapshot says why its ACL is no evidence.
extern const char kAclFullLine[];
extern const char kAclSilentLine[];
extern const char kAclUnreadableLine[];

}  // namespace mm
