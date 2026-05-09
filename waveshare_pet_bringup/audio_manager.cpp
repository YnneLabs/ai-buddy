#include "audio_manager.h"

#include <esp_heap_caps.h>
#include <math.h>
#include <Wire.h>

#include "board_pins.h"

namespace {

constexpr uint8_t REG_RESET = 0x00;
constexpr uint8_t REG_CLK_01 = 0x01;
constexpr uint8_t REG_CLK_02 = 0x02;
constexpr uint8_t REG_CLK_03 = 0x03;
constexpr uint8_t REG_CLK_04 = 0x04;
constexpr uint8_t REG_CLK_05 = 0x05;
constexpr uint8_t REG_CLK_06 = 0x06;
constexpr uint8_t REG_CLK_07 = 0x07;
constexpr uint8_t REG_CLK_08 = 0x08;
constexpr uint8_t REG_SDPIN = 0x09;
constexpr uint8_t REG_SDPOUT = 0x0A;
constexpr uint8_t REG_SYSTEM_0B = 0x0B;
constexpr uint8_t REG_SYSTEM_0C = 0x0C;
constexpr uint8_t REG_SYSTEM_0D = 0x0D;
constexpr uint8_t REG_SYSTEM_0E = 0x0E;
constexpr uint8_t REG_SYSTEM_10 = 0x10;
constexpr uint8_t REG_SYSTEM_11 = 0x11;
constexpr uint8_t REG_SYSTEM_12 = 0x12;
constexpr uint8_t REG_SYSTEM_13 = 0x13;
constexpr uint8_t REG_SYSTEM_14 = 0x14;
constexpr uint8_t REG_ADC_15 = 0x15;
constexpr uint8_t REG_ADC_16 = 0x16;
constexpr uint8_t REG_ADC_17 = 0x17;
constexpr uint8_t REG_ADC_1B = 0x1B;
constexpr uint8_t REG_ADC_1C = 0x1C;
constexpr uint8_t REG_DAC_31 = 0x31;
constexpr uint8_t REG_DAC_32 = 0x32;
constexpr uint8_t REG_DAC_37 = 0x37;
constexpr uint8_t REG_GPIO_44 = 0x44;
constexpr uint8_t REG_GP_45 = 0x45;

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

  return initCodec() && initI2S();
}

void AudioManager::setAmplifierEnabled(bool enabled) {
  digitalWrite(board::PIN_AUDIO_PWR, enabled ? LOW : HIGH);
  digitalWrite(board::PIN_AUDIO_CTRL, enabled ? HIGH : LOW);
}

bool AudioManager::startRecording() {
  if (buffer_ == nullptr) {
    return false;
  }
  recordedBytes_ = 0;
  isRecording_ = true;
  setAmplifierEnabled(false);
  stopCodecPath();
  startCodecPath(true, false);
  i2s_zero_dma_buffer(kI2SPort);
  return true;
}

void AudioManager::captureStep() {
  if (!isRecording_ || recordedBytes_ >= kMaxBytes) {
    return;
  }

  const size_t remainingBytes = kMaxBytes - recordedBytes_;
  const size_t bytesToRead = remainingBytes > kChunkBytes ? kChunkBytes : remainingBytes;
  size_t bytesRead = 0;
  i2s_read(kI2SPort, buffer_ + recordedBytes_, bytesToRead, &bytesRead, pdMS_TO_TICKS(20));
  recordedBytes_ += bytesRead;
}

bool AudioManager::finishRecording() {
  if (!isRecording_) {
    return false;
  }
  isRecording_ = false;
  stopCodecPath();
  return true;
}

bool AudioManager::hasRecording() const {
  return recordedBytes_ > 0;
}

size_t AudioManager::recordedBytes() const {
  return recordedBytes_;
}

void AudioManager::shutdown() {
  isRecording_ = false;
  stopCodecPath();
  setAmplifierEnabled(false);
}

