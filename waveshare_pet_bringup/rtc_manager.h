#pragma once

#include <Arduino.h>

class RTCManager {
 public:
  bool begin();
  time_t now() const;
  void syncToRtcIfNeeded();
  String formatHourMinute() const;

 private:
  bool readEpoch(time_t& outEpoch);
  bool writeEpoch(time_t epoch);
  bool isValid(tm& timeInfo) const;
  time_t compileTimeEpoch() const;

  time_t baseEpoch_ = 0;
  uint32_t baseMillis_ = 0;
  uint32_t lastRtcSyncMs_ = 0;
};
