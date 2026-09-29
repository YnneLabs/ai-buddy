#include <Arduino.h>
#include <ArduinoJson.h>
#include <GxEPD2_BW.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <SPI.h>
#include <Wire.h>
#include <WebServer.h>
#include <WebSocketsClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_wps.h>

#include "board_pins.h"
#include "audio_manager.h"

#if __has_include("local_config.h")
#include "local_config.h"
#endif

#ifndef AI_BUDDY_DEFAULT_WIFI_SSID
#define AI_BUDDY_DEFAULT_WIFI_SSID ""
#endif

#ifndef AI_BUDDY_DEFAULT_WIFI_PASSWORD
#define AI_BUDDY_DEFAULT_WIFI_PASSWORD ""
#endif

#ifndef AI_BUDDY_DEFAULT_BACKEND_URL
#define AI_BUDDY_DEFAULT_BACKEND_URL ""
#endif

#ifndef AI_BUDDY_DEFAULT_DEVICE_TOKEN
#define AI_BUDDY_DEFAULT_DEVICE_TOKEN ""
#endif

#ifndef AI_BUDDY_BACKEND_MIGRATE_FROM
#define AI_BUDDY_BACKEND_MIGRATE_FROM ""
#endif

#ifndef AI_BUDDY_BACKEND_MIGRATE_TO
#define AI_BUDDY_BACKEND_MIGRATE_TO ""
#endif

