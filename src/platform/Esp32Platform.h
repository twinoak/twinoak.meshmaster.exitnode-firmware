// The Arduino-on-ESP32 implementation of the platform seam (src/agent/IPlatform.h): the calls
// JobRunner and AgentLoop made directly before task p1.5-15, behind the interface the library
// takes. Board-only; the clock half is AgentClock (src/net/Clock.h), which implements IClock.

#pragma once

#include <cstdint>

#include "agent/IPlatform.h"
#include "net/Clock.h"

namespace mm {

class Esp32Platform : public IPlatform {
 public:
  explicit Esp32Platform(AgentClock& clock) : clock_(clock) {}

  uint32_t millis() override;
  void delayMs(uint32_t ms) override;
  uint32_t random() override;
  IClock& clock() override { return clock_; }
  uint32_t freeHeap() override;
  uint32_t minFreeHeap() override;
  [[noreturn]] void rebootForHealth(const char* why) override;
  uint32_t selfReboots() override;

 private:
  AgentClock& clock_;
};

}  // namespace mm
