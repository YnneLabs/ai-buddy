#pragma once

#include <Arduino.h>

struct ClimateReading {
  float temperatureC = 0.0f;
  float humidityPct = 0.0f;
  bool valid = false;
};

class SensorManager {
 public:
  bool begin();
  void updateIfNeeded();
  ClimateReading latest() const;

 private:
  bool readMeasurement(ClimateReading& outReading);
  bool writeCommand(uint16_t command);
  static uint8_t crc8(const uint8_t* data, size_t length);

  ClimateReading latest_;
  uint32_t lastReadMs_ = 0;
};
