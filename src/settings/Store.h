// Where the settings live: NVS, one namespace ("meshmaster"), one entry per key of Keys.h.
//
// The token and the WiFi passphrase live here and nowhere else on the board - not in a build
// flag, not in a file, not in the WiFi driver's own NVS (src/net/WifiLink.cpp turns that off).
// Flash encryption is the owner's choice and is not assumed (README.md, "Rails"): without it,
// NVS is readable with a cable and physical access, which is the threat model of a board on a
// windowsill and is written down rather than hidden.
//
// Board-only (Preferences is the Arduino core's NVS wrapper); the key table and every rule
// about values are host-portable in Keys.h.

#pragma once

#include <Preferences.h>

#include <cstddef>

#include "Keys.h"

namespace mm {

class SettingsStore {
 public:
  bool begin();

  bool isSet(const KeyDef& key);

  // The stored value into `out` (cap bytes); returns its length, 0 and "" when unset.
  size_t get(const KeyDef& key, char* out, size_t cap);

  // The stored value, or the key's fallback, or "" - what the board actually uses.
  size_t effective(const KeyDef& key, char* out, size_t cap);

  // The numeric value in force for a Float/Int key: stored, else the fallback.
  float number(const KeyDef& key);

  // The same at double precision, for the heartbeat's radio block (Keys.h).
  double numberDouble(const KeyDef& key);

  bool put(const KeyDef& key, const char* value);

  // Forget every setting. The identity is not a setting and stays; the records below go with the
  // settings, which is right - a board whose settings are gone is about to be provisioned again,
  // and what its other application slot holds is then nobody's.
  bool eraseAll();

  // ---- records: what the firmware writes down for itself -------------------------------------
  //
  // Not settings. Nobody types one, `get` does not print one, `help` does not list one and
  // Keys.h has no row for one: they are the firmware's own notes in the same namespace, keyed by
  // a plain NVS key (15 characters at most, NVS's limit). Task p1.15-59's record of what the
  // other application slot holds is the first and only user - its five keys and their meaning
  // are src/update/SlotRecord.h, and nothing secret is among them.
  size_t getRecord(const char* nvsKey, char* out, size_t cap);
  // `value` under `nvsKey`; nullptr or "" forgets it, and forgetting one that is not there is not
  // a failure.
  bool putRecord(const char* nvsKey, const char* value);

 private:
  Preferences prefs_;
  bool open_ = false;
};

}  // namespace mm
