#include "Store.h"

#include <cstring>

namespace mm {

namespace {
constexpr const char kNamespace[] = "meshmaster";
}

bool SettingsStore::begin() {
  open_ = prefs_.begin(kNamespace, false);
  return open_;
}

bool SettingsStore::isSet(const KeyDef& key) { return open_ && prefs_.isKey(key.nvs); }

size_t SettingsStore::get(const KeyDef& key, char* out, size_t cap) {
  if (cap == 0) return 0;
  out[0] = '\0';
  if (!open_ || !prefs_.isKey(key.nvs)) return 0;
  const size_t n = prefs_.getString(key.nvs, out, cap);
  out[cap - 1] = '\0';
  if (n == 0) out[0] = '\0';
  return std::strlen(out);
}

size_t SettingsStore::effective(const KeyDef& key, char* out, size_t cap) {
  const size_t n = get(key, out, cap);
  if (n > 0) return n;
  if (key.fallback != nullptr) {
    std::snprintf(out, cap, "%s", key.fallback);
    return std::strlen(out);
  }
  return 0;
}

float SettingsStore::number(const KeyDef& key) {
  char buf[kMaxValueLen + 1];
  effective(key, buf, sizeof buf);
  return numericValue(buf);
}

double SettingsStore::numberDouble(const KeyDef& key) {
  char buf[kMaxValueLen + 1];
  effective(key, buf, sizeof buf);
  return numericValueDouble(buf);
}

bool SettingsStore::put(const KeyDef& key, const char* value) {
  if (!open_ || value == nullptr) return false;
  return prefs_.putString(key.nvs, value) > 0;
}

bool SettingsStore::eraseAll() {
  if (!open_) return false;
  return prefs_.clear();
}

size_t SettingsStore::getRecord(const char* key, char* out, size_t cap) {
  if (cap == 0) return 0;
  out[0] = '\0';
  if (!open_ || key == nullptr || !prefs_.isKey(key)) return 0;
  prefs_.getString(key, out, cap);
  out[cap - 1] = '\0';
  return std::strlen(out);
}

bool SettingsStore::putRecord(const char* key, const char* value) {
  if (!open_ || key == nullptr) return false;
  if (value == nullptr || value[0] == '\0') {
    // Forgetting one: a key that was never there is already forgotten, and that is not a failure.
    if (!prefs_.isKey(key)) return true;
    return prefs_.remove(key);
  }
  return prefs_.putString(key, value) > 0;
}

}  // namespace mm
