// The OLED: four lines - WiFi state and IP, time or "no time", server state, the identity's
// first eight hex characters. Off after five minutes without a button press, on with the button.

#pragma once

#include <cstdint>

#include "mesh/MeshCoreHeaders.h"

namespace mm {

class Oled {
 public:
  static constexpr int kLines = 4;
  static constexpr int kCols = 22;  // 21 characters of the 6x8 font across 128 pixels, and a NUL
  using Fill = void (*)(char lines[kLines][kCols]);

  static constexpr uint32_t kAutoOffMs = 5 * 60000;
  static constexpr uint32_t kRefreshMs = 1000;

  bool begin(DisplayDriver* display, MomentaryButton* button, Fill fill);
  void loop();
  bool isOn() const { return display_ != nullptr && display_->isOn(); }

 private:
  void render();

  DisplayDriver* display_ = nullptr;
  MomentaryButton* button_ = nullptr;
  Fill fill_ = nullptr;
  uint32_t offAt_ = 0;
  uint32_t nextRefreshAt_ = 0;
};

}  // namespace mm
