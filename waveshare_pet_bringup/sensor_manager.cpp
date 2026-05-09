#include "sensor_manager.h"

#include <Wire.h>

namespace {

constexpr uint8_t kShtc3Address = 0x70;
constexpr uint16_t kCmdWake = 0x3517;
constexpr uint16_t kCmdSleep = 0xB098;
constexpr uint16_t kCmdMeasureTFirst = 0x7CA2;
constexpr uint32_t kRefreshIntervalMs = 30000;

}  // namespace

bool SensorManager::begin() {
  delay(1);
  ClimateReading reading;
  latest_.valid = readMeasurement(reading);
  if (latest_.valid) {
    latest_ = reading;
    lastReadMs_ = millis();
  }
  return latest_.valid;
}

void SensorManager::updateIfNeeded() {
  const uint32_t nowMs = millis();
  if ((nowMs - lastReadMs_) < kRefreshIntervalMs) {
    return;
  }

  ClimateReading reading;
  if (readMeasurement(reading)) {
    latest_ = reading;
  }
  lastReadMs_ = nowMs;
}

ClimateReading SensorManager::latest() const {
  return latest_;
}

bool SensorManager::readMeasurement(ClimateReading& outReading) {
  if (!writeCommand(kCmdWake)) {
    return false;
  }
  delay(1);

  if (!writeCommand(kCmdMeasureTFirst)) {
    writeCommand(kCmdSleep);
    return false;
  }
  delay(20);

  constexpr uint8_t kBytes = 6;
  if (Wire.requestFrom(static_cast<int>(kShtc3Address), static_cast<int>(kBytes)) != kBytes) {
    writeCommand(kCmdSleep);
    return false;
  }

  uint8_t raw[kBytes];
  for (uint8_t i = 0; i < kBytes; ++i) {
    raw[i] = Wire.read();
  }

  writeCommand(kCmdSleep);

  if (crc8(raw, 2) != raw[2] || crc8(raw + 3, 2) != raw[5]) {
    return false;
  }

  const uint16_t rawTemp = static_cast<uint16_t>(raw[0] << 8) | raw[1];
  const uint16_t rawHumidity = static_cast<uint16_t>(raw[3] << 8) | raw[4];

  outReading.temperatureC = -45.0f + 175.0f * (static_cast<float>(rawTemp) / 65535.0f);
  outReading.humidityPct = 100.0f * (static_cast<float>(rawHumidity) / 65535.0f);
  outReading.valid = true;
  return true;
}

bool SensorManager::writeCommand(uint16_t command) {
  Wire.beginTransmission(kShtc3Address);
  Wire.write(static_cast<uint8_t>(command >> 8));
  Wire.write(static_cast<uint8_t>(command & 0xFF));
  return Wire.endTransmission() == 0;
}

uint8_t SensorManager::crc8(const uint8_t* data, size_t length) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x31) : static_cast<uint8_t>(crc << 1);
    }
  }
  return crc;
}