void AudioManager::playBeep(uint16_t frequencyHz, uint16_t durationMs) {
  setAmplifierEnabled(true);
  stopCodecPath();
  startCodecPath(false, true);
  synthTone(frequencyHz, durationMs);
  delay(30);
  stopCodecPath();
  setAmplifierEnabled(false);
}

void AudioManager::playRecording() {
  if (!hasRecording()) {
    return;
  }

  setAmplifierEnabled(true);
  stopCodecPath();
  startCodecPath(false, true);
  i2s_zero_dma_buffer(kI2SPort);

  size_t offset = 0;
  while (offset < recordedBytes_) {
    const size_t remaining = recordedBytes_ - offset;
    const size_t chunk = remaining > kChunkBytes ? kChunkBytes : remaining;
    writeChunk(buffer_ + offset, chunk);
    offset += chunk;
  }

  delay(60);
  stopCodecPath();
  setAmplifierEnabled(false);
}

void AudioManager::playEmotionTone(PetEmotion emotion) {
  setAmplifierEnabled(true);
  stopCodecPath();
  startCodecPath(false, true);

  switch (emotion) {
    case PetEmotion::Neutral:
      synthTone(660, 100);
      break;
    case PetEmotion::Happy:
      synthTone(740, 110);
      synthTone(988, 110);
      break;
    case PetEmotion::Sleepy:
      synthTone(392, 180);
      synthTone(330, 200);
      break;
    case PetEmotion::Surprised:
      synthTone(1175, 90);
      synthTone(1568, 120);
      break;
    case PetEmotion::Sad:
      synthTone(440, 160);
      synthTone(349, 220);
      break;
    default:
      break;
  }

  delay(40);
  stopCodecPath();
  setAmplifierEnabled(false);
}

bool AudioManager::initCodec() {
  delay(20);
  return writeCodecRegister(REG_GPIO_44, 0x08) &&
         writeCodecRegister(REG_GPIO_44, 0x08) &&
         writeCodecRegister(REG_CLK_01, 0x30) &&
         writeCodecRegister(REG_CLK_02, 0x00) &&
         writeCodecRegister(REG_CLK_03, 0x10) &&
         writeCodecRegister(REG_ADC_16, 0x24) &&
         writeCodecRegister(REG_CLK_04, 0x10) &&
         writeCodecRegister(REG_CLK_05, 0x00) &&
         writeCodecRegister(REG_SYSTEM_0B, 0x00) &&
         writeCodecRegister(REG_SYSTEM_0C, 0x00) &&
         writeCodecRegister(REG_SYSTEM_10, 0x1F) &&
         writeCodecRegister(REG_SYSTEM_11, 0x7F) &&
         writeCodecRegister(REG_RESET, 0x80) &&
         writeCodecRegister(REG_CLK_01, 0x3F) &&
         writeCodecRegister(REG_CLK_06, 0x03) &&
         writeCodecRegister(REG_SYSTEM_13, 0x10) &&
         writeCodecRegister(REG_ADC_1B, 0x0A) &&
         writeCodecRegister(REG_ADC_1C, 0x6A) &&
         writeCodecRegister(REG_SDPIN, 0x0C) &&
         writeCodecRegister(REG_SDPOUT, 0x0C) &&
         writeCodecRegister(REG_DAC_32, 0x00) &&
         writeCodecRegister(REG_ADC_17, 0xBF) &&
         writeCodecRegister(REG_ADC_16, 0x06);
}

bool AudioManager::initI2S() {
  i2s_driver_uninstall(kI2SPort);

  i2s_config_t config = {};
  config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX);
  config.sample_rate = kSampleRate;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  config.dma_buf_count = 8;
  config.dma_buf_len = 256;
  config.use_apll = false;
  config.tx_desc_auto_clear = true;
  config.fixed_mclk = static_cast<int>(kSampleRate * 256);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
  config.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  config.bits_per_chan = I2S_BITS_PER_CHAN_16BIT;
