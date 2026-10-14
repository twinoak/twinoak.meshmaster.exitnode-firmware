// The Heltec's log sink: the serial port and the four-kilobyte ring (src/Log.cpp).
//
// The formatting is the library's (agent/Log.h, task p1.5-16) and the port is the board's, so
// this is the one line of wiring between them. `logBeginSerial` is what main.cpp calls instead
// of `mm::logBegin`: it sets the sink and hands the library Arduino's millis() for the "[+123s]"
// stamp a board prints before its clock is known.

#pragma once

#include <cstddef>
#include <cstdint>

namespace mm {

// One formatted line, CR LF and all, to the serial port and then into the ring `dump` prints.
void serialLogSink(const char* full, size_t len);

// `clock` returns UNIX seconds, or 0 while there is no time yet (agent/Log.h's logBegin).
void logBeginSerial(uint32_t (*clock)());

}  // namespace mm
