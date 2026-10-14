#include "Esp32Platform.h"

#include <Arduino.h>
#include <esp_system.h>

#include "Health.h"

namespace mm {

uint32_t Esp32Platform::millis() { return ::millis(); }

void Esp32Platform::delayMs(uint32_t ms) { ::delay(ms); }

uint32_t Esp32Platform::random() { return esp_random(); }

uint32_t Esp32Platform::freeHeap() { return ESP.getFreeHeap(); }

uint32_t Esp32Platform::minFreeHeap() { return ESP.getMinFreeHeap(); }

void Esp32Platform::rebootForHealth(const char* why) { healthRebootNow(why); }

uint32_t Esp32Platform::selfReboots() { return mm::selfReboots(); }

}  // namespace mm
