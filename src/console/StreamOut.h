// An Arduino Stream behind the library's TextOut (agent/TextOut.h): what main.cpp hands the
// runner's and the loop's console reports, so that those files need no Stream of their own.
// Board-only.

#pragma once

#include <Stream.h>

#include <cstddef>
#include <cstdint>

#include <agent/TextOut.h>

namespace mm {

class StreamOut : public TextOut {
 public:
  explicit StreamOut(Stream& out) : out_(out) {}
  void write(const char* text, size_t len) override { out_.write(reinterpret_cast<const uint8_t*>(text), len); }

 private:
  Stream& out_;
};

}  // namespace mm
