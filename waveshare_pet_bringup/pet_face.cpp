#include "pet_face.h"

const char* emotionName(PetEmotion emotion) {
  switch (emotion) {
    case PetEmotion::Neutral:
      return "neutral";
    case PetEmotion::Happy:
      return "happy";
    case PetEmotion::Sleepy:
      return "sleepy";
    case PetEmotion::Surprised:
      return "surprised";
    case PetEmotion::Sad:
      return "sad";
    default:
      return "unknown";
  }
}

PetEmotion nextEmotion(PetEmotion emotion) {
  uint8_t next = (static_cast<uint8_t>(emotion) + 1) % static_cast<uint8_t>(PetEmotion::Count);
  return static_cast<PetEmotion>(next);
}