#endif

  if (i2s_driver_install(kI2SPort, &config, 0, nullptr) != ESP_OK) {
    return false;
  }

  i2s_pin_config_t pins = {};
  pins.mck_io_num = board::PIN_I2S_MCLK;
  pins.bck_io_num = board::PIN_I2S_BCLK;
  pins.ws_io_num = board::PIN_I2S_LRCK;
  pins.data_out_num = board::PIN_I2S_DOUT;
  pins.data_in_num = board::PIN_I2S_DIN;

  if (i2s_set_pin(kI2SPort, &pins) != ESP_OK) {
    return false;
  }

  i2s_zero_dma_buffer(kI2SPort);
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
  if (Wire.endTransmission(false) != 0) {
    return 0;
  }
  if (Wire.requestFrom(static_cast<int>(board::I2C_ADDR_ES8311), 1) != 1) {
    return 0;
  }
  return Wire.read();
}

void AudioManager::writeChunk(const uint8_t* data, size_t bytes) {
  size_t bytesWritten = 0;
  i2s_write(kI2SPort, data, bytes, &bytesWritten, portMAX_DELAY);
}

void AudioManager::startCodecPath(bool enableAdc, bool enableDac) {
  uint8_t dacIface = readCodecRegister(REG_SDPIN) & 0xBF;
  uint8_t adcIface = readCodecRegister(REG_SDPOUT) & 0xBF;
  dacIface |= BIT(6);
  adcIface |= BIT(6);
  if (enableDac) {
    dacIface &= ~BIT(6);
  }
  if (enableAdc) {
    adcIface &= ~BIT(6);
  }

  writeCodecRegister(REG_SDPIN, dacIface);
  writeCodecRegister(REG_SDPOUT, adcIface);
  writeCodecRegister(REG_ADC_17, 0xBF);
  writeCodecRegister(REG_SYSTEM_0E, 0x02);
  writeCodecRegister(REG_SYSTEM_12, 0x00);
  writeCodecRegister(REG_SYSTEM_14, 0x1A);
  writeCodecRegister(REG_SYSTEM_0D, 0x01);
  writeCodecRegister(REG_ADC_15, 0x40);
  writeCodecRegister(REG_DAC_37, 0x08);
  writeCodecRegister(REG_GP_45, 0x00);
  writeCodecRegister(REG_GPIO_44, 0x58);

  uint8_t mute = readCodecRegister(REG_DAC_31) & 0x9F;
  writeCodecRegister(REG_DAC_31, enableDac ? mute : static_cast<uint8_t>(mute | 0x60));
}

void AudioManager::stopCodecPath() {
  writeCodecRegister(REG_DAC_32, 0x00);
  writeCodecRegister(REG_ADC_17, 0x00);
  writeCodecRegister(REG_SYSTEM_0E, 0xFF);
  writeCodecRegister(REG_SYSTEM_12, 0x02);
  writeCodecRegister(REG_SYSTEM_14, 0x00);
  writeCodecRegister(REG_SYSTEM_0D, 0xFA);
  writeCodecRegister(REG_ADC_15, 0x00);
  writeCodecRegister(REG_GP_45, 0x01);
}

void AudioManager::synthTone(uint16_t frequencyHz, uint16_t durationMs) {
  int16_t chunk[(kChunkBytes / kBytesPerFrame) * 2];
  const size_t totalFrames = (static_cast<size_t>(kSampleRate) * durationMs) / 1000;
  float phase = 0.0f;
  const float phaseStep = 2.0f * PI * static_cast<float>(frequencyHz) / static_cast<float>(kSampleRate);
  size_t emittedFrames = 0;

  while (emittedFrames < totalFrames) {
    const size_t frameCount = min(kChunkBytes / kBytesPerFrame, totalFrames - emittedFrames);
    for (size_t i = 0; i < frameCount; ++i) {
      const int16_t sample = static_cast<int16_t>(sinf(phase) * 9000.0f);
      chunk[i * 2] = sample;
      chunk[i * 2 + 1] = sample;
      phase += phaseStep;
      if (phase > (2.0f * PI)) {
        phase -= (2.0f * PI);
      }
    }
    writeChunk(reinterpret_cast<const uint8_t*>(chunk), frameCount * kBytesPerFrame);
    emittedFrames += frameCount;
  }
}