namespace {

constexpr char kProtocolVersion[] = "1";
constexpr char kPortalSsid[] = "AI-Buddy-Setup";
constexpr uint32_t kWifiConnectTimeoutMs = 15000;
constexpr uint32_t kReconnectIntervalMs = 5000;
constexpr uint32_t kResetHoldMs = 3000;
constexpr uint32_t kWpsHoldMs = 1500;
constexpr uint32_t kWpsTimeoutMs = 120000;

using DisplayType = GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT>;
DisplayType gDisplay(GxEPD2_154_D67(board::PIN_EPD_CS, board::PIN_EPD_DC, board::PIN_EPD_RST, board::PIN_EPD_BUSY));
Preferences gPreferences;
WebServer gPortal(80);
WebSocketsClient gSocket;
AudioManager gAudio;

enum class DeviceState : uint8_t {
  Boot,
  Provisioning,
  Wps,
  ConnectingWifi,
  FetchingConfig,
  ConnectingSession,
  Listening,
  Thinking,
  PlayingAudio,
  MemoryConfirmation,
  Online,
  Error,
};

struct DeviceConfig {
  String wifiSsid;
  String wifiPassword;
  String backendBaseUrl;
  String deviceToken;
  String deviceId;
};

DeviceConfig gConfig;
DeviceState gState = DeviceState::Boot;
String gStateDetail;
String gSessionUrl;
String gPendingMemoryId;
bool gSocketConnected = false;

void saveConfig();
bool gBootButtonDown = false;
uint32_t gBootButtonPressedAtMs = 0;
bool gPowerButtonDown = false;
uint32_t gPowerButtonPressedAtMs = 0;
uint32_t gLastReconnectAtMs = 0;
bool gWpsActive = false;
bool gWpsSucceeded = false;
uint32_t gWpsStartedAtMs = 0;
bool gAudioReady = false;

struct BatteryReading {
  bool valid;
  uint16_t millivolts;
  uint8_t percent;
};

void startConnection();

const char* stateName(DeviceState state) {
  switch (state) {
    case DeviceState::Boot:
      return "Iniciando";
    case DeviceState::Provisioning:
      return "Configurar Wi-Fi";
    case DeviceState::Wps:
      return "Router WPS";
    case DeviceState::ConnectingWifi:
      return "Conectando Wi-Fi";
    case DeviceState::FetchingConfig:
      return "Buscando config";
    case DeviceState::ConnectingSession:
      return "Conectando Buddy";
    case DeviceState::Listening:
      return "Escuchando";
    case DeviceState::Thinking:
      return "Pensando";
    case DeviceState::PlayingAudio:
      return "Hablando";
    case DeviceState::MemoryConfirmation:
      return "Guardar memoria?";
    case DeviceState::Online:
      return "Buddy listo";
    case DeviceState::Error:
      return "Error de conexion";
  }
  return "Unknown";
}

String escapedHtml(const String& value) {
  String escaped = value;
  escaped.replace("&", "&amp;");
  escaped.replace("\"", "&quot;");
  escaped.replace("<", "&lt;");
  escaped.replace(">", "&gt;");
  return escaped;
}

String deviceIdFromMac() {
  const uint64_t mac = ESP.getEfuseMac();
  char id[24];
  snprintf(id, sizeof(id), "buddy-%06llX", static_cast<unsigned long long>(mac & 0xFFFFFF));
  return String(id);
}

String setupPassword() {
  const uint64_t mac = ESP.getEfuseMac();
  char password[16];
  snprintf(password, sizeof(password), "buddy%06llX", static_cast<unsigned long long>(mac & 0xFFFFFF));
  return String(password);
}

BatteryReading readBattery() {
  constexpr uint8_t kSamples = 8;
  uint32_t totalMv = 0;
  uint8_t validSamples = 0;
  for (uint8_t sample = 0; sample < kSamples; ++sample) {
    const uint16_t adcMv = analogReadMilliVolts(board::PIN_VBAT_ADC);
    if (adcMv > 0) {
      totalMv += adcMv;
      ++validSamples;
    }
  }
  if (validSamples == 0) {
    return {false, 0, 0};
  }
  const uint16_t millivolts = static_cast<uint16_t>((totalMv / validSamples) * 2);
  const int percent = ((static_cast<int>(millivolts) - 3200) * 100 + 500) / 1000;
  return {true, millivolts, static_cast<uint8_t>(constrain(percent, 0, 100))};
}

String shortenedText(const String& value, size_t maxChars) {
  if (value.length() <= maxChars) {
    return value;
  }
  return value.substring(0, maxChars - 3) + "...";
}

void drawBattery(const BatteryReading& battery) {
  constexpr int kX = 148;
  constexpr int kY = 10;
  gDisplay.drawRoundRect(kX, kY, 25, 12, 2, GxEPD_BLACK);
  gDisplay.fillRect(kX + 25, kY + 4, 2, 4, GxEPD_BLACK);
  if (battery.valid) {
    const uint8_t segments = battery.percent == 0 ? 0 : (battery.percent + 24) / 25;
    for (uint8_t segment = 0; segment < segments; ++segment) {
      gDisplay.fillRect(kX + 3 + segment * 5, kY + 3, 3, 6, GxEPD_BLACK);
    }
  }
  gDisplay.setTextSize(1);
  gDisplay.setCursor(176, 20);
  if (battery.valid) {
    gDisplay.printf("%u%%", battery.percent);
  } else {
    gDisplay.print("--");
  }
}

void drawStatusBar(const BatteryReading& battery) {
  const bool connected = gState == DeviceState::Online || gState == DeviceState::Listening ||
                         gState == DeviceState::Thinking || gState == DeviceState::PlayingAudio ||
                         gState == DeviceState::MemoryConfirmation;
  gDisplay.fillCircle(12, 16, 3, connected ? GxEPD_BLACK : GxEPD_WHITE);
  gDisplay.drawCircle(12, 16, 3, GxEPD_BLACK);
  gDisplay.setTextSize(1);
  gDisplay.setCursor(20, 20);
  gDisplay.print(shortenedText(stateName(gState), 19));
  drawBattery(battery);
  gDisplay.drawLine(8, 31, 192, 31, GxEPD_BLACK);
}

void drawCloud() {
  // Build a single organic silhouette, then hollow it to leave a heavy e-paper outline.
  gDisplay.fillRoundRect(43, 86, 114, 50, 25, GxEPD_BLACK);
  gDisplay.fillCircle(68, 83, 30, GxEPD_BLACK);
  gDisplay.fillCircle(100, 70, 34, GxEPD_BLACK);
  gDisplay.fillCircle(133, 84, 29, GxEPD_BLACK);
  gDisplay.fillRoundRect(47, 90, 106, 41, 21, GxEPD_WHITE);
  gDisplay.fillCircle(69, 85, 25, GxEPD_WHITE);
  gDisplay.fillCircle(100, 73, 29, GxEPD_WHITE);
  gDisplay.fillCircle(132, 86, 24, GxEPD_WHITE);

  // Two fixed stippled shadows give depth without gray levels or animation.
  for (int y = 105; y <= 124; ++y) {
    for (int x = 109; x <= 143; ++x) {
      const int dx = x - 126;
      const int dy = y - 112;
      if (dx * dx + dy * dy < 280 && ((x + y) & 3) == 0) {
        gDisplay.drawPixel(x, y, GxEPD_BLACK);
      }
    }
  }
  for (int y = 112; y <= 128; ++y) {
    for (int x = 57; x <= 84; ++x) {
      const int dx = x - 71;
      const int dy = y - 119;
      if (dx * dx + dy * dy < 170 && ((x + y) % 5) == 0) {
        gDisplay.drawPixel(x, y, GxEPD_BLACK);
      }
    }
  }
}

void drawSmile(bool open = false) {
  if (open) {
    gDisplay.fillRoundRect(88, 108, 24, 11, 5, GxEPD_BLACK);
    gDisplay.fillRoundRect(92, 109, 16, 3, 1, GxEPD_WHITE);
    return;
  }
  gDisplay.drawLine(88, 109, 94, 114, GxEPD_BLACK);
  gDisplay.drawLine(94, 114, 100, 115, GxEPD_BLACK);
  gDisplay.drawLine(100, 115, 106, 114, GxEPD_BLACK);
  gDisplay.drawLine(106, 114, 112, 109, GxEPD_BLACK);
}

void drawFace() {
  switch (gState) {
    case DeviceState::Online:
      gDisplay.fillCircle(86, 97, 3, GxEPD_BLACK);
      gDisplay.fillCircle(114, 97, 3, GxEPD_BLACK);
      drawSmile();
      break;
    case DeviceState::Listening:
      gDisplay.fillCircle(85, 97, 4, GxEPD_BLACK);
      gDisplay.fillCircle(115, 97, 4, GxEPD_BLACK);
      gDisplay.fillCircle(100, 111, 3, GxEPD_BLACK);
      break;
    case DeviceState::Thinking:
      gDisplay.drawLine(79, 98, 91, 100, GxEPD_BLACK);
      gDisplay.drawLine(109, 100, 121, 98, GxEPD_BLACK);
      gDisplay.fillCircle(92, 113, 2, GxEPD_BLACK);
      gDisplay.fillCircle(100, 113, 2, GxEPD_BLACK);
      gDisplay.fillCircle(108, 113, 2, GxEPD_BLACK);
      break;
    case DeviceState::PlayingAudio:
      gDisplay.fillCircle(86, 97, 3, GxEPD_BLACK);
      gDisplay.fillCircle(114, 97, 3, GxEPD_BLACK);
      drawSmile(true);
      break;
    case DeviceState::MemoryConfirmation:
      gDisplay.fillCircle(86, 98, 3, GxEPD_BLACK);
      gDisplay.drawLine(109, 96, 119, 94, GxEPD_BLACK);
      gDisplay.drawLine(110, 101, 119, 99, GxEPD_BLACK);
      gDisplay.drawCircle(100, 112, 5, GxEPD_BLACK);
      gDisplay.drawLine(100, 112, 100, 116, GxEPD_BLACK);
      gDisplay.fillCircle(100, 121, 1, GxEPD_BLACK);
      break;
    case DeviceState::Error:
      gDisplay.drawLine(79, 94, 91, 100, GxEPD_BLACK);
      gDisplay.drawLine(109, 100, 121, 94, GxEPD_BLACK);
      gDisplay.drawLine(88, 114, 112, 114, GxEPD_BLACK);
      break;
    default:
      gDisplay.drawLine(80, 99, 91, 99, GxEPD_BLACK);
      gDisplay.drawLine(109, 99, 120, 99, GxEPD_BLACK);
      gDisplay.drawLine(91, 112, 109, 112, GxEPD_BLACK);
      break;
  }
}

void drawContext() {
  const String detail = shortenedText(gStateDetail, 54);
  gDisplay.setTextSize(1);
  gDisplay.setCursor(10, 153);
  gDisplay.print(detail.substring(0, 29));
  if (detail.length() > 29) {
    gDisplay.setCursor(10, 165);
    gDisplay.print(detail.substring(29));
  }
  if (gState == DeviceState::MemoryConfirmation) {
    gDisplay.setCursor(10, 189);
    gDisplay.print("BOOT guardar  PWR descartar");
  } else if (gState == DeviceState::Provisioning) {
    gDisplay.setCursor(10, 189);
    gDisplay.print("Portal activo: usa el telefono");
  } else if (gState == DeviceState::Wps) {
    gDisplay.setCursor(10, 189);
    gDisplay.print("Presiona WPS en el router");
  }
}

void renderState() {
  const BatteryReading battery = readBattery();
  if (battery.valid) {
    Serial.printf("BATTERY voltage=%u mV percent=%u\n", battery.millivolts, battery.percent);
  } else {
    Serial.println("BATTERY voltage=invalid");
  }
  gDisplay.setFullWindow();
  gDisplay.firstPage();
  do {
    gDisplay.fillScreen(GxEPD_WHITE);
    gDisplay.setTextColor(GxEPD_BLACK);
    drawStatusBar(battery);
    drawCloud();
    drawFace();
    drawContext();
  } while (gDisplay.nextPage());
}

void setState(DeviceState state, const String& detail) {
  if (gState == state && gStateDetail == detail) {
    return;
  }
  gState = state;
  gStateDetail = detail;
  // GPIO3 is the validated active-high indicator: lit means firmware is awake.
  digitalWrite(board::PIN_STATUS_LED, HIGH);
  Serial.printf("STATE %s: %s\n", stateName(state), detail.c_str());
  renderState();
}

void loadConfig() {
  gPreferences.begin("ai-buddy", true);
  gConfig.wifiSsid = gPreferences.getString("wifi_ssid", "");
  gConfig.wifiPassword = gPreferences.getString("wifi_pass", "");
  gConfig.backendBaseUrl = gPreferences.getString("backend", "");
  gConfig.deviceToken = gPreferences.getString("token", "");
  gConfig.deviceId = gPreferences.getString("device_id", deviceIdFromMac());
  gPreferences.end();

  if (gConfig.wifiSsid.isEmpty()) {
    gConfig.wifiSsid = AI_BUDDY_DEFAULT_WIFI_SSID;
  }
  if (gConfig.wifiPassword.isEmpty()) {
    gConfig.wifiPassword = AI_BUDDY_DEFAULT_WIFI_PASSWORD;
  }
  if (gConfig.backendBaseUrl.isEmpty()) {
    gConfig.backendBaseUrl = AI_BUDDY_DEFAULT_BACKEND_URL;
  }
  if (gConfig.deviceToken.isEmpty()) {
    gConfig.deviceToken = AI_BUDDY_DEFAULT_DEVICE_TOKEN;
  }

  const String migrateFrom = AI_BUDDY_BACKEND_MIGRATE_FROM;
  const String migrateTo = AI_BUDDY_BACKEND_MIGRATE_TO;
  if (!migrateFrom.isEmpty() && !migrateTo.isEmpty() && gConfig.backendBaseUrl == migrateFrom) {
    gConfig.backendBaseUrl = migrateTo;
    saveConfig();
  }
}

bool configComplete() {
  return !gConfig.wifiSsid.isEmpty() && !gConfig.backendBaseUrl.isEmpty() && !gConfig.deviceToken.isEmpty();
}

void saveConfig() {
  gPreferences.begin("ai-buddy", false);
  gPreferences.putString("wifi_ssid", gConfig.wifiSsid);
  gPreferences.putString("wifi_pass", gConfig.wifiPassword);
  gPreferences.putString("backend", gConfig.backendBaseUrl);
  gPreferences.putString("token", gConfig.deviceToken);
  gPreferences.putString("device_id", gConfig.deviceId);
  gPreferences.end();
}

void clearConfig() {
  gPreferences.begin("ai-buddy", false);
  gPreferences.clear();
  gPreferences.end();
}

void showPortal() {
  const String form = String(F("<!doctype html><html><meta name=viewport content='width=device-width,initial-scale=1'>"
                                "<title>AI Buddy setup</title><style>body{font-family:sans-serif;max-width:34rem;margin:2rem auto;padding:0 1rem}"
                                "label{display:block;margin-top:1rem}input{box-sizing:border-box;width:100%;padding:.6rem}button{margin-top:1.25rem;padding:.7rem 1rem}</style>"
                                "<h1>AI Buddy setup</h1><form method=post action=/save>"
                                "<label>Wi-Fi name<input name=ssid value='")) + escapedHtml(gConfig.wifiSsid) +
                      F("'></label><label>Wi-Fi password<input type=password name=password></label>"
                        "<label>Backend URL<input name=backend placeholder='http://192.168.1.4:8000' value='") +
                      escapedHtml(gConfig.backendBaseUrl) + F("'></label><label>Device token<input type=password name=token></label>"
                                                        "<label>Device ID<input name=device_id value='") +
                      escapedHtml(gConfig.deviceId) + F("'></label><button>Save and restart</button></form></html>");
  gPortal.send(200, "text/html", form);
}

void savePortalConfig() {
  gConfig.wifiSsid = gPortal.arg("ssid");
  const String password = gPortal.arg("password");
  if (!password.isEmpty()) {
    gConfig.wifiPassword = password;
  }
  gConfig.backendBaseUrl = gPortal.arg("backend");
  while (gConfig.backendBaseUrl.endsWith("/")) {
    gConfig.backendBaseUrl.remove(gConfig.backendBaseUrl.length() - 1);
  }
  const String token = gPortal.arg("token");
  if (!token.isEmpty()) {
    gConfig.deviceToken = token;
  }
  gConfig.deviceId = gPortal.arg("device_id");
  if (gConfig.deviceId.isEmpty()) {
    gConfig.deviceId = deviceIdFromMac();
  }
  saveConfig();
  gPortal.send(200, "text/html", "<h1>Saved</h1><p>AI Buddy will restart now.</p>");
  delay(500);
  ESP.restart();
}

void startPortal(bool preserveStation = false) {
  if (preserveStation) {
    WiFi.mode(WIFI_AP_STA);
  } else {
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_AP);
  }
  const String password = setupPassword();
  WiFi.softAP(kPortalSsid, password.c_str());
  gPortal.on("/", HTTP_GET, showPortal);
  gPortal.on("/save", HTTP_POST, savePortalConfig);
  gPortal.begin();
  setState(DeviceState::Provisioning, String("Join ") + kPortalSsid + " / " + password + " or hold PWR for WPS");
  Serial.printf("Provisioning portal: SSID=%s password=%s IP=%s\n", kPortalSsid, password.c_str(), WiFi.softAPIP().toString().c_str());
}

