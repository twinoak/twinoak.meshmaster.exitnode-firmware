#include "Receiver.h"

#include <stdio.h>

namespace mm {

namespace {

// The interval in force. Four seconds before anything is pushed, which is both the key's
// fallback and what the standard profile gives the repeaters: a board whose settings could not
// be read is a board with the timer on, not a board with it off.
int theSeconds = kAgcResetDefaultSeconds;

}  // namespace

void setAgcResetSeconds(int seconds) {
  if (seconds < kAgcResetMinSeconds) seconds = kAgcResetMinSeconds;
  if (seconds > kAgcResetMaxSeconds) seconds = kAgcResetMaxSeconds;
  theSeconds = seconds;
}

int agcResetSeconds() { return theSeconds; }

int agcResetIntervalMs() { return theSeconds * 1000; }

void agcResetPhrase(char* out, size_t cap) {
  if (out == nullptr || cap == 0) return;
  out[0] = '\0';
  if (theSeconds <= 0) {
    snprintf(out, cap, "receiver never reset");
    return;
  }
  snprintf(out, cap, "receiver reset every %d s", theSeconds);
}

bool noiseFloorIsReading(int dbm) { return dbm != 0 && dbm > -999; }

void noiseFloorPhrase(int dbm, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return;
  out[0] = '\0';
  if (!noiseFloorIsReading(dbm)) {
    snprintf(out, cap, "noise floor being measured");
    return;
  }
  snprintf(out, cap, "noise floor %d dBm", dbm);
}

}  // namespace mm
