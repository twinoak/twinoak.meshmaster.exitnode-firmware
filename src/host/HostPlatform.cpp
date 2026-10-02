// The host build's Arduino core: the clock, the sleep, `Serial`, the RNG and the restart.
//
// Everything the shims of src/host/arduino declare, in one file. No board, no peripheral, no
// radio - a Linux process with a monotonic clock and two file descriptors.

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <thread>

#include "arduino/Arduino.h"
#include "arduino/Preferences.h"
#include "arduino/esp_system.h"

// ------------------------------------------------------------------------------- the clock

namespace {

using Clock = std::chrono::steady_clock;

Clock::time_point start() {
  static const Clock::time_point t0 = Clock::now();
  return t0;
}

std::mt19937& generator() {
  static std::mt19937 gen(std::random_device{}());
  return gen;
}

bool stdinNonBlocking() {
  static const bool done = [] {
    const int flags = ::fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags >= 0) ::fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    return true;
  }();
  return done;
}

// One character of look-ahead: Stream's available()/read() pair wants to be able to say "there is
// one" before handing it over, and a non-blocking read is the only way to find out.
int g_peeked = -1;

}  // namespace

uint32_t millis() {
  const auto delta = Clock::now() - start();
  return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(delta).count());
}

uint32_t micros() {
  const auto delta = Clock::now() - start();
  return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(delta).count());
}

void delay(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

void delayMicroseconds(uint32_t us) { std::this_thread::sleep_for(std::chrono::microseconds(us)); }

void yield() { std::this_thread::yield(); }

// ------------------------------------------------------------------------------- Serial

mm::host::HostSerial Serial;

namespace mm {
namespace host {

int HostSerial::available() {
  stdinNonBlocking();
  if (g_peeked >= 0) return 1;
  unsigned char c = 0;
  const ssize_t n = ::read(STDIN_FILENO, &c, 1);
  if (n == 1) {
    g_peeked = c;
    return 1;
  }
  return 0;
}

int HostSerial::read() {
  if (available() == 0) return -1;
  const int c = g_peeked;
  g_peeked = -1;
  return c;
}

size_t HostSerial::write(const uint8_t* buffer, size_t size) {
  return std::fwrite(buffer, 1, size, stdout);
}

void HostSerial::flush() { std::fflush(stdout); }

}  // namespace host
}  // namespace mm

// ------------------------------------------------------------------------------- Stream

size_t Stream::print(const char* text) {
  if (text == nullptr) return 0;
  const size_t len = std::strlen(text);
  return write(reinterpret_cast<const uint8_t*>(text), len);
}

size_t Stream::println(const char* text) { return print(text) + print("\r\n"); }

size_t Stream::printf(const char* fmt, ...) {
  char buf[1024];
  va_list args;
  va_start(args, fmt);
  const int n = std::vsnprintf(buf, sizeof buf, fmt, args);
  va_end(args);
  if (n <= 0) return 0;
  const size_t len = static_cast<size_t>(n) < sizeof buf ? static_cast<size_t>(n) : sizeof buf - 1;
  return write(reinterpret_cast<const uint8_t*>(buf), len);
}

// ------------------------------------------------------------------------------- esp_system

uint32_t esp_random() { return static_cast<uint32_t>(generator()()); }

uint32_t esp_get_free_heap_size() { return 0; }

void esp_restart() {
  std::fflush(stdout);
  std::_Exit(70);
}

// ------------------------------------------------------------------------------- Preferences

bool Preferences::begin(const char*, bool) {
  open_ = true;
  return true;
}

int Preferences::find(const char* key) const {
  if (key == nullptr) return -1;
  for (int i = 0; i < mm::host::kMaxHostPrefs; i++) {
    if (entries_[i].set && std::strcmp(entries_[i].key, key) == 0) return i;
  }
  return -1;
}

bool Preferences::isKey(const char* key) const { return open_ && find(key) >= 0; }

size_t Preferences::getString(const char* key, char* out, size_t cap) const {
  if (!open_ || out == nullptr || cap == 0) return 0;
  out[0] = '\0';
  const int at = find(key);
  if (at < 0) return 0;
  std::snprintf(out, cap, "%s", entries_[at].value);
  return std::strlen(out);
}

size_t Preferences::putString(const char* key, const char* value) {
  if (!open_ || key == nullptr || value == nullptr) return 0;
  if (std::strlen(key) > mm::host::kMaxHostPrefKeyLen) return 0;
  int at = find(key);
  if (at < 0) {
    for (int i = 0; i < mm::host::kMaxHostPrefs && at < 0; i++) {
      if (!entries_[i].set) at = i;
    }
  }
  if (at < 0) return 0;
  entries_[at].set = true;
  std::snprintf(entries_[at].key, sizeof entries_[at].key, "%s", key);
  std::snprintf(entries_[at].value, sizeof entries_[at].value, "%s", value);
  return std::strlen(entries_[at].value) + 1;  // NVS answers the bytes written, NUL included
}

bool Preferences::clear() {
  if (!open_) return false;
  for (auto& entry : entries_) entry = Entry();
  return true;
}