bool connectWifi() {
  setState(DeviceState::ConnectingWifi, gConfig.wifiSsid);
  WiFi.mode(WIFI_STA);
  if (gConfig.wifiPassword.isEmpty()) {
    WiFi.begin();
  } else {
    WiFi.begin(gConfig.wifiSsid.c_str(), gConfig.wifiPassword.c_str());
  }
  const uint32_t startedAtMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAtMs < kWifiConnectTimeoutMs) {
    delay(200);
  }
  if (WiFi.status() != WL_CONNECTED) {
    setState(DeviceState::Error, "Wi-Fi failed; setup mode");
    return false;
  }
  Serial.printf("Wi-Fi connected: %s\n", WiFi.localIP().toString().c_str());
  return true;
}

void stopWps() {
  if (!gWpsActive) {
    return;
  }
  esp_wifi_wps_disable();
  gWpsActive = false;
}

void startWps() {
  if (gWpsActive) {
    return;
  }
  gPortal.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);

  esp_wps_config_t config = {};
  config.wps_type = WPS_TYPE_PBC;
  snprintf(config.factory_info.manufacturer, sizeof(config.factory_info.manufacturer), "AI Buddy");
  snprintf(config.factory_info.model_name, sizeof(config.factory_info.model_name), "Personal Agent");
  snprintf(config.factory_info.device_name, sizeof(config.factory_info.device_name), "%s", gConfig.deviceId.c_str());
  snprintf(config.pin, sizeof(config.pin), "00000000");
  const esp_err_t enableResult = esp_wifi_wps_enable(&config);
  if (enableResult != ESP_OK) {
    setState(DeviceState::Error, "WPS unavailable");
    return;
  }
  const esp_err_t startResult = esp_wifi_wps_start(0);
  if (startResult != ESP_OK) {
    esp_wifi_wps_disable();
    setState(DeviceState::Error, "WPS could not start");
    return;
  }
  gWpsActive = true;
  gWpsSucceeded = false;
  gWpsStartedAtMs = millis();
  setState(DeviceState::Wps, "Press WPS on your router now");
  Serial.println("WPS started; waiting for router button");
}

