#include "SlotRecord.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace mm {
namespace update {

// NVS keys are at most 15 characters, which is NVS's limit and not ours; every one of these is
// under it with room to spare.
const char kRecordKeyState[] = "fw.slot.state";
const char kRecordKeyVersion[] = "fw.slot.ver";
const char kRecordKeySha[] = "fw.slot.sha";
const char kRecordKeyLength[] = "fw.slot.len";
const char kRecordKeyBase[] = "fw.slot.base";

const char kRecordJudged[] = "judged";

namespace {

bool isLowerHex(const char* text, size_t want) {
  size_t n = 0;
  for (const char* p = text; *p != '\0'; p++, n++) {
    const char c = *p;
    const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    if (!hex) return false;
  }
  return n == want;
}

// A decimal length, with nothing else in it. 0 is not a file.
bool parseLength(const char* text, uint32_t& out) {
  if (text == nullptr || text[0] == '\0') return false;
  char* end = nullptr;
  const unsigned long n = strtoul(text, &end, 10);
  if (end == text || *end != '\0' || n == 0 || n > 0xFFFFFFFFul) return false;
  out = static_cast<uint32_t>(n);
  return true;
}

bool copyInto(char* out, size_t cap, const char* text) {
  const size_t n = strlen(text);
  if (n == 0 || n + 1 > cap) return false;
  memcpy(out, text, n + 1);
  return true;
}

}  // namespace

SlotRecord loadRecord(RecordStore& store) {
  SlotRecord rec;
  char state[16] = {0};
  store.get(kRecordKeyState, state, sizeof(state));
  if (strcmp(state, kRecordJudged) != 0) return rec;  // nothing judged, and nothing else believed

  char version[sizeof(rec.version) + 1] = {0};
  char sha[sizeof(rec.sha256hex) + 1] = {0};
  char length[16] = {0};
  char base[sizeof(rec.base) + 1] = {0};
  store.get(kRecordKeyVersion, version, sizeof(version));
  store.get(kRecordKeySha, sha, sizeof(sha));
  store.get(kRecordKeyLength, length, sizeof(length));
  store.get(kRecordKeyBase, base, sizeof(base));

  uint32_t size = 0;
  if (!isLowerHex(sha, 2 * 32) || !parseLength(length, size)) return rec;
  if (!copyInto(rec.version, sizeof(rec.version), version)) return SlotRecord();
  if (!copyInto(rec.sha256hex, sizeof(rec.sha256hex), sha)) return SlotRecord();
  if (!copyInto(rec.base, sizeof(rec.base), base)) return SlotRecord();
  rec.size = size;
  rec.judged = true;
  return rec;
}

bool saveRecord(RecordStore& store, const SlotRecord& rec) {
  if (!rec.judged) {
    // The state first: from this moment the record reads as nothing judged, whatever else is
    // still lying in the other four keys.
    bool ok = store.put(kRecordKeyState, "");
    ok = store.put(kRecordKeyVersion, "") && ok;
    ok = store.put(kRecordKeySha, "") && ok;
    ok = store.put(kRecordKeyLength, "") && ok;
    ok = store.put(kRecordKeyBase, "") && ok;
    return ok;
  }

  char length[16];
  snprintf(length, sizeof(length), "%lu", static_cast<unsigned long>(rec.size));
  // The state last: until it is written the record reads as nothing judged, so a power cut
  // between any two of these leaves no record rather than half of one.
  if (!store.put(kRecordKeyVersion, rec.version)) return false;
  if (!store.put(kRecordKeySha, rec.sha256hex)) return false;
  if (!store.put(kRecordKeyLength, length)) return false;
  if (!store.put(kRecordKeyBase, rec.base)) return false;
  return store.put(kRecordKeyState, kRecordJudged);
}

void renderRecordLine(const SlotRecord& rec, char* out, size_t cap) {
  if (cap == 0) return;
  if (!rec.judged) {
    snprintf(out, cap, "holds nothing this firmware will boot");
    return;
  }
  snprintf(out, cap, "holds %.32s, %lu bytes, sha %.16s..., fetched from %.64s", rec.version,
           static_cast<unsigned long>(rec.size), rec.sha256hex, rec.base);
}

}  // namespace update
}  // namespace mm
