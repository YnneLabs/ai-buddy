#pragma once

#include <Arduino.h>
#include <driver/i2s_std.h>

class AudioManager {
 public:
  bool begin();
  bool ready() const;
  bool startRecording();
  void captureStep();
  bool finishRecording();
  bool isRecording() const;
  bool hasRecording() const;
  size_t recordedBytes() const;
  void playRecording();
  void playTone(uint16_t frequencyHz, uint16_t durationMs);
  void playStartupChime();
  void shutdown();

 private:
  bool initCodec();
  bool initI2S();
  bool writeCodecRegister(uint8_t reg, uint8_t value);
  uint8_t readCodecRegister(uint8_t reg);
  void startCodecPath(bool enableAdc, bool enableDac);
  void stopCodecPath();
  void setAmplifierEnabled(bool enabled);
  void writeChunk(const uint8_t* data, size_t bytes);
  void synthTone(uint16_t frequencyHz, uint16_t durationMs);

  static constexpr uint32_t kSampleRate = 16000;
  static constexpr size_t kChannels = 2;
  static constexpr size_t kBytesPerSample = 2;
  static constexpr size_t kBytesPerFrame = kChannels * kBytesPerSample;
  static constexpr size_t kMaxSeconds = 8;
  static constexpr size_t kMaxBytes = kSampleRate * kMaxSeconds * kBytesPerFrame;
  static constexpr size_t kChunkBytes = 1024;
  static constexpr uint16_t kAmplifierStartupMs = 140;

  uint8_t* buffer_ = nullptr;
  size_t recordedBytes_ = 0;
  bool recording_ = false;
  bool ready_ = false;
  i2s_chan_handle_t txChannel_ = nullptr;
  i2s_chan_handle_t rxChannel_ = nullptr;
};
