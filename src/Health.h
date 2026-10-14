// Staying up: the task watchdog, and why the board last restarted.
//
// A Pi agent is a systemd unit with `Restart=always` and a journal that outlives the process; a
// board at a window has neither, so this is both. Three things:
//
//   the watchdog   The ESP-IDF task watchdog, on the task that is this firmware - the Arduino
//                  loop task, which is the main loop *and* the network: every HTTPS call to
//                  meshmaster.dk is made from it. It is fed at the top of loop() and inside
//                  every wait a job takes (JobRunner::idle through the console pump), so a
//                  thirteen-minute region load does not trip it and a TLS call that never
//                  returns does. `kWatchdogSeconds` is well past the worst legitimate call - a
//                  30 s handshake, a 10 s connect and a 15 s read - so what trips it is a stack
//                  that is wedged, which is the one thing a board cannot talk its way out of.
//
//   the reboot     A wedged TLS stack usually does not hang: it fails, quickly, for ever. So the
//                  loop counts consecutive failures against a live WiFi link and, past a bound,
//                  reboots on purpose - noting *why* first. That is the cheapest thing that
//                  works, and it is what a person would do.
//
//   the log        Five reasons and times, in **RTC memory**, which survives a restart (it is
//                  not cleared by the bootloader and is not the heap). `reboots` on the console
//                  prints them, so a board found quiet at a window can say what happened to it
//                  without a network. A power cut clears it, and says so.
//
// The counters this keeps are what the heartbeat reports as `reboots` (EXIT-NODES §5's error
// counters) and what `errors` prints.

#pragma once

#include <cstddef>
#include <cstdint>

namespace mm {

// Longer than any call this firmware makes: the handshake (30 s), the connect (10 s) and the
// read (15 s) of src/agent/AgentClient.cpp, with room over. Anything past this is not slow, it
// is stuck.
constexpr uint32_t kWatchdogSeconds = 90;

// What earns a reboot - how many consecutive server failures, over how long - is the loop's
// policy and lives beside it: kRebootAfterFailures and kRebootAfterFailingMs in
// agent/AgentLoop.h (moved there by task p1.5-15, so that the loop includes nothing of the
// board). The loop asks for the restart through the platform seam (agent/IPlatform.h,
// rebootForHealth), which on this board is healthRebootNow() below.

constexpr int kRebootsRemembered = 5;
constexpr size_t kMaxRebootWhyLen = 63;

// Plain data, and deliberately so: these live in RTC memory, which is not initialised at boot
// (that is the point), so nothing here may have an initialiser to run.
struct RebootRecord {
  uint32_t atEpoch;   // the board's clock when it went down, or 0 if it had none
  uint32_t uptimeS;   // how long it had been up
  char why[kMaxRebootWhyLen + 1];
};

// At boot, before anything else can restart the board. `clock` returns UNIX seconds or 0;
// `resetReason` is what the board calls this reset (MeshCore's getResetReasonString). Starts the
// watchdog on the calling task, validates the RTC log and, when this reset was not one this
// firmware asked for, writes the reason it can infer into the log.
void healthBegin(uint32_t (*clock)(), const char* resetReason);

// Every pass of the loop, and inside every wait: feeds the watchdog and keeps the RTC copy of
// "the clock and the uptime, a moment ago" fresh, which is what a crash entry is timed from.
void healthTick();

// Write `why` into the log and mark the restart that follows as this firmware's own, so the next
// boot prints the reason rather than guessing one from the reset cause. The caller restarts.
void healthNoteReboot(const char* why);

// The same, and restart: this does not return.
[[noreturn]] void healthRebootNow(const char* why);

int rebootCount();                      // how many reasons are remembered, 0..kRebootsRemembered
const RebootRecord& rebootAt(int i);    // 0 is the newest
uint32_t rebootsSeen();                 // restarts this log has seen, across all of them
uint32_t selfReboots();                 // of those, the ones this firmware asked for
const char* bootReason();               // what the board called this boot's reset
bool rtcLogSurvived();                  // false after a power cut: the log starts empty

}  // namespace mm