void onWifiEvent(WiFiEvent_t event, arduino_event_info_t) {
  if (event == ARDUINO_EVENT_WPS_ER_SUCCESS) {
    gWpsSucceeded = true;
  }
}

void processWps() {
  if (!gWpsActive) {
    return;
  }
  if (gWpsSucceeded) {
    stopWps();
    WiFi.begin();
    gConfig.wifiSsid = WiFi.SSID();
    gConfig.wifiPassword = "";
    saveConfig();
    if (configComplete()) {
      startConnection();
    } else {
      startPortal(true);
    }
    return;
  }
  if (millis() - gWpsStartedAtMs >= kWpsTimeoutMs) {
    stopWps();
    setState(DeviceState::Error, "WPS timed out; try again");
    startPortal();
  }
}

bool parseWebsocketUrl(const String& url, String* host, uint16_t* port, String* path, bool* secure) {
  int authorityStart = 0;
  if (url.startsWith("wss://")) {
    authorityStart = 6;
    *secure = true;
  } else if (url.startsWith("ws://")) {
    authorityStart = 5;
    *secure = false;
  } else {
    return false;
  }
  const int pathStart = url.indexOf('/', authorityStart);
  const String authority = pathStart < 0 ? url.substring(authorityStart) : url.substring(authorityStart, pathStart);
  const int colon = authority.lastIndexOf(':');
  *host = colon < 0 ? authority : authority.substring(0, colon);
  *port = colon < 0 ? (*secure ? 443 : 80) : static_cast<uint16_t>(authority.substring(colon + 1).toInt());
  *path = pathStart < 0 ? "/" : url.substring(pathStart);
  return !host->isEmpty() && *port != 0;
}

