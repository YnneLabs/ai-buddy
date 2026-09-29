#include "audio_manager.h"

#include <Wire.h>
#include <esp_heap_caps.h>
#include <math.h>

#include "board_pins.h"

namespace {

constexpr uint8_t kRegReset = 0x00;
constexpr uint8_t kRegClock1 = 0x01;
constexpr uint8_t kRegClock2 = 0x02;
constexpr uint8_t kRegClock3 = 0x03;
constexpr uint8_t kRegClock4 = 0x04;
constexpr uint8_t kRegClock5 = 0x05;
constexpr uint8_t kRegClock6 = 0x06;
constexpr uint8_t kRegClock7 = 0x07;
constexpr uint8_t kRegClock8 = 0x08;
constexpr uint8_t kRegSdpIn = 0x09;
constexpr uint8_t kRegSdpOut = 0x0A;
constexpr uint8_t kRegSystem0B = 0x0B;
constexpr uint8_t kRegSystem0C = 0x0C;
constexpr uint8_t kRegSystem0D = 0x0D;
constexpr uint8_t kRegSystem0E = 0x0E;
constexpr uint8_t kRegSystem10 = 0x10;
constexpr uint8_t kRegSystem11 = 0x11;
constexpr uint8_t kRegSystem12 = 0x12;
constexpr uint8_t kRegSystem13 = 0x13;
constexpr uint8_t kRegSystem14 = 0x14;
constexpr uint8_t kRegAdc15 = 0x15;
constexpr uint8_t kRegAdc16 = 0x16;
constexpr uint8_t kRegAdc17 = 0x17;
constexpr uint8_t kRegAdc1B = 0x1B;
constexpr uint8_t kRegAdc1C = 0x1C;
constexpr uint8_t kRegDac31 = 0x31;
constexpr uint8_t kRegDac32 = 0x32;
constexpr uint8_t kRegDac37 = 0x37;
constexpr uint8_t kRegGpio44 = 0x44;
constexpr uint8_t kRegGp45 = 0x45;
constexpr uint8_t kDacVolume80Percent = 0xCC;

}  // namespace

