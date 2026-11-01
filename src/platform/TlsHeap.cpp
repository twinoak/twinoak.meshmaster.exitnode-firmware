#include "TlsHeap.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

// The core's pair (mbedtls/esp_config.h: MBEDTLS_PLATFORM_STD_CALLOC/FREE). They are C symbols
// that the core's esp_mem.h declares without an extern "C" - WiFiClientSecure.h pulls that
// header in with C++ linkage elsewhere in this image - so they are reached by their assembler
// names rather than through a declaration that would conflict with the core's.
void* coreMbedtlsCalloc(size_t n, size_t size) asm("esp_mbedtls_mem_calloc");
void coreMbedtlsFree(void* ptr) asm("esp_mbedtls_mem_free");
#include <mbedtls/platform.h>

namespace mm {
namespace tlsheap {

namespace {

volatile bool g_watching = false;
volatile uint32_t g_lowest = 0xFFFFFFFFu;
bool g_installed = false;

#if defined(BOARD_HAS_PSRAM)
volatile uint32_t g_routed = 0;
volatile uint32_t g_fallbacks = 0;
#endif

void noteFree() {
  const uint32_t f = ESP.getFreeHeap();
  if (f < g_lowest) g_lowest = f;
}

void* wrappedCalloc(size_t n, size_t size) {
  void* p = nullptr;
#if defined(BOARD_HAS_PSRAM)
  if (n * size >= kPsramFromBytes && psramFound()) {
    p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p != nullptr) {
      g_routed = g_routed + 1;
    } else {
      g_fallbacks = g_fallbacks + 1;
    }
  }
#endif
  if (p == nullptr) p = coreMbedtlsCalloc(n, size);
  if (g_watching) noteFree();
  return p;
}

// free() on this core is heap_caps_free, which gives a block back to whichever heap it came
// from, PSRAM included; the core's own esp_mbedtls_mem_free is that free.
void wrappedFree(void* p) { coreMbedtlsFree(p); }

}  // namespace

void begin() {
  if (g_installed) return;
  mbedtls_platform_set_calloc_free(&wrappedCalloc, &wrappedFree);
  g_installed = true;
}

void watchBegin(uint32_t freeNow) {
  g_lowest = freeNow;
  g_watching = true;
}

void sample() {
  if (g_watching) noteFree();
}

uint32_t watchEnd() {
  g_watching = false;
  return g_lowest;
}

bool psramInUse() {
#if defined(BOARD_HAS_PSRAM)
  return psramFound();
#else
  return false;
#endif
}

uint32_t psramRouted() {
#if defined(BOARD_HAS_PSRAM)
  return g_routed;
#else
  return 0;
#endif
}

uint32_t psramFallbacks() {
#if defined(BOARD_HAS_PSRAM)
  return g_fallbacks;
#else
  return 0;
#endif
}

}  // namespace tlsheap
}  // namespace mm
