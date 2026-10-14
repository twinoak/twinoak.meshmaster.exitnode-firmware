// The Heltec's end of the log: the serial port and the ring, and nothing about formatting.
//
// Everything that makes a line - the level, the stamp, the one vsnprintf - is the library's
// (lib/agent-core/src/Log.cpp) since task p1.5-16, because it is portable and a serial port is
// not. What is left here is the sink the library hands a finished line to, which on this board
// is `Serial` and the four-kilobyte ring `dump` prints: the same bytes, in that order, as every
// task before this one printed.

#include "SerialLog.h"

#include <Arduino.h>

#include <agent/Log.h>
#include <agent/LogRing.h>

namespace mm {

namespace {

uint32_t uptimeMs() { return millis(); }

}  // namespace

void serialLogSink(const char* full, size_t len) {
  Serial.print(full);
  // And the same bytes into the ring `dump` prints (agent/LogRing.h): whatever redaction the line
  // has had, it has had before this point - a line that could carry a secret here would already
  // have carried it to the port.
  logRing().add(full, len);
}

void logBeginSerial(uint32_t (*clock)()) {
  logSetSink(&serialLogSink);
  logBegin(clock, &uptimeMs);
}

}  // namespace mm
