// The one JSON writer this firmware sends anything with.
//
// Bounded: it never writes past `cap`, and it remembers that it ran out, so a caller answers 0
// rather than half a body. Every envelope the board sends - the heartbeat, an observation batch,
// a job result - is written straight into the output buffer by this, rather than built as a
// document on the heap first: a twenty-item batch is 6 KB, and a board that is its own exit node
// has 320 KB of RAM in all.
//
// The bytes it makes are System.Text.Json's for the same value, which is what the contract
// fixtures hold both ends to: camelCase keys in the record's own field order, no spaces, no
// indentation, a null field left out entirely, and a double written as the shortest decimal that
// reads back as itself (.NET Core 3.0 onward).
//
// Header-only and host-portable, so the native tests use it as the board does.

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace mm {

class JsonWriter {
 public:
  JsonWriter(char* out, size_t cap) : out_(out), cap_(cap) {
    if (cap_ > 0) out_[0] = '\0';
  }

  void raw(const char* text) {
    if (text == nullptr) return;
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
  // difference in bytes and not in value, and it cannot arise in an envelope whose every field
  // is ASCII and is held against a fixture.)
  void string(const char* text) {
    put('"');
    if (text != nullptr) {
      for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p != '\0'; p++) {
        escape(*p);
      }
    }
    put('"');
  }

  // The same, for text that is not NUL-terminated or is longer than it should be.
  void string(const char* text, size_t len) {
    put('"');
    if (text != nullptr) {
      for (size_t i = 0; i < len && text[i] != '\0'; i++) escape(static_cast<unsigned char>(text[i]));
    }
    put('"');
  }

  void integer(long long v) {
    char buf[24];
    std::snprintf(buf, sizeof buf, "%lld", v);
    raw(buf);
  }

  void boolean(bool v) { raw(v ? "true" : "false"); }

  // The shortest decimal that reads back as the same double, which is what System.Text.Json
  // writes. Every number this firmware sends (a frequency, an SNR, a voltage, a round trip) is
  // in a range where %g never chooses the exponent form, and the process never calls setlocale(),
  // so the decimal point is a point.
  void number(double v) {
    char buf[40];
    if (std::isnan(v) || std::isinf(v)) {
      raw("0");
      return;
    }
    for (int digits = 15; digits <= 17; digits++) {
      std::snprintf(buf, sizeof buf, "%.*g", digits, v);
      if (std::strtod(buf, nullptr) == v) break;
    }
    raw(buf);
  }

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
  void escape(unsigned char c) {
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

  char* out_;
  size_t cap_;
  size_t len_ = 0;
  bool overflow_ = false;
  bool first_ = true;
};

}  // namespace mm
