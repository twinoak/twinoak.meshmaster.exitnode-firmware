// Where mbedtls gets its memory on this board, and what a call costs (tasks p1.8-30 and 31).
//
// mbedtls's allocator as this core ships it is MBEDTLS_PLATFORM_MEMORY with esp_mbedtls_mem_calloc
// and esp_mbedtls_mem_free as the standard pair (mbedtls/esp_config.h), behind the function
// pointers mbedtls_platform_set_calloc_free() sets - which this file sets, once, to a wrapper of
// the pair that does two things:
//
//   the watch     while an uplink call is in flight (watchBegin .. watchEnd), every allocation
//                 mbedtls makes - the handshake's certificate chain, the two 16 KB content
//                 buffers, the session's keys - is followed by a read of the free internal
//                 heap, so that the lowest free heap during a call is exact for mbedtls's share
//                 of it. Task 30's measurement; src/agent/AgentClient.cpp reads the result.
//   PSRAM         on a board built with BOARD_HAS_PSRAM (the Heltec V4, boards/heltec_v4.json),
//                 an allocation of kPsramFromBytes and over goes to PSRAM, with the internal heap
//                 as the fallback when PSRAM has no room; smaller ones stay internal. That is the
//                 line the core itself draws for malloc (CONFIG_SPIRAM_USE_MALLOC with
//                 CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL 4096 in its sdkconfig) - which mbedtls does
//                 not get, because the core builds it with CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC, the
//                 pair above calling heap_caps_calloc(MALLOC_CAP_INTERNAL). The ESP-IDF's own
//                 alternative, CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC, puts all of mbedtls in PSRAM;
//                 this is that choice for the large allocations only, made at run time through
//                 the hook rather than with a custom ESP-IDF build. The ESP32-S3's AES and SHA
//                 engines reach PSRAM through their DMA (SOC_PSRAM_DMA_CAPABLE in the core's
//                 soc_caps.h), which is what makes a content buffer there usable at all; the
//                 bench is where a handshake over PSRAM buffers is seen to work.
//
// The wrapper is global: the uplink's WiFiClientSecure and the observer's esp-mqtt tasks share
// one mbedtls, so both are watched while a call is in flight (what is wanted is the lowest
// moment, whoever asked for the bytes) and both get their buffers in PSRAM on a V4. The two
// watch variables are volatile with a non-atomic compare: a lower figure lost to a race between
// two tasks is at worst one sample, never a wrong number.
//
// Board-only.

#pragma once

#include <cstddef>
#include <cstdint>

namespace mm {
namespace tlsheap {

// Allocations of this size and over go to PSRAM where there is some: the core's own threshold
// for malloc (CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL), and src/observer/Connection.h's
// kPsramThresholdBytes, which the gate counts the internal block against.
constexpr size_t kPsramFromBytes = 4096;

// Install the wrapper. Idempotent.
void begin();

// Start watching: the lowest free internal heap seen from here is `freeNow` or less.
void watchBegin(uint32_t freeNow);
// One more sample of the free internal heap while watching (the uplink's own four, for what
// mbedtls does not allocate). Nothing when not watching.
void sample();
// Stop watching and return the lowest free internal heap seen.
uint32_t watchEnd();

// True when this image routes large mbedtls allocations to PSRAM and PSRAM was found at boot.
bool psramInUse();
// Allocations routed to PSRAM since boot, and the ones that fell back to the internal heap
// because PSRAM had no room - the second should read 0 on a healthy V4.
uint32_t psramRouted();
uint32_t psramFallbacks();

}  // namespace tlsheap
}  // namespace mm
