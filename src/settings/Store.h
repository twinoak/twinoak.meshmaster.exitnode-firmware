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

  // Forget every setting. The identity is not a setting and stays.
  bool eraseAll();

 private:
  Preferences prefs_;
  bool open_ = false;
};

}  // namespace mm