void sendJson(JsonDocument& document) {
  String payload;
  serializeJson(document, payload);
  gSocket.sendTXT(payload);
}

void handleSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_CONNECTED) {
    gSocketConnected = true;
    setState(DeviceState::Online, WiFi.localIP().toString());
    JsonDocument hello;
    hello["type"] = "hello";
    hello["device_id"] = gConfig.deviceId;
    hello["protocol_version"] = kProtocolVersion;
    sendJson(hello);
    JsonDocument boot;
    boot["type"] = "boot";
    boot["device_id"] = gConfig.deviceId;
    sendJson(boot);
    return;
  }
  if (type == WStype_DISCONNECTED) {
    gSocketConnected = false;
    setState(DeviceState::ConnectingSession, "Session disconnected");
    return;
  }
  if (type == WStype_BIN) {
    if (!gAudio.appendRemoteAudio(payload, length)) {
      setState(DeviceState::Error, "Invalid response audio");
      return;
    }
    if (gAudio.remoteAudioComplete()) {
      gAudio.playRemoteAudio();
      setState(DeviceState::Online, WiFi.localIP().toString());
    }
    return;
  }
  if (type != WStype_TEXT) {
    return;
  }

  JsonDocument message;
  if (deserializeJson(message, payload, length)) {
    return;
  }
  const char* messageType = message["type"] | "";
  if (strcmp(messageType, "show_text") == 0) {
    setState(DeviceState::Online, String(message["text"] | ""));
  } else if (strcmp(messageType, "audio_receiving") == 0) {
    setState(DeviceState::Listening, "Transcribing audio");
  } else if (strcmp(messageType, "transcription") == 0) {
    setState(DeviceState::Online, String(message["text"] | ""));
  } else if (strcmp(messageType, "thinking") == 0) {
    setState(DeviceState::Thinking, String(message["text"] | "Pensando..."));
  } else if (strcmp(messageType, "assistant_audio_start") == 0) {
    const size_t bytes = message["bytes"] | 0;
    if (String(message["format"] | "") != "pcm_s16le" ||
        message["sample_rate"] != 16000 ||
        message["channels"] != 2 ||
        bytes == 0 || bytes > 512000 || bytes % 4 != 0) {
      setState(DeviceState::Error, "Invalid response metadata");
      return;
    }
    if (!gAudio.beginRemoteAudio(bytes)) {
      setState(DeviceState::Error, "Response audio unavailable");
      return;
    }
    setState(DeviceState::PlayingAudio, "Buddy speaking");
  } else if (strcmp(messageType, "memory_confirmation") == 0) {
    gPendingMemoryId = String(message["request_id"] | "");
    if (gPendingMemoryId.isEmpty()) {
      setState(DeviceState::Error, "Invalid memory request");
      return;
    }
    setState(DeviceState::MemoryConfirmation, String(message["content"] | "BOOT yes PWR no"));
  } else if (strcmp(messageType, "memory_saved") == 0) {
    gPendingMemoryId = "";
    setState(DeviceState::Online, "Memory saved");
  } else if (strcmp(messageType, "memory_rejected") == 0) {
    gPendingMemoryId = "";
    setState(DeviceState::Online, "Memory not saved");
  } else if (strcmp(messageType, "action_blocked") == 0) {
    setState(DeviceState::Online, "Action needs confirmation");
  } else if (strcmp(messageType, "error") == 0) {
    setState(DeviceState::Error, String(message["code"] | "Audio error"));
  }
}

