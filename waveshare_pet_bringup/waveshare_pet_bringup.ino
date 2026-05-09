#include <Arduino.h>
#include <Wire.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include "audio_manager.h"
#include "board_pins.h"
#include "button_manager.h"
#include "display_manager.h"
#include "pet_face.h"
#include "rtc_manager.h"
#include "sensor_manager.h"

enum class AppState : uint8_t {
  Boot = 0,
  IdleHome,
  Recording,
  Playback,
  Sleeping
};

namespace {

enum class WakeGuardState : uint8_t {
  None = 0,
  Active,
  Released
};

DisplayManager gDisplay;
RTCManager gRtc;
SensorManager gSensor;
AudioManager gAudio;
DebouncedButton gTopButton(board::PIN_BTN_TOP);
DebouncedButton gBottomButton(board::PIN_BTN_BOTTOM);

AppState gState = AppState::Boot;
PetEmotion gEmotion = PetEmotion::Neutral;
String gLastRenderedMinute;
String gLastRenderedClimate;
bool gForceFullRefresh = true;
PetEmotion gLastRenderedEmotion = PetEmotion::Count;
OverlayState gLastRenderedOverlay = OverlayState::Playing;
bool gSleepPending = false;
uint32_t gSleepReadyAtMs = 0;
WakeGuardState gWakeGuardState = WakeGuardState::None;
uint32_t gBootPressStartedAtMs = 0;
bool gBootHoldArmed = false;

const char* appStateName(AppState state) {
  switch (state) {
    case AppState::Boot:
      return "Boot";
    case AppState::IdleHome:
      return "IdleHome";
    case AppState::Recording:
      return "Recording";
    case AppState::Playback:
      return "Playback";
    case AppState::Sleeping:
      return "Sleeping";
    default:
      return "Unknown";
  }
}

const char* wakeCauseName(esp_sleep_wakeup_cause_t cause) {
  switch (cause) {
    case ESP_SLEEP_WAKEUP_EXT0:
      return "EXT0";
    case ESP_SLEEP_WAKEUP_EXT1:
      return "EXT1";
    case ESP_SLEEP_WAKEUP_TIMER:
      return "TIMER";
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
      return "TOUCH";
    case ESP_SLEEP_WAKEUP_ULP:
      return "ULP";
    case ESP_SLEEP_WAKEUP_GPIO:
      return "GPIO";
    case ESP_SLEEP_WAKEUP_UART:
      return "UART";
    case ESP_SLEEP_WAKEUP_UNDEFINED:
      return "UNDEFINED";
    default:
      return "OTHER";
  }
}

void logButtonEvent(const char* name, uint8_t pin, const ButtonSnapshot& snapshot) {
  if (!(snapshot.pressed || snapshot.released || snapshot.shortPress || snapshot.longPressStart)) {
    return;
  }
  Serial.printf("BTN pin=%u name=%s pressed=%d released=%d short=%d long=%d down=%d\n",
                pin,
                name,
                snapshot.pressed,
                snapshot.released,
                snapshot.shortPress,
                snapshot.longPressStart,
                snapshot.isDown);
}

OverlayState overlayForState(AppState state) {
  switch (state) {
    case AppState::Recording:
      return OverlayState::Recording;
    case AppState::Playback:
      return OverlayState::Playing;
    default:
      return OverlayState::None;
  }
}

void refreshDisplayIfNeeded(bool forceFull = false) {
  const String minuteText = gRtc.formatHourMinute();
  const ClimateReading reading = gSensor.latest();
  String climateText = "sensor --.-C --.-%";
  if (reading.valid) {
    char buffer[24];
    snprintf(buffer, sizeof(buffer), "%.1fC %.0f%%RH", reading.temperatureC, reading.humidityPct);
    climateText = String(buffer);
  }
  const OverlayState overlay = overlayForState(gState);
  if (!forceFull &&
      minuteText == gLastRenderedMinute &&
      climateText == gLastRenderedClimate &&
      gEmotion == gLastRenderedEmotion &&
      overlay == gLastRenderedOverlay &&
      gState == AppState::IdleHome) {
    return;
  }
  gLastRenderedMinute = minuteText;
  gLastRenderedClimate = climateText;
  gLastRenderedEmotion = gEmotion;
  gLastRenderedOverlay = overlay;
  gDisplay.renderHome(minuteText, climateText, gEmotion, overlay, forceFull || gForceFullRefresh);
  gForceFullRefresh = false;
}

void setState(AppState state, bool forceFullDisplay = false) {
  if (gState != state) {
    Serial.printf("STATE %s -> %s\n", appStateName(gState), appStateName(state));
  }
  gState = state;
  refreshDisplayIfNeeded(forceFullDisplay);
}

void enterSleep() {
  if (gState == AppState::Sleeping) {
    return;
  }

  gState = AppState::Sleeping;
  Serial.println("PWR sleep_enter");
  gAudio.shutdown();
  gDisplay.renderSleep(gRtc.formatHourMinute(), gEmotion);
  gDisplay.sleep();
  digitalWrite(board::PIN_STATUS_LED, LOW);
  digitalWrite(board::PIN_VBAT_HOLD, HIGH);

  rtc_gpio_pullup_en(static_cast<gpio_num_t>(board::PIN_BTN_BOTTOM));
  rtc_gpio_pulldown_dis(static_cast<gpio_num_t>(board::PIN_BTN_BOTTOM));
  esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(board::PIN_BTN_BOTTOM), 0);
  delay(50);
  esp_deep_sleep_start();
}

