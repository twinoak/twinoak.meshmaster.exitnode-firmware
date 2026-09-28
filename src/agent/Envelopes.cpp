#include "Envelopes.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Iso8601.h"

namespace mm {

namespace {

// A bounded writer: everything goes through it, it never writes past `cap`, and it remembers
// that it ran out so the caller answers 0 rather than half a body.
class JsonWriter {
 public:
  JsonWriter(char* out, size_t cap) : out_(out), cap_(cap) {
    if (cap_ > 0) out_[0] = '\0';
  }

  void raw(const char* text) {
    for (const char* p = text; *p != '\0'; p++) put(*p);
  }

  void put(char c) {
    if (len_ + 1 >= cap_) {
      overflow_ = true;
      return;
    }
    out_[len_++] = c;
    out_[len_] = '\0';
  }

  // A JSON string: the two characters JSON reserves and the C0 controls are escaped, and
  // nothing else is - the text has already been bounded to well-formed UTF-8, which is what a
  // JSON string is made of. (The .NET encoder escapes non-ASCII as \uXXXX as well; that is a
  // difference in bytes and not in value, and it cannot arise in a heartbeat, whose every field
  // is ASCII and is held against the fixture.)
  void string(const char* text) {
    put('"');
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p != '\0'; p++) {
      const unsigned char c = *p;
      if (c == '"' || c == '\\') {
        put('\\');
        put(static_cast<char>(c));
      } else if (c < 0x20) {
        char esc[8];
        std::snprintf(esc, sizeof esc, "\\u%04x", c);
        raw(esc);
      } else {
        put(static_cast<char>(c));
      }
    }
    put('"');
  }

  void integer(long long v) {
    char buf[24];
    std::snprintf(buf, sizeof buf, "%lld", v);
    raw(buf);
  }

  void boolean(bool v) { raw(v ? "true" : "false"); }

  void key(const char* name) {
    comma();
    string(name);
    put(':');
  }

  // Between two members of the same object or array. A container that has just been closed
  // leaves `first_` false, which is right: whatever follows it in the parent needs a comma.
  void comma() {
    if (!first_) put(',');
    first_ = false;
  }

  void beginObject() {
    put('{');
    first_ = true;
  }

  void endObject() {
    put('}');
    first_ = false;
  }

  void beginArray() {
    put('[');
    first_ = true;
  }

  void endArray() {
    put(']');
    first_ = false;
  }

  bool overflowed() const { return overflow_; }
  size_t length() const { return len_; }

 private:
  char* out_;
  size_t cap_;
  size_t len_ = 0;
  bool overflow_ = false;
  bool first_ = true;
};

// A double the way .NET writes one: the shortest decimal that reads back as the same value,
// which is what System.Text.Json has done since .NET Core 3.0. Every number this firmware sends
// (a frequency, a bandwidth, an SNR, an RSSI) is in a range where %g never chooses the exponent
// form, and the process never calls setlocale(), so the decimal point is a point.
void formatDouble(double v, char* out, size_t cap) {
  if (std::isnan(v) || std::isinf(v)) {
    std::snprintf(out, cap, "0");
    return;
  }
  for (int digits = 15; digits <= 17; digits++) {
    std::snprintf(out, cap, "%.*g", digits, v);
    if (std::strtod(out, nullptr) == v) return;
  }
}

// Degrees x 1e6 as a decimal: the six places an advert can carry, with trailing zeros dropped,
// and no decimal point at all when there are none - which is what .NET writes for 56.0.
void formatE6(int32_t value, char* out, size_t cap) {
  const bool negative = value < 0;
  const uint32_t magnitude = negative ? static_cast<uint32_t>(-static_cast<int64_t>(value)) : static_cast<uint32_t>(value);
  const uint32_t whole = magnitude / 1000000u;
  uint32_t fraction = magnitude % 1000000u;
  char digits[8] = {0};
  std::snprintf(digits, sizeof digits, "%06lu", static_cast<unsigned long>(fraction));
  size_t n = std::strlen(digits);
  while (n > 0 && digits[n - 1] == '0') digits[--n] = '\0';
  if (n == 0) {
    std::snprintf(out, cap, "%s%lu", negative ? "-" : "", static_cast<unsigned long>(whole));
  } else {
    std::snprintf(out, cap, "%s%lu.%s", negative ? "-" : "", static_cast<unsigned long>(whole), digits);
  }
}

void writeDouble(JsonWriter& w, double v) {
  char buf[40];
  formatDouble(v, buf, sizeof buf);
  w.raw(buf);
}

void writeTime(JsonWriter& w, uint32_t epoch) {
  char when[24];
  formatUtc(epoch, when, sizeof when);
  w.string(when);
}

}  // namespace

