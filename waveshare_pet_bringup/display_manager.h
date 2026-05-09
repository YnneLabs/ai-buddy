#pragma once

#include <Arduino.h>

#include "pet_face.h"

enum class OverlayState : uint8_t {
  None = 0,
  Recording,
  Playing
};

class DisplayManager {
 public:
  bool begin();
  void renderHome(const String& timeText,
                  const String& climateText,
                  PetEmotion emotion,
                  OverlayState overlay,
                  bool forceFullRefresh);
  void renderSleep(const String& timeText, PetEmotion emotion);
  void sleep();

 private:
  void drawFrame(const String& timeText, const String& climateText, PetEmotion emotion, OverlayState overlay);
  void drawFace(PetEmotion emotion);
  void drawOverlay(OverlayState overlay);
  void drawIcon();
  void drawEmotionLabel(PetEmotion emotion);
};