void armSleep() {
  if (gState != AppState::IdleHome || gSleepPending) {
    return;
  }
  gSleepPending = true;
  gSleepReadyAtMs = millis() + 180;
  Serial.println("PWR sleep_armed");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("Waveshare pet bring-up starting");

  pinMode(board::PIN_STATUS_LED, OUTPUT);
  digitalWrite(board::PIN_STATUS_LED, HIGH);
  pinMode(board::PIN_VBAT_HOLD, OUTPUT);
  digitalWrite(board::PIN_VBAT_HOLD, HIGH);

  Wire.begin(board::PIN_I2C_SDA, board::PIN_I2C_SCL);
  Wire.setClock(100000);

  gTopButton.begin();
  gBottomButton.begin();

  const bool displayReady = gDisplay.begin();
  const bool rtcReady = gRtc.begin();
  const bool sensorReady = gSensor.begin();
  const bool audioReady = gAudio.begin();
  const esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();

  gWakeGuardState = (wakeCause == ESP_SLEEP_WAKEUP_EXT0) ? WakeGuardState::Active : WakeGuardState::None;

  Serial.printf("display=%s rtc=%s sensor=%s audio=%s\n",
                displayReady ? "ok" : "fail",
                rtcReady ? "ok" : "fail",
                sensorReady ? "ok" : "fail",
                audioReady ? "ok" : "fail");
  Serial.printf("WAKE cause=%s (%d)\n", wakeCauseName(wakeCause), static_cast<int>(wakeCause));
  if (gWakeGuardState == WakeGuardState::Active) {
    Serial.println("PWR wake_guard_on");
  }

  gState = AppState::IdleHome;
  refreshDisplayIfNeeded(true);
}

void loop() {
  const ButtonSnapshot top = gTopButton.update();
  const ButtonSnapshot bottom = gBottomButton.update();
  logButtonEvent("BOOT", board::PIN_BTN_TOP, top);
  logButtonEvent("PWR", board::PIN_BTN_BOTTOM, bottom);

  gRtc.syncToRtcIfNeeded();
  gSensor.updateIfNeeded();

  if (gState == AppState::Recording) {
    gAudio.captureStep();
  }

  if (gWakeGuardState == WakeGuardState::Active) {
    if (bottom.released || !bottom.isDown) {
      gWakeGuardState = WakeGuardState::Released;
      Serial.println("PWR wake_guard_released");
    }
  } else if (gWakeGuardState == WakeGuardState::Released) {
    gWakeGuardState = WakeGuardState::None;
    Serial.println("PWR wake_guard_clear");
  }

  if (gSleepPending) {
    const bool pwrReleased = digitalRead(board::PIN_BTN_BOTTOM) == HIGH;
    if (pwrReleased && millis() >= gSleepReadyAtMs) {
      gSleepPending = false;
      Serial.println("entering sleep");
      enterSleep();
    }
  }

  if (top.pressed && gState == AppState::IdleHome) {
    gBootPressStartedAtMs = millis();
    gBootHoldArmed = true;
  }

  if (gBootHoldArmed &&
      gState == AppState::IdleHome &&
      top.longPressStart) {
    gBootHoldArmed = false;
    if (gAudio.startRecording()) {
      setState(AppState::Recording, true);
      Serial.println("AUDIO start_record");
    }
  }

  if (top.released && gState == AppState::Recording) {
    gBootHoldArmed = false;
    gAudio.finishRecording();
    Serial.printf("AUDIO stop_record bytes=%u\n", static_cast<unsigned>(gAudio.recordedBytes()));
    setState(AppState::Playback, true);
    if (gAudio.hasRecording()) {
      Serial.println("AUDIO playback_start source=fresh_record");
      gAudio.playRecording();
    }
    setState(AppState::IdleHome, true);
  }

  if (top.released && gState == AppState::IdleHome) {
    const uint32_t heldMs = millis() - gBootPressStartedAtMs;
    if (gBootHoldArmed && heldMs < 650) {
      gBootHoldArmed = false;
      if (gAudio.hasRecording()) {
        Serial.println("AUDIO replay_start");
        setState(AppState::Playback, true);
        gAudio.playRecording();
        setState(AppState::IdleHome, true);
      } else {
        Serial.println("AUDIO replay_skip no_buffer");
      }
    } else {
      gBootHoldArmed = false;
    }
  }

  if (gWakeGuardState == WakeGuardState::None && bottom.shortPress && gState == AppState::IdleHome) {
    Serial.println("PWR sleep_toggle");
    armSleep();
  }

  if (gWakeGuardState == WakeGuardState::None && bottom.longPressStart && gState == AppState::IdleHome) {
    Serial.println("PWR long_press_ignored");
  }

  if (gState == AppState::IdleHome) {
    refreshDisplayIfNeeded(false);
  }

  delay(10);
}
