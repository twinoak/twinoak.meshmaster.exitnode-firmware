// `Stream`, for the host build only (docs/host.md).
//
// The firmware's console and its three reports (`status`, `jobs`, `server`) take an Arduino
// `Stream&` and write to it. On the board that is `Serial`; here it is this, and the one instance
// of it is stdin/stdout - so `pio run -e host` gives the same console, driven from a pipe or a
// terminal, over the same `src/console/Console.cpp` and the same `src/console/Parser.cpp`.
//
// Only what the firmware calls is here. This is not a port of Arduino's Stream and must never
// grow into one: a method the board build uses and this file lacks is a compile error in the host
// build, which is the whole point of the arrangement.

#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>

class Stream {
 public:
  virtual ~Stream() = default;

  // What the board's HardwareSerial has, as the console and the reports use it.
  virtual int available() = 0;
  virtual int read() = 0;
  virtual size_t write(const uint8_t* buffer, size_t size) = 0;
  virtual void flush() = 0;

  size_t write(uint8_t b) { return write(&b, 1); }

  size_t print(const char* text);
  size_t print(char c) { return write(static_cast<uint8_t>(c)); }
  size_t println(const char* text);
  size_t println() { return print("\r\n"); }
  size_t printf(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
};
