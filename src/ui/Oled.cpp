#include "Oled.h"

#include <Arduino.h>

#include <cstring>

namespace mm {

bool Oled::begin(DisplayDriver* display, MomentaryButton* button, Fill fill) {
  display_ = display;
  button_ = button;
  fill_ = fill;
  if (display_ == nullptr) return false;
  if (button_ != nullptr) button_->begin();
  display_->turnOn();
  offAt_ = millis() + kAutoOffMs;
  nextRefreshAt_ = 0;
  render();
  return true;
}

void Oled::render() {
  if (display_ == nullptr || fill_ == nullptr) return;
  char lines[kLines][kCols];
  std::memset(lines, 0, sizeof lines);
  fill_(lines);
  display_->startFrame();
  display_->setTextSize(1);
  display_->setColor(UIColor::primary_txt);
  for (int i = 0; i < kLines; i++) {
    lines[i][kCols - 1] = '\0';
    display_->setCursor(0, 2 + i * 16);
    display_->print(lines[i]);
  }
  display_->endFrame();
}

void Oled::loop() {
  if (display_ == nullptr) return;
  const uint32_t now = millis();

  if (button_ != nullptr && button_->check() != BUTTON_EVENT_NONE) {
    if (!display_->isOn()) display_->turnOn();
    offAt_ = now + kAutoOffMs;
    nextRefreshAt_ = now;
  }

  if (!display_->isOn()) return;

  if (static_cast<int32_t>(now - offAt_) >= 0) {
    display_->turnOff();
    return;
  }
  if (static_cast<int32_t>(now - nextRefreshAt_) >= 0) {
    render();
    nextRefreshAt_ = now + kRefreshMs;
  }
}

}  // namespace mm
