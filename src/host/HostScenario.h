// The scenario the fake mesh is set up from: which repeaters exist, their passwords, their ACLs,
// their configurations - and the knobs a test needs (docs/host.md).
//
// One JSON file, read once at start-up, into fixed structs with no allocation, exactly as the
// firmware holds a claim. The shape is this file; `tests/fixtures/host/*.json` are the scenarios
// the harness ships with and `docs/host.md` is the prose.
//
// Nothing here is a secret of anybody's. A scenario's `adminPassword` is the fake repeater's, made
// up for the run, and the one in `tests/fixtures/host/` is in the repository on purpose: it is the
// password of a repeater that does not exist.

#pragma once

#include <cstddef>
#include <cstdint>

#include "agent/ConfigRead.h"
#include "agent/Credentials.h"
#include "agent/JobEnvelopes.h"
#include "mesh/ReplyCodec.h"

namespace mm {

constexpr int kMaxHostRepeaters = 4;
constexpr int kMaxHostAclEntries = 32;        // past the 23 one reply can carry, so "an ACL of 24" is a scenario
constexpr int kMaxHostNeighbours = 40;
constexpr int kMaxHostRepeaterPrefs = 24;
constexpr int kMaxHostTelemetry = 8;
constexpr int kMaxHostKnobs = 8;
constexpr size_t kMaxHostNameLen = 32;
constexpr size_t kMaxHostTextLen = 200;
constexpr size_t kMaxHostRegionListing = 320;  // longer than the firmware's 160, so "over 160" is a scenario
constexpr size_t kMaxHostKnobLen = 40;

// One entry of the repeater's own ACL: a 32-byte key as 64 hex, and the permission byte whose low
// two bits are the role (mesh/ReplyCodec.h).
struct HostAclEntry {
  char key[kPublicKeyHexLen + 1] = {0};
  uint8_t permissions = 0;
};

struct HostPref {
  char key[kMaxConfigKeyLen + 1] = {0};
  char value[kMaxHostTextLen + 1] = {0};
};

struct HostNeighbour {
  char key[kPublicKeyHexLen + 1] = {0};
  uint32_t heardSecondsAgo = 0;
  double snr = 0;
};

struct HostTelemetryReading {
  int channel = 1;
  // Cayenne LPP type codes as CayenneLPP writes them, which mesh/ReplyCodec.cpp decodes.
  uint8_t typeCode = 0;
  double value = 0;
};

// What the status request answers: RepeaterStats of examples/simple_repeater/MyMesh.h at d9296435,
// as the scenario sets it.
struct HostStatus {
  int batteryMv = 4012;
  int txQueueLen = 0;
  int noiseFloorDbm = -112;
  int lastRssi = -97;
  uint32_t packetsRecv = 12345;
  uint32_t packetsSent = 6789;
  uint32_t airtimeS = 1234;
  uint32_t uptimeS = 864000;
  uint32_t sentFlood = 1000;
  uint32_t sentDirect = 2000;
  uint32_t recvFlood = 3000;
  uint32_t recvDirect = 4000;
  int errEvents = 0;
  double lastSnr = 6.25;
  int directDups = 0;
  int floodDups = 3;
  uint32_t rxAirtimeS = 456;
  uint32_t recvErrors = 2;
  bool legacy = false;  // a 48-byte RepeaterStats: the two newest counters absent
};

struct HostRepeaterSpec {
  char key[kPublicKeyHexLen + 1] = {0};
  char name[kMaxHostNameLen + 1] = {0};
  char ownerInfo[kMaxHostTextLen + 1] = {0};
  char firmware[kMaxHostNameLen + 1] = {0};  // what `ver` answers, and the owner-info reply's first line
  char build[kMaxHostNameLen + 1] = {0};     // the `(Build: ...)` of `ver`
  char board[kMaxHostNameLen + 1] = {0};     // what `board` answers: the manufacturer name
  char role[kMaxHostNameLen + 1] = {0};      // what `get role` answers

