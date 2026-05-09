#pragma once

#include <Arduino.h>

struct ButtonSnapshot {
  bool pressed = false;
  bool released = false;
  bool shortPress = false;
  bool longPressStart = false;
  bool isDown = false;
};

class DebouncedButton {
 public:
  explicit DebouncedButton(uint8_t pin) : pin_(pin) {}

  void begin();
  ButtonSnapshot update();

 private:
  static constexpr uint32_t kDebounceMs = 25;
  static constexpr uint32_t kLongPressMs = 250;

  uint8_t pin_;
  bool rawState_ = false;
  bool debouncedState_ = false;
  bool longPressFired_ = false;
  uint32_t lastTransitionMs_ = 0;
  uint32_t pressedAtMs_ = 0;
};