bool AudioManager::begin() {
  buffer_ = static_cast<uint8_t*>(heap_caps_malloc(kMaxBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (buffer_ == nullptr) {
    buffer_ = static_cast<uint8_t*>(heap_caps_malloc(kMaxBytes, MALLOC_CAP_8BIT));
  }
  if (buffer_ == nullptr) {
    return false;
  }

  pinMode(board::PIN_AUDIO_PWR, OUTPUT);
  pinMode(board::PIN_AUDIO_CTRL, OUTPUT);
  setAmplifierEnabled(false);
  ready_ = initCodec() && initI2S();
  if (!ready_) {
    shutdown();
  }
  return ready_;
}

bool AudioManager::ready() const { return ready_; }

bool AudioManager::startRecording() {
  if (!ready_ || recording_) {
    return false;
  }
  recordedBytes_ = 0;
  recording_ = true;
  setAmplifierEnabled(false);
  stopCodecPath();
  startCodecPath(true, false);
  return true;
}

void AudioManager::captureStep() {
  if (!recording_ || recordedBytes_ >= kMaxBytes) {
    return;
  }
  const size_t remaining = kMaxBytes - recordedBytes_;
  const size_t requested = min(remaining, kChunkBytes);
  size_t received = 0;
  i2s_channel_read(rxChannel_, buffer_ + recordedBytes_, requested, &received, 20);
  recordedBytes_ += received;
}

bool AudioManager::finishRecording() {
  if (!recording_) {
    return false;
  }
  recording_ = false;
  stopCodecPath();
  return true;
}

bool AudioManager::isRecording() const { return recording_; }

bool AudioManager::hasRecording() const { return recordedBytes_ > 0; }

size_t AudioManager::recordedBytes() const { return recordedBytes_; }

const uint8_t* AudioManager::recordingData() const { return buffer_; }

bool AudioManager::beginRemoteAudio(size_t expectedBytes) {
  if (!ready_ || expectedBytes == 0 || expectedBytes > kMaxBytes || expectedBytes % kBytesPerFrame != 0) {
    return false;
  }
  remoteAudioBytes_ = 0;
  remoteAudioExpectedBytes_ = expectedBytes;
  return true;
}

bool AudioManager::appendRemoteAudio(const uint8_t* data, size_t bytes) {
  if (data == nullptr || remoteAudioExpectedBytes_ == 0 || remoteAudioBytes_ + bytes > remoteAudioExpectedBytes_) {
    return false;
  }
  memcpy(buffer_ + remoteAudioBytes_, data, bytes);
  remoteAudioBytes_ += bytes;
  return true;
}

bool AudioManager::remoteAudioComplete() const {
  return remoteAudioExpectedBytes_ > 0 && remoteAudioBytes_ == remoteAudioExpectedBytes_;
}

void AudioManager::playRemoteAudio() {
  if (remoteAudioComplete()) {
    playPcm(buffer_, remoteAudioBytes_);
  }
  remoteAudioBytes_ = 0;
  remoteAudioExpectedBytes_ = 0;
}

void AudioManager::playRecording() {
  if (!ready_ || !hasRecording()) {
    return;
  }
  setAmplifierEnabled(true);
  stopCodecPath();
  startCodecPath(false, true);
  for (size_t offset = 0; offset < recordedBytes_; offset += kChunkBytes) {
    writeChunk(buffer_ + offset, min(kChunkBytes, recordedBytes_ - offset));
  }
  delay(60);
  stopCodecPath();
  setAmplifierEnabled(false);
}

void AudioManager::playPcm(const uint8_t* data, size_t bytes) {
  if (!ready_ || data == nullptr || bytes == 0) {
    return;
  }
  setAmplifierEnabled(true);
  stopCodecPath();
  startCodecPath(false, true);
  for (size_t offset = 0; offset < bytes; offset += kChunkBytes) {
    writeChunk(data + offset, min(kChunkBytes, bytes - offset));
  }
  delay(60);
  stopCodecPath();
  setAmplifierEnabled(false);
}

void AudioManager::playTone(uint16_t frequencyHz, uint16_t durationMs) {
  if (!ready_) {
    return;
  }
  setAmplifierEnabled(true);
  stopCodecPath();
  startCodecPath(false, true);
  synthTone(frequencyHz, durationMs);
  delay(30);
  stopCodecPath();
  setAmplifierEnabled(false);
}

void AudioManager::playStartupChime() {
  if (!ready_) {
    return;
  }
  setAmplifierEnabled(true);
  stopCodecPath();
  startCodecPath(false, true);
  synthTone(659, 110);
  delay(30);
  synthTone(784, 110);
  delay(30);
  synthTone(1047, 220);
  delay(40);
  stopCodecPath();
  setAmplifierEnabled(false);
}

void AudioManager::shutdown() {
  recording_ = false;
  remoteAudioBytes_ = 0;
  remoteAudioExpectedBytes_ = 0;
  stopCodecPath();
  setAmplifierEnabled(false);
  if (buffer_ != nullptr) {
    heap_caps_free(buffer_);
    buffer_ = nullptr;
  }
  if (txChannel_ != nullptr) {
    i2s_channel_disable(txChannel_);
    i2s_del_channel(txChannel_);
    txChannel_ = nullptr;
  }
  if (rxChannel_ != nullptr) {
    i2s_channel_disable(rxChannel_);
    i2s_del_channel(rxChannel_);
    rxChannel_ = nullptr;
  }
  ready_ = false;
}

bool AudioManager::initCodec() {
  delay(20);
  return writeCodecRegister(kRegGpio44, 0x08) &&
         writeCodecRegister(kRegGpio44, 0x08) &&
         writeCodecRegister(kRegClock1, 0x30) &&
         writeCodecRegister(kRegClock2, 0x00) &&
         writeCodecRegister(kRegClock3, 0x10) &&
         writeCodecRegister(kRegAdc16, 0x24) &&
         writeCodecRegister(kRegClock4, 0x20) &&
         writeCodecRegister(kRegClock5, 0x00) &&
         writeCodecRegister(kRegSystem0B, 0x00) &&
         writeCodecRegister(kRegSystem0C, 0x00) &&
         writeCodecRegister(kRegSystem10, 0x1F) &&
         writeCodecRegister(kRegSystem11, 0x7F) &&
         writeCodecRegister(kRegReset, 0x80) &&
         writeCodecRegister(kRegClock1, 0x3F) &&
         writeCodecRegister(kRegClock6, 0x03) &&
         writeCodecRegister(kRegClock7, 0x00) &&
         writeCodecRegister(kRegClock8, 0xFF) &&
         writeCodecRegister(kRegSystem13, 0x10) &&
         writeCodecRegister(kRegAdc1B, 0x0A) &&
         writeCodecRegister(kRegAdc1C, 0x6A) &&
         writeCodecRegister(kRegSdpIn, 0x0C) &&
         writeCodecRegister(kRegSdpOut, 0x0C) &&
         writeCodecRegister(kRegDac32, 0x00) &&
         writeCodecRegister(kRegAdc17, 0xBF) &&
         writeCodecRegister(kRegAdc16, 0x06);
}

bool AudioManager::initI2S() {
  i2s_chan_config_t channelConfig = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  channelConfig.dma_desc_num = 8;
  channelConfig.dma_frame_num = 256;
  channelConfig.auto_clear = true;
  if (i2s_new_channel(&channelConfig, &txChannel_, &rxChannel_) != ESP_OK) {
    return false;
  }
  i2s_std_config_t config = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kSampleRate),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
      .gpio_cfg = {
          .mclk = static_cast<gpio_num_t>(board::PIN_I2S_MCLK),
          .bclk = static_cast<gpio_num_t>(board::PIN_I2S_BCLK),
          .ws = static_cast<gpio_num_t>(board::PIN_I2S_LRCK),
          .dout = static_cast<gpio_num_t>(board::PIN_I2S_DOUT),
          .din = static_cast<gpio_num_t>(board::PIN_I2S_DIN),
      },
  };
  config.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  if (i2s_channel_init_std_mode(txChannel_, &config) != ESP_OK ||
      i2s_channel_init_std_mode(rxChannel_, &config) != ESP_OK ||
      i2s_channel_enable(txChannel_) != ESP_OK ||
      i2s_channel_enable(rxChannel_) != ESP_OK) {
    return false;
  }
  return true;
}

