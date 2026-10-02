// `Preferences`, for the host build only (docs/host.md).
//
// The board's settings live in NVS and `src/settings/Store.cpp` is the one file that touches it.
// The host build compiles that same file - the key table, the fallbacks, the numeric parsing and
// the rule that an unset key is "" are the firmware's and are worth exercising - against this: a
// fixed table in RAM, seeded from the host program's arguments (`--url`, `--token`, `--board`) and
// gone when the process is.
//
// **No file, and deliberately so.** The token the harness runs with is the compose stack's or the
// recorded stub's, and the rails of README.md say a secret is never written anywhere in this
// repository. A host run that keeps nothing cannot leave one behind.

#pragma once

// The Arduino core's Preferences.h pulls these in, and src/settings/Store.cpp leans on it for
// them: a shim that did not would make that file's own includes a board-only detail.
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace mm {
namespace host {

// Enough for the key table of src/settings/Keys.cpp with room over.
constexpr int kMaxHostPrefs = 48;
constexpr size_t kMaxHostPrefKeyLen = 32;
constexpr size_t kMaxHostPrefValueLen = 256;

}  // namespace host
}  // namespace mm

class Preferences {
 public:
  bool begin(const char* name, bool readOnly);
  bool isKey(const char* key) const;
  size_t getString(const char* key, char* out, size_t cap) const;
  size_t putString(const char* key, const char* value);
  bool clear();

 private:
  struct Entry {
    char key[mm::host::kMaxHostPrefKeyLen + 1] = {0};
    char value[mm::host::kMaxHostPrefValueLen + 1] = {0};
    bool set = false;
  };

  int find(const char* key) const;

  Entry entries_[mm::host::kMaxHostPrefs];
  bool open_ = false;
};
