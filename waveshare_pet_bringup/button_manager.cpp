#include "button_manager.h"

void DebouncedButton::begin() {
  pinMode(pin_, INPUT_PULLUP);
  rawState_ = digitalRead(pin_) == LOW;
  debouncedState_ = rawState_;
  longPressFired_ = false;
  lastTransitionMs_ = millis();
}

ButtonSnapshot DebouncedButton::update() {
  ButtonSnapshot snapshot;
  const uint32_t now = millis();
  const bool sampled = digitalRead(pin_) == LOW;

  if (sampled != rawState_) {
    rawState_ = sampled;
    lastTransitionMs_ = now;
  }

  if ((now - lastTransitionMs_) >= kDebounceMs && debouncedState_ != rawState_) {
    debouncedState_ = rawState_;
    if (debouncedState_) {
      pressedAtMs_ = now;
      longPressFired_ = false;
      snapshot.pressed = true;
    } else {
      snapshot.released = true;
      if (!longPressFired_) {
        snapshot.shortPress = true;
      }
    }
  }

  if (debouncedState_ && !longPressFired_ && (now - pressedAtMs_) >= kLongPressMs) {
    longPressFired_ = true;
    snapshot.longPressStart = true;
  }

  snapshot.isDown = debouncedState_;
  return snapshot;
}
