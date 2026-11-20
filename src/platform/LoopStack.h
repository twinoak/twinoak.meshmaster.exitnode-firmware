// The loop task's stack: how big it is, and the least of it that was ever free (task p1.17-65).
//
// Everything this firmware does in `setup()` and `loop()` runs on one FreeRTOS task - the Arduino
// core's `loopTask` - and that includes the TLS handshake, a release's signature and the fetch of
// a 1.4 MB image. 0.7.3 did not start because three copies of an update's record did not fit the
// 8 192 bytes that task had; it has 16 384 since (`CONFIG_ARDUINO_LOOP_STACK_SIZE`,
// sdkconfig.defaults), and nobody had ever measured what is left of them. So the board says:
//
//   * one line of the boot log, when `setup()` ends;
//   * a `stack` line in `status`, beside the ones the observer's two tasks have.
//
// **Both are this one function's words.** The two numbers come from a seam - the configuration and
// FreeRTOS's own high-water mark on a board (src/main.cpp), a test's own figures on a host - so
// that the line a person reads at the window is the line a test holds.
//
// Portable and host-tested (test/test_loop_stack). Nothing here is a secret and nothing here is
// overheard: two byte counts of this board's own RAM.

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace mm {
namespace platform {

// The loop task's stack. `sizeBytes` 0 is *nothing to say*: no seam has been set, which on a
// board cannot happen after `setup()` has begun and on a host is the default.
struct LoopStack {
  uint32_t sizeBytes = 0;
  // The smallest number of bytes that was ever free on the task, as FreeRTOS has watched it
  // since the task began. 0 before anything was measured.
  uint32_t leastEverFree = 0;
};

// Where the two numbers come from. The board sets its own in `setup()`; a test sets its own.
typedef LoopStack (*LoopStackReader)();
void readLoopStackWith(LoopStackReader reader);

// The one line the boot log and `status` both print, into `out` (NUL-terminated, never empty):
//
//   loop task: 16384 bytes, the least ever free 11240
//   loop task: 16384 bytes; nothing measured yet
//   loop task: nothing measured yet
//
// The caller puts it behind its own prefix - `stack: ` in the log, `stack     ` in `status`.
void loopStackLine(char* out, size_t cap);

}  // namespace platform
}  // namespace mm