bool fetchDeviceConfig() {
  setState(DeviceState::FetchingConfig, gConfig.backendBaseUrl);
  HTTPClient http;
  const String url = gConfig.backendBaseUrl + "/device/config?device_id=" + gConfig.deviceId;
  WiFiClientSecure secureClient;
  bool started = false;
  if (url.startsWith("https://")) {
    // The deployed endpoint is Cloudflare-managed. ESP32's bundled CA store is not used here.
    secureClient.setInsecure();
    started = http.begin(secureClient, url);
  } else {
    started = http.begin(url);
  }
  if (!started) {
    setState(DeviceState::Error, "Invalid backend URL");
    return false;
  }
  http.addHeader("X-Device-Token", gConfig.deviceToken);
  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    setState(DeviceState::Error, String("Config HTTP ") + httpCode);
    http.end();
    return false;
  }
  JsonDocument config;
  const DeserializationError error = deserializeJson(config, http.getString());
  http.end();
  if (error || String(config["protocol_version"] | "") != kProtocolVersion) {
    setState(DeviceState::Error, "Invalid config response");
    return false;
  }
  gSessionUrl = String(config["session_url"] | "");
  return !gSessionUrl.isEmpty();
}

bool connectSocket() {
  String host;
  String path;
  uint16_t port = 0;
  bool secure = false;
  if (!parseWebsocketUrl(gSessionUrl, &host, &port, &path, &secure)) {
    setState(DeviceState::Error, "Expected ws:// or wss:// URL");
    return false;
  }
  setState(DeviceState::ConnectingSession, host);
  path += (path.indexOf('?') >= 0 ? "&" : "?");
  path += "token=" + gConfig.deviceToken;
  if (secure) {
    gSocket.beginSSL(host.c_str(), port, path.c_str());
  } else {
    gSocket.begin(host.c_str(), port, path.c_str());
  }
  gSocket.onEvent(handleSocketEvent);
  gSocket.setReconnectInterval(kReconnectIntervalMs);
  return true;
}

