// The serial log: one entry point, a level, a clock.
//
// What it will and will not print is decided here for every task after this one:
//   - a line is "[HH:MM:SS] L text" once the clock is known, "[+123s] L text" before that;
//   - nothing the mesh says is ever an argument (the rails in README.md): frames are counted,
//     and `log.frames on` prints a header line per frame - type, route, lengths, SNR - never
//     a payload byte;
//   - no secret is ever an argument: the token and the passphrase are read from NVS into the
//     call that needs them and into nothing that is printed. There is no redactor here because
//     there is nothing to redact - the .NET agent redacts its log against secrets it holds as
//     strings; this firmware never holds one as a printable thing;
//   - every line also goes into the four-kilobyte ring `dump` prints (LogRing.h), which is a
//     board's answer to the Pi agent's journal: the same bytes the port carried, and no others.
//
// Where a formatted line goes is the platform's (agent/IPlatform.h, task p1.5-15): the Heltec
// leaves logSetSink() alone and gets its serial port and the ring; another platform sets a sink
// and gets every line the same way, formatted here.

#pragma once

#include <cstddef>
#include <cstdint>

namespace mm {

enum class LogLevel : uint8_t { Error = 0, Warn = 1, Info = 2, Debug = 3 };

// `clock` returns UNIX seconds, or 0 while there is no time yet.
void logBegin(uint32_t (*clock)());
void logSetLevel(LogLevel level);
bool logSetLevelByName(const char* name);  // "error" | "warn" | "info" | "debug"
LogLevel logLevel();
const char* logLevelName(LogLevel level);

void logf(LogLevel level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

// The sink one formatted line goes to - the stamped line, CR LF and all, and its length. Unset (the
// default, and the Heltec's), a line goes to the serial port and into the ring; set, it goes to
// the sink and nowhere else - the sink owns the ring then, if it wants one.
using LogSink = void (*)(const char* line, size_t len);
void logSetSink(LogSink sink);

}  // namespace mm

#define LOG_E(...) ::mm::logf(::mm::LogLevel::Error, __VA_ARGS__)
#define LOG_W(...) ::mm::logf(::mm::LogLevel::Warn, __VA_ARGS__)
#define LOG_I(...) ::mm::logf(::mm::LogLevel::Info, __VA_ARGS__)
#define LOG_D(...) ::mm::logf(::mm::LogLevel::Debug, __VA_ARGS__)