  char adminPassword[kMaxPasswordLen + 1] = {0};
  // Empty by default, as `NodePrefs()` leaves it (src/helpers/CommonCLI.h:191 at d9296435) - which
  // is a behaviour and not an omission: see HostMesh.cpp's login.
  char guestPassword[kMaxPasswordLen + 1] = {0};

  int hops = 1;          // how far away: the path the fake teaches, and what a trace walks
  uint32_t latencyMs = 60;  // one hop's round trip, so an rtt is a plausible number and not zero
  bool online = true;    // false: every frame to it is silence
  // How far this repeater's own clock is from the host's, in seconds. Negative is behind, which is
  // what a repeater that has been up for a month looks like and what `clock.sync` is for; positive
  // is ahead, which is the refusal `(ERR: clock cannot go backwards)`.
  int32_t clockOffsetS = -120;

  // The one concession the compose-stack scenario makes (docs/host.md section 7): any non-empty
  // password is taken as the admin password. meshmaster.dk will not tell a script what it stored
  // for a repeater - revealing a credential is a gated action and a token is always refused one -
  // so a fake that insisted on the real value could not be logged into from a claim at all. What
  // this gives up is exactly one thing: that the password the server holds is the password the
  // repeater has, which is the server's own business and what the rotation step checks. Everything
  // else - the level the login is answered at, the restore, the session cache - is unchanged.
  // Against the recorded stub it is **false**, where the passwords are the scenario's own.
  bool acceptAnyPassword = false;

  int aclCount = 0;
  HostAclEntry acl[kMaxHostAclEntries];

  int prefCount = 0;
  HostPref prefs[kMaxHostRepeaterPrefs];

  int neighbourCount = 0;
  HostNeighbour neighbours[kMaxHostNeighbours];

  int telemetryCount = 0;
  HostTelemetryReading telemetry[kMaxHostTelemetry];

  HostStatus status;

  // The region map as `region` prints it and as `region default` answers. A listing longer than
  // the firmware's 160-character reply buffer is cut there, with no marker, which is the knob the
  // task asks for.
  char regionListing[kMaxHostRegionListing + 1] = {0};
  char regionDefault[kMaxHostNameLen + 1] = {0};
  char regionHome[kMaxHostNameLen + 1] = {0};

  // A reply this repeater does not send: a name per the journal's `what` (`login`, `status`,
  // `owner-info`, `telemetry`, `neighbours`, `acl`, `trace`, `discover`, or `cli:<prefix>`).
  int dropCount = 0;
  char drop[kMaxHostKnobs][kMaxHostKnobLen + 1] = {};

  // A `set <key>` this repeater refuses, answering the firmware's own refusal for that key.
  int refuseCount = 0;
  char refuse[kMaxHostKnobs][kMaxConfigKeyLen + 1] = {};

  const char* pref(const char* key) const;
  bool setPref(const char* key, const char* value);
  bool drops(const char* what) const;
  bool refuses(const char* key) const;
  const HostAclEntry* aclEntry(const char* key) const;
};

struct HostScenario {
  // The exit node's own identity, which is what its ACL entries are about and what the heartbeat
  // sends as `companionPublicKey`.
  char exitNodeKey[kPublicKeyHexLen + 1] = {0};

  int repeaterCount = 0;
  HostRepeaterSpec repeaters[kMaxHostRepeaters];

  HostRepeaterSpec* find(const char* key);
};

// A scenario with one repeater, as `docs/host.md` describes it: the defaults a run with no
// `--scenario` gets, so the program is useful before anybody has written one.
void defaultScenario(HostScenario& out, const char* exitNodeKey);

// Reads a scenario file. `json` is mutable and parsed in place, as a claim is. On refusal `err`
// says why in one sentence.
bool parseScenario(char* json, size_t len, HostScenario& out, char* err, size_t errCap);

}  // namespace mm
