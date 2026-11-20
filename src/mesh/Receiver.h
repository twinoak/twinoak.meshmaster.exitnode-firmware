// The radio's receiver: the timer that puts it back, and the noise floor that follows one.
//
// From the evening of 19 November 2026 until it was restarted the next morning, the exit node at
// Risskov heard almost nothing that answered it, from repeaters that heard *it* at the same -96
// dBm all night. A receiver that stops hearing until its radio is restarted is what MeshCore
// resets a radio's AGC for, and the reset is a timer of its own dispatcher:
//
//   `Dispatcher::loop()` (src/Dispatcher.cpp at the pin, ~L122-136) goes back to receive mode
//   after every send and arms `next_agc_reset_time` from `getAGCResetInterval()`; when the timer
//   has run out and the interval is above 0 it calls the radio's `resetAGC()` and arms it again.
//   `Dispatcher::getAGCResetInterval()` (src/Dispatcher.h ~L172) answers **0 - disabled - unless
//   the application overrides it**, which this firmware never did. Upstream's own companion does
//   (`examples/companion_radio/MyMesh.h` ~L133, from a setting of four-second units), and the
//   standard profile gives the repeaters four seconds.
//
// So this file holds the number, in one place, and `AgentMesh::getAGCResetInterval()` is the only
// line of this firmware that has anything to do with the reset: **nothing of ours ever calls a
// radio's `resetAGC()` itself and there is no second timer of ours** (task p1.18-66, held by
// test/test_receiver reading the sources). The number comes from `agc.reset.interval`, which is
// the repeaters' own word for it, and main.cpp pushes the setting here at boot, on every
// `set agc.*` and on `erase confirm` - so a change takes effect without a restart.
//
// **The name and the unit are the repeaters' own.** `set agc.reset.interval <secs>` is what a
// repeater takes on its own CLI (`CommonRadioPrefs.cpp` ~L163-173 at the pin): whole **seconds**,
// which it then stores in units of four (`CommonCLI.h` ~L51, ~L122-123: `secs / 4`, answered back
// as `secs * 4`) and rounds in the reply. This firmware keeps the seconds as they were typed and
// does not round them, and its range is the task's 0..3600 rather than the repeater's 0..1020.
//
// **And the floor that follows a reset.** `RadioLibWrapper::resetAGC()`
// (src/helpers/radiolib/RadioLibWrappers.cpp ~L74-85) warm-sleeps the radio and then sets
// `_noise_floor = 0` so that the sampling starts from scratch; `getNoiseFloor()` answers that 0
// until `NUM_NOISE_FLOOR_SAMPLES` have been taken again. A noise floor is a negative number of
// dBm, so **0 is not a level - it is a receiver being measured**, and neither `status` nor the
// observer's status message may pass it off as one. That rule lives here too, because it is the
// reset's own consequence, and `status` and src/observer/Dialect.cpp both read it from here.
//
// Portable and host-tested (test/test_receiver): nothing of Arduino, of NVS or of MeshCore is in
// it, and nothing in it is a secret or was overheard - one interval of this board's own and one
// number its own receiver measured.

#pragma once

#include <stddef.h>

namespace mm {

// `agc.reset.interval`, in whole seconds: what the dispatcher's timer is set to, 0 being never.
// The range the console takes; 3600 is an hour, which is as good as never and is the top of it.
constexpr int kAgcResetMinSeconds = 0;
constexpr int kAgcResetMaxSeconds = 3600;

// What the board uses while the key is unset: four seconds, as the standard profile gives the
// repeaters. The same number is the key's `fallback` in src/settings/Keys.cpp, and
// test/test_console_parser holds the two together so they cannot drift.
constexpr int kAgcResetDefaultSeconds = 4;

// The one place the setting becomes the dispatcher's interval. main.cpp pushes whole seconds
// here (`applyAgcSettings`); a value outside the range above is clamped into it, because a
// number that reached NVS before this range existed is not a reason to run with no timer.
void setAgcResetSeconds(int seconds);

// What was pushed, in whole seconds.
int agcResetSeconds();

// What `AgentMesh::getAGCResetInterval()` answers: the same interval in **milliseconds**, and 0
// when the setting is 0, which is MeshCore's own word for *never reset*.
int agcResetIntervalMs();

// What `status`' `radio` line says of it, into `out` (NUL-terminated, never empty):
//
//   receiver reset every 4 s
//   receiver never reset
void agcResetPhrase(char* out, size_t cap);

// Is `dbm` a reading? A noise floor is negative dBm; 0 is the value MeshCore leaves behind a
// reset until the sampling has converged again, and -999 is this firmware's own *not measured*
// (src/observer/Observer.h, src/observer/Dialect.h). Neither is a level.
bool noiseFloorIsReading(int dbm);

// What `status`' `frames` line says of the floor, into `out` (NUL-terminated, never empty):
//
//   noise floor -112 dBm
//   noise floor being measured
void noiseFloorPhrase(int dbm, char* out, size_t cap);

}  // namespace mm
