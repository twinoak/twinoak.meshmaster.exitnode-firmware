// What one answer from meshmaster.dk means for the loop, as a pure function of the status code.
//
// The rules are EXIT-NODES §6 and API §3 as the .NET agent reads them (ServerAnswer in
// Uplink.cs), and tasks/phase-1/02 names three of them outright: a 401 stops everything but the
// console, a 429 honours Retry-After, a 5xx backs off. They live here rather than inside
// src/agent/AgentLoop.cpp so that test/test_observations can hold every status code against the
// action it earns without a board.

#pragma once

#include <cstdint>

namespace mm {

enum class ServerAction : uint8_t {
  Ok,              // 2xx: take what the reply says and reset the backoff
  Halt,            // 401: the token is revoked or wrong, and retrying fixes neither
  WaitRetryAfter,  // 429: the wait is the answer's own, to the second
  Discard,         // a 4xx that will not get better: what was being sent is dropped, not retried
  BackOff,         // 5xx, or no reply at all: try again later, more slowly each time
};

// `reachable` is false when no reply arrived at all - DNS, TCP, the TLS handshake, a timeout -
// and then `httpStatus` is not read.
ServerAction actionFor(bool reachable, int httpStatus);

// The wait a 429 earns: its Retry-After in milliseconds, bounded at an hour, and one minute when
// the header was absent or was an HTTP date rather than a count of seconds.
uint32_t retryAfterMs(uint32_t retryAfterS);

// The wait a failure earns: half a minute, doubling, never more than ten minutes. 0 in means
// "the first failure since the last answer".
uint32_t nextBackoffMs(uint32_t previousMs);

constexpr uint32_t kFirstBackoffMs = 30000;
constexpr uint32_t kMaxBackoffMs = 10 * 60000;
constexpr uint32_t kRetryAfterWhenUnstatedMs = 60000;
constexpr uint32_t kMaxRetryAfterMs = 60 * 60000;

}  // namespace mm