bool AudioManager::writeCodecRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(board::I2C_ADDR_ES8311);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

uint8_t AudioManager::readCodecRegister(uint8_t reg) {
  Wire.beginTransmission(board::I2C_ADDR_ES8311);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(static_cast<int>(board::I2C_ADDR_ES8311), 1) != 1) {
    return 0;
  }
  return Wire.read();
}

void AudioManager::startCodecPath(bool enableAdc, bool enableDac) {
  uint8_t dacInterface = readCodecRegister(kRegSdpIn) & 0xBF;
  uint8_t adcInterface = readCodecRegister(kRegSdpOut) & 0xBF;
  dacInterface |= BIT(6);
  adcInterface |= BIT(6);
  if (enableDac) dacInterface &= ~BIT(6);
  if (enableAdc) adcInterface &= ~BIT(6);
  writeCodecRegister(kRegSdpIn, dacInterface);
  writeCodecRegister(kRegSdpOut, adcInterface);
  writeCodecRegister(kRegAdc17, 0xBF);
  writeCodecRegister(kRegSystem0E, 0x02);
  writeCodecRegister(kRegSystem12, 0x00);
  writeCodecRegister(kRegSystem14, 0x1A);
  writeCodecRegister(kRegSystem0D, 0x01);
  writeCodecRegister(kRegAdc15, 0x40);
  writeCodecRegister(kRegDac37, 0x08);
  writeCodecRegister(kRegGp45, 0x00);
  writeCodecRegister(kRegGpio44, 0x58);
  writeCodecRegister(kRegDac32, enableDac ? kDacVolume80Percent : 0x00);
  const uint8_t mute = readCodecRegister(kRegDac31) & 0x9F;
  writeCodecRegister(kRegDac31, enableDac ? mute : static_cast<uint8_t>(mute | 0x60));
}

void AudioManager::stopCodecPath() {
  writeCodecRegister(kRegDac32, 0x00);
  writeCodecRegister(kRegAdc17, 0x00);
  writeCodecRegister(kRegSystem0E, 0xFF);
  writeCodecRegister(kRegSystem12, 0x02);
  writeCodecRegister(kRegSystem14, 0x00);
  writeCodecRegister(kRegSystem0D, 0xFA);
  writeCodecRegister(kRegAdc15, 0x00);
  writeCodecRegister(kRegGp45, 0x01);
}

void AudioManager::setAmplifierEnabled(bool enabled) {
  if (enabled) {
    digitalWrite(board::PIN_AUDIO_PWR, LOW);
    delay(5);
    digitalWrite(board::PIN_AUDIO_CTRL, HIGH);
    delay(kAmplifierStartupMs);
    return;
  }
  digitalWrite(board::PIN_AUDIO_CTRL, LOW);
  delay(5);
  digitalWrite(board::PIN_AUDIO_PWR, HIGH);
}

void AudioManager::writeChunk(const uint8_t* data, size_t bytes) {
  size_t written = 0;
  i2s_channel_write(txChannel_, data, bytes, &written, portMAX_DELAY);
}

void AudioManager::synthTone(uint16_t frequencyHz, uint16_t durationMs) {
  int16_t samples[(kChunkBytes / kBytesPerFrame) * kChannels];
  const size_t totalFrames = (static_cast<size_t>(kSampleRate) * durationMs) / 1000;
  const float step = 2.0f * PI * frequencyHz / kSampleRate;
  float phase = 0;
  for (size_t emitted = 0; emitted < totalFrames;) {
    const size_t frames = min(kChunkBytes / kBytesPerFrame, totalFrames - emitted);
    for (size_t index = 0; index < frames; ++index) {
      const int16_t sample = static_cast<int16_t>(sinf(phase) * 9000.0f);
      samples[index * 2] = sample;
      samples[index * 2 + 1] = sample;
      phase += step;
      if (phase > 2.0f * PI) phase -= 2.0f * PI;
    }
    writeChunk(reinterpret_cast<const uint8_t*>(samples), frames * kBytesPerFrame);
    emitted += frames;
  }
}
