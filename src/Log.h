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
//     strings; this firmware never holds one as a printable thing.

#pragma once

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

}  // namespace mm

#define LOG_E(...) ::mm::logf(::mm::LogLevel::Error, __VA_ARGS__)
#define LOG_W(...) ::mm::logf(::mm::LogLevel::Warn, __VA_ARGS__)
#define LOG_I(...) ::mm::logf(::mm::LogLevel::Info, __VA_ARGS__)
#define LOG_D(...) ::mm::logf(::mm::LogLevel::Debug, __VA_ARGS__)