void startConnection() {
  if (!connectWifi()) {
    startPortal();
    return;
  }
  if (!fetchDeviceConfig()) {
    return;
  }
  connectSocket();
}

void handleBootButton() {
  const bool pressed = digitalRead(board::PIN_BTN_TOP) == LOW;
  if (pressed && !gBootButtonDown) {
    gBootButtonDown = true;
    gBootButtonPressedAtMs = millis();
    return;
  }
  if (!pressed && gBootButtonDown) {
    const uint32_t heldMs = millis() - gBootButtonPressedAtMs;
    gBootButtonDown = false;
    if (heldMs >= kResetHoldMs) {
      Serial.println("Clearing saved configuration");
      clearConfig();
      ESP.restart();
    }
    if (!gPendingMemoryId.isEmpty()) {
      JsonDocument confirmation;
      confirmation["type"] = "memory_confirm";
      confirmation["request_id"] = gPendingMemoryId;
      sendJson(confirmation);
      setState(DeviceState::MemoryConfirmation, "Saving memory");
      return;
    }
    if (gAudio.isRecording()) {
      gAudio.finishRecording();
      Serial.printf("AUDIO captured bytes=%u\n", static_cast<unsigned>(gAudio.recordedBytes()));
      if (gSocketConnected && gAudio.hasRecording()) {
        JsonDocument metadata;
        metadata["type"] = "audio_start";
        metadata["format"] = "pcm_s16le";
        metadata["sample_rate"] = 16000;
        metadata["channels"] = 2;
        metadata["bytes"] = gAudio.recordedBytes();
        sendJson(metadata);
        if (gSocket.sendBIN(gAudio.recordingData(), gAudio.recordedBytes())) {
          setState(DeviceState::Listening, "Sending audio");
        } else {
          setState(DeviceState::Error, "Audio upload failed");
        }
      } else {
        setState(DeviceState::PlayingAudio, "Replaying local audio");
        gAudio.playRecording();
        setState(DeviceState::Online, WiFi.localIP().toString());
      }
      return;
    }
    if (gAudioReady) {
      gAudio.playTone(880, 500);
      if (gAudio.startRecording()) {
        setState(DeviceState::Listening, "Press BOOT again to send");
        Serial.println("AUDIO recording started");
        return;
      }
    }
    if (gSocketConnected) {
      JsonDocument event;
      event["type"] = "button";
      event["button"] = "boot";
      sendJson(event);
    }
  }
}

