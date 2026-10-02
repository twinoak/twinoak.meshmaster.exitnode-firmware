// `Arduino.h`, for the host build only (docs/host.md).
//
// What the host build of src/ actually asks the Arduino core for, and nothing else: a
// millisecond clock, a sleep, and `Serial`. `src/host/HostPlatform.cpp` is the implementation.
//
// The clock is real time, not a virtual one. The airtime budget of EXIT-NODES §8 is minutes of
// waiting per job and it would be tempting to make it instant - but the lease of a claim is real
// seconds on the server's clock, and a board that waited faster than the world would read every
// lease as lapsed. So the harness waits: against the recorded stub the budget is a loose one and
// a cycle is seconds, and against the compose stack it is the server's own and a config.read
// takes the ten minutes it takes (tools/host-run.ps1 says so, and prints the time).

#pragma once

#include <cstdint>

#include "Stream.h"

namespace mm {
namespace host {

// The one Serial: stdin and stdout, line-buffered, non-blocking on the read.
class HostSerial : public Stream {
 public:
  int available() override;
  int read() override;
  size_t write(const uint8_t* buffer, size_t size) override;
  void flush() override;
};

}  // namespace host
}  // namespace mm

extern mm::host::HostSerial Serial;

uint32_t millis();
uint32_t micros();
void delay(uint32_t ms);
void delayMicroseconds(uint32_t us);
void yield();
