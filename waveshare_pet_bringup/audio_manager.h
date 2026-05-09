#pragma once

#include <Arduino.h>
#include <driver/i2s.h>

#include "pet_face.h"

class AudioManager {
 public:
  bool begin();
  void setAmplifierEnabled(bool enabled);

  bool startRecording();
  void captureStep();
  bool finishRecording();
  bool hasRecording() const;
  size_t recordedBytes() const;
  void shutdown();
  void playBeep(uint16_t frequencyHz, uint16_t durationMs);
  void playRecording();
  void playEmotionTone(PetEmotion emotion);

 private:
  bool initCodec();
  bool initI2S();
  bool writeCodecRegister(uint8_t reg, uint8_t value);
  uint8_t readCodecRegister(uint8_t reg);
  void startCodecPath(bool enableAdc, bool enableDac);
  void stopCodecPath();
  void writeChunk(const uint8_t* data, size_t bytes);
  void synthTone(uint16_t frequencyHz, uint16_t durationMs);

  static constexpr i2s_port_t kI2SPort = I2S_NUM_0;
  static constexpr uint32_t kSampleRate = 16000;
  static constexpr size_t kChannels = 2;
  static constexpr size_t kBytesPerSample = 2;
  static constexpr size_t kBytesPerFrame = kChannels * kBytesPerSample;
  static constexpr size_t kMaxSeconds = 8;
  static constexpr size_t kMaxBytes = kSampleRate * kMaxSeconds * kBytesPerFrame;
  static constexpr size_t kChunkBytes = 1024;

  uint8_t* buffer_ = nullptr;
  size_t recordedBytes_ = 0;
  bool isRecording_ = false;
};