void handlePowerButton() {
  const bool pressed = digitalRead(board::PIN_BTN_BOTTOM) == LOW;
  if (pressed && !gPowerButtonDown) {
    gPowerButtonDown = true;
    gPowerButtonPressedAtMs = millis();
    return;
  }
  if (!pressed && gPowerButtonDown) {
    const uint32_t heldMs = millis() - gPowerButtonPressedAtMs;
    gPowerButtonDown = false;
    if (heldMs < kWpsHoldMs && !gPendingMemoryId.isEmpty()) {
      JsonDocument rejection;
      rejection["type"] = "memory_reject";
      rejection["request_id"] = gPendingMemoryId;
      sendJson(rejection);
      setState(DeviceState::MemoryConfirmation, "Discarding memory");
      return;
    }
    if (heldMs >= kWpsHoldMs) {
      startWps();
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("AI Buddy client starting");

  pinMode(board::PIN_VBAT_HOLD, OUTPUT);
  digitalWrite(board::PIN_VBAT_HOLD, HIGH);
  analogReadResolution(12);
  analogSetPinAttenuation(board::PIN_VBAT_ADC, ADC_11db);
  pinMode(board::PIN_STATUS_LED, OUTPUT);
  digitalWrite(board::PIN_STATUS_LED, HIGH);
  pinMode(board::PIN_BTN_TOP, INPUT_PULLUP);
  pinMode(board::PIN_BTN_BOTTOM, INPUT_PULLUP);
  pinMode(board::PIN_EPD_POWER, OUTPUT);
  digitalWrite(board::PIN_EPD_POWER, LOW);
  SPI.begin(board::PIN_EPD_SCLK, -1, board::PIN_EPD_MOSI, board::PIN_EPD_CS);
  gDisplay.init(115200, true, 10, false);
  gDisplay.setRotation(0);
  gDisplay.setTextColor(GxEPD_BLACK);

  Wire.begin(board::PIN_I2C_SDA, board::PIN_I2C_SCL);
  gAudioReady = gAudio.begin();
  Serial.printf("AUDIO codec=%s free_psram=%u\n", gAudioReady ? "ok" : "fail", static_cast<unsigned>(ESP.getFreePsram()));
  if (gAudioReady) {
    gAudio.playStartupChime();
  }

  loadConfig();
  WiFi.onEvent(onWifiEvent);
  setState(DeviceState::Boot, gConfig.deviceId);
  if (!configComplete()) {
    startPortal();
    return;
  }
  startConnection();
}

void loop() {
  gAudio.captureStep();
  handleBootButton();
  handlePowerButton();
  processWps();
  if (gWpsActive) {
    delay(10);
    return;
  }
  if (gState == DeviceState::Provisioning) {
    gPortal.handleClient();
    delay(10);
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - gLastReconnectAtMs >= kReconnectIntervalMs) {
      gLastReconnectAtMs = millis();
      startConnection();
    }
    return;
  }

  gSocket.loop();
  if (!gSocketConnected && millis() - gLastReconnectAtMs >= kReconnectIntervalMs) {
    gLastReconnectAtMs = millis();
    if (fetchDeviceConfig()) {
      connectSocket();
    }
  }
  delay(10);
}