size_t validUtf8PrefixLength(const char* text, size_t max) {
  if (text == nullptr) return 0;
  const unsigned char* p = reinterpret_cast<const unsigned char*>(text);
  size_t i = 0;
  while (i < max && p[i] != '\0') {
    const unsigned char c = p[i];
    size_t width = 0;
    uint32_t code = 0;
    if (c < 0x20) break;          // a control character is not something to carry across
    if (c < 0x80) { width = 1; code = c; }
    else if ((c & 0xE0) == 0xC0) { width = 2; code = c & 0x1Fu; }
    else if ((c & 0xF0) == 0xE0) { width = 3; code = c & 0x0Fu; }
    else if ((c & 0xF8) == 0xF0) { width = 4; code = c & 0x07u; }
    else break;                   // a continuation byte or 0xF8..0xFF: not a start
    if (i + width > max) break;
    bool ok = true;
    for (size_t k = 1; k < width; k++) {
      const unsigned char cc = p[i + k];
      if ((cc & 0xC0) != 0x80) { ok = false; break; }
      code = (code << 6) | (cc & 0x3Fu);
    }
    if (!ok) break;
    // Overlong encodings, the surrogate range and anything past U+10FFFF are not UTF-8.
    if (width == 2 && code < 0x80) break;
    if (width == 3 && code < 0x800) break;
    if (width == 4 && code < 0x10000) break;
    if (code >= 0xD800 && code <= 0xDFFF) break;
    if (code > 0x10FFFF) break;
    i += width;
  }
  return i;
}

size_t writeHeartbeat(const Heartbeat& hb, char* out, size_t cap) {
  JsonWriter w(out, cap);
  w.beginObject();
  w.key("agentVersion");
  w.string(hb.agentVersion == nullptr ? "" : hb.agentVersion);
  w.key("transport");
  w.string(hb.transport == nullptr ? "" : hb.transport);
  w.key("at");
  writeTime(w, hb.at);
  if (hb.companionPublicKey != nullptr) {
    w.key("companionPublicKey");
    w.string(hb.companionPublicKey);
  }
  if (hb.companionFirmware != nullptr) {
    w.key("companionFirmware");
    w.string(hb.companionFirmware);
  }
  if (hb.hasRadio) {
    w.key("radio");
    w.beginObject();
    w.key("freqMHz");
    writeDouble(w, hb.radio.freqMHz);
    w.key("bwKHz");
    writeDouble(w, hb.radio.bwKHz);
    w.key("sf");
    w.integer(hb.radio.sf);
    w.key("cr");
    w.integer(hb.radio.cr);
    w.endObject();
  }
  if (hb.hasJobTypes) {
    w.key("jobTypes");
    w.beginArray();
    for (int i = 0; i < hb.jobTypeCount; i++) {
      w.comma();
      w.string(hb.jobTypes[i]);
    }
    w.endArray();
  }
  if (hb.hasBudget) {
    w.key("budget");
    w.beginObject();
    w.key("maxTransactionsPerMinute");
    w.integer(hb.budget.maxTransactionsPerMinute);
    w.key("minGapSeconds");
    w.integer(hb.budget.minGapSeconds);
    w.endObject();
  }
  w.key("companionConnected");
  w.boolean(hb.companionConnected);
  w.key("queueDepth");
  w.integer(hb.queueDepth);
  if (hb.errors != nullptr && hb.errorCount > 0) {
    w.key("errors");
    w.beginObject();
    for (int i = 0; i < hb.errorCount; i++) {
      w.key(hb.errors[i].name);
      w.integer(hb.errors[i].value);
    }
    w.endObject();
  }
  if (hb.board != nullptr) {
    w.key("board");
    w.string(hb.board);
  }
  w.endObject();
  return w.overflowed() ? 0 : w.length();
}

size_t writeObservationBatch(const char* clientId, const ObservationItem* items, int count, char* out, size_t cap) {
  JsonWriter w(out, cap);
  w.beginObject();
  w.key("clientId");
  w.string(clientId == nullptr ? "" : clientId);
  w.key("items");
  w.beginArray();
  for (int i = 0; i < count; i++) {
    const ObservationItem& it = items[i];
    w.comma();
    w.beginObject();
    w.key("at");
    writeTime(w, it.at);
    w.key("kind");
    w.string(it.kind);
    w.key("publicKey");
    w.string(it.publicKey);
    if (it.hasPath) {
      w.key("path");
      w.beginArray();
      for (int h = 0; h < it.pathCount; h++) {
        w.comma();
        w.string(it.path[h]);
      }
      w.endArray();
    }
    if (it.hasHops) {
      w.key("hops");
      w.integer(it.hops);
    }
    if (it.hasSnr) {
      w.key("snr");
      writeDouble(w, it.snr);
    }
    if (it.hasRssi) {
      w.key("rssi");
      writeDouble(w, it.rssi);
    }
    if (it.hasFields) {
      const ObservationFields& f = it.fields;
      w.key("fields");
      w.beginObject();
      if (f.name != nullptr) {
        w.key("name");
        w.string(f.name);
      }
      if (f.hasPosition) {
        char buf[24];
        w.key("lat");
        formatE6(f.latE6, buf, sizeof buf);
        w.raw(buf);
        w.key("lon");
        formatE6(f.lonE6, buf, sizeof buf);
        w.raw(buf);
      }
      if (f.role != nullptr) {
        w.key("role");
        w.string(f.role);
      }
      if (f.hasFlags) {
        w.key("flags");
        w.integer(f.flags);
      }
      w.endObject();
    }
    w.endObject();
  }
  w.endArray();
  w.endObject();
  return w.overflowed() ? 0 : w.length();
}

}  // namespace mm
