#pragma once

#include <Arduino.h>

enum class PetEmotion : uint8_t {
  Neutral = 0,
  Happy,
  Sleepy,
  Surprised,
  Sad,
  Count
};

const char* emotionName(PetEmotion emotion);
PetEmotion nextEmotion(PetEmotion emotion);
