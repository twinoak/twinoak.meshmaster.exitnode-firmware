// The OLED: the four lines of DisplayLines.h - the identity's prefix and the clock, WiFi and
// Online, the last heartbeat, and the job that is open or the reason none is - drawn once a
// second.
//
// **It stays on.** The panel is lit at boot and stays lit: `display.timeout` is the number of
// seconds without a button press after which it goes dark, and its default is `0`, which never
// turns it off (Thomas, 4 October 2026: "I'd like that the display on the Heltec doesn't turn
// off"). `set display.timeout 300` is the five minutes this firmware did until task p1.2-09, and
// it takes effect without a reboot - main.cpp re-reads the setting and calls setTimeout the way
// it re-applies the radio and the log settings.
//
// **The button toggles it.** A click on the user button (PRG, GPIO 0) turns the display off when
// it is on and on when it is off, and restarts the timeout when one is set. The other events
// MomentaryButton reports - long, double and triple - are deliberately unused. Holding GPIO 0
// through a reset is the ESP32-S3's bootloader, which is the chip's ROM and not this loop: a
// click here cannot put the board into it, and nothing this class does makes the bootloader
// easier or harder to reach.

#pragma once

#include <cstdint>

#include "mesh/MeshCoreHeaders.h"
#include "ui/DisplayLines.h"

namespace mm {

class Oled {
 public:
  static constexpr int kLines = kDisplayLines;
  static constexpr int kCols = kDisplayCols;
  using Fill = void (*)(char lines[kLines][kCols]);

  static constexpr uint32_t kRefreshMs = 1000;

  bool begin(DisplayDriver* display, MomentaryButton* button, Fill fill);
  void loop();
  bool isOn() const { return display_ != nullptr && display_->isOn(); }

  // `display.timeout`, in seconds; 0 never turns the panel off. Read at begin() and again on
  // every `set display.timeout`, so no reboot is part of changing it. A timeout that arrives
  // while the panel is lit starts counting from now.
  void setTimeout(uint32_t seconds);
  uint32_t timeoutSeconds() const { return timeoutMs_ / 1000; }

 private:
  void render();
  void restartTimeout(uint32_t now);

  DisplayDriver* display_ = nullptr;
  MomentaryButton* button_ = nullptr;
  Fill fill_ = nullptr;
  uint32_t timeoutMs_ = 0;  // 0: never off, which is the default
  uint32_t offAt_ = 0;
  uint32_t nextRefreshAt_ = 0;
};

}  // namespace mm
