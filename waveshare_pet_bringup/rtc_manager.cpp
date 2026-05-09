#include "rtc_manager.h"

#include <Wire.h>

#include "board_pins.h"

namespace {

uint8_t decToBcd(uint8_t value) {
  return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
}

uint8_t bcdToDec(uint8_t value) {
  return static_cast<uint8_t>(((value >> 4) * 10) + (value & 0x0F));
}

uint8_t monthFromString(const char* month) {
  static const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  for (uint8_t i = 0; i < 12; ++i) {
    if (strncmp(month, kMonths[i], 3) == 0) {
      return i + 1;
    }
  }
  return 1;
}

}  // namespace

bool RTCManager::begin() {
  time_t rtcEpoch = 0;
  if (readEpoch(rtcEpoch)) {
    baseEpoch_ = rtcEpoch;
  } else {
    baseEpoch_ = compileTimeEpoch();
    writeEpoch(baseEpoch_);
  }
  baseMillis_ = millis();
  lastRtcSyncMs_ = millis();
  return true;
}

time_t RTCManager::now() const {
  return baseEpoch_ + static_cast<time_t>((millis() - baseMillis_) / 1000);
}

void RTCManager::syncToRtcIfNeeded() {
  const uint32_t nowMs = millis();
  if (nowMs - lastRtcSyncMs_ < 60000) {
    return;
  }
  lastRtcSyncMs_ = nowMs;
  const time_t current = now();
  writeEpoch(current);
  baseEpoch_ = current;
  baseMillis_ = nowMs;
}

String RTCManager::formatHourMinute() const {
  time_t epoch = now();
  struct tm timeInfo;
  localtime_r(&epoch, &timeInfo);
  char buffer[6];
  snprintf(buffer, sizeof(buffer), "%02d:%02d", timeInfo.tm_hour, timeInfo.tm_min);
  return String(buffer);
}

bool RTCManager::readEpoch(time_t& outEpoch) {
  Wire.beginTransmission(board::I2C_ADDR_RTC);
  Wire.write(0x04);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  constexpr uint8_t kBytes = 7;
  if (Wire.requestFrom(static_cast<int>(board::I2C_ADDR_RTC), static_cast<int>(kBytes)) != kBytes) {
    return false;
  }

  tm timeInfo = {};
  timeInfo.tm_sec = bcdToDec(Wire.read() & 0x7F);
  timeInfo.tm_min = bcdToDec(Wire.read() & 0x7F);
  timeInfo.tm_hour = bcdToDec(Wire.read() & 0x3F);
  timeInfo.tm_mday = bcdToDec(Wire.read() & 0x3F);
  Wire.read();  // weekday
  timeInfo.tm_mon = bcdToDec(Wire.read() & 0x1F) - 1;
  timeInfo.tm_year = bcdToDec(Wire.read()) + 100;  // 2000-based

  if (!isValid(timeInfo)) {
    return false;
  }

  timeInfo.tm_isdst = -1;
  outEpoch = mktime(&timeInfo);
  return outEpoch > 0;
}

bool RTCManager::writeEpoch(time_t epoch) {
  struct tm timeInfo;
  localtime_r(&epoch, &timeInfo);

  Wire.beginTransmission(board::I2C_ADDR_RTC);
  Wire.write(0x04);
  Wire.write(decToBcd(timeInfo.tm_sec));
  Wire.write(decToBcd(timeInfo.tm_min));
  Wire.write(decToBcd(timeInfo.tm_hour));
  Wire.write(decToBcd(timeInfo.tm_mday));
  Wire.write(decToBcd(timeInfo.tm_wday));
  Wire.write(decToBcd(timeInfo.tm_mon + 1));
  Wire.write(decToBcd((timeInfo.tm_year + 1900) - 2000));
  return Wire.endTransmission() == 0;
}

bool RTCManager::isValid(tm& timeInfo) const {
  const int year = timeInfo.tm_year + 1900;
  if (year < 2024 || year > 2099) {
    return false;
  }
  if (timeInfo.tm_mon < 0 || timeInfo.tm_mon > 11) {
    return false;
  }
  if (timeInfo.tm_mday < 1 || timeInfo.tm_mday > 31) {
    return false;
  }
  if (timeInfo.tm_hour > 23 || timeInfo.tm_min > 59 || timeInfo.tm_sec > 59) {
    return false;
  }
  return true;
}

time_t RTCManager::compileTimeEpoch() const {
  tm timeInfo = {};
  timeInfo.tm_mon = monthFromString(__DATE__) - 1;
  timeInfo.tm_mday = atoi(__DATE__ + 4);
  timeInfo.tm_year = atoi(__DATE__ + 7) - 1900;
  timeInfo.tm_hour = atoi(__TIME__);
  timeInfo.tm_min = atoi(__TIME__ + 3);
  timeInfo.tm_sec = atoi(__TIME__ + 6);
  timeInfo.tm_isdst = -1;
  return mktime(&timeInfo);
}
