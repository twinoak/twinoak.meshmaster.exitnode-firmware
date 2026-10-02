// `esp_system.h`, for the host build only (docs/host.md).
//
// Three calls of the ESP-IDF that src/agent reaches for: the hardware random number generator
// (every client id, every request's uniqueness blob), the free heap `status` prints, and the
// restart `Health.h` asks for. The host has a PRNG, a heap it does not count the same way, and a
// process to exit.

#pragma once

#include <cstdint>

// A 32-bit random number. On the board this is the RNG peripheral; here it is a seeded
// std::mt19937, seeded from std::random_device in src/host/HostPlatform.cpp.
uint32_t esp_random();

// What the board reports as free heap. There is no equivalent on a host, so this answers 0 and
// `status` prints it as the 0 it is rather than pretending to a number.
uint32_t esp_get_free_heap_size();

// `Health.h`'s restart. The host exits with a status of 70 (EX_SOFTWARE), which tools/host-run.ps1
// and CI read as "the agent asked to be restarted", because a harness run that reboots has found
// something.
[[noreturn]] void esp_restart();
