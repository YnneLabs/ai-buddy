#include <Arduino.h>
#include <ArduinoJson.h>
#include <GxEPD2_BW.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <SPI.h>
#include <WebServer.h>
#include <WebSocketsClient.h>
#include <WiFi.h>
#include <esp_wps.h>

#include "board_pins.h"

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

enum class DeviceState : uint8_t {
  Boot,
  Provisioning,
  Wps,
  ConnectingWifi,
  FetchingConfig,
  ConnectingSession,
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
bool gSocketConnected = false;
bool gBootButtonDown = false;
uint32_t gBootButtonPressedAtMs = 0;
bool gPowerButtonDown = false;
uint32_t gPowerButtonPressedAtMs = 0;
uint32_t gLastReconnectAtMs = 0;
bool gWpsActive = false;
bool gWpsSucceeded = false;
uint32_t gWpsStartedAtMs = 0;

void startConnection();

const char* stateName(DeviceState state) {
  switch (state) {
    case DeviceState::Boot:
      return "Starting";
    case DeviceState::Provisioning:
      return "Setup Wi-Fi";
    case DeviceState::Wps:
      return "Router WPS";
    case DeviceState::ConnectingWifi:
      return "Connecting Wi-Fi";
    case DeviceState::FetchingConfig:
      return "Getting config";
    case DeviceState::ConnectingSession:
      return "Connecting buddy";
    case DeviceState::Online:
      return "Buddy online";
    case DeviceState::Error:
      return "Connection error";
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

void renderState() {
  gDisplay.setFullWindow();
  gDisplay.firstPage();
  do {
    gDisplay.fillScreen(GxEPD_WHITE);
    gDisplay.setTextColor(GxEPD_BLACK);
    gDisplay.setTextSize(2);
    gDisplay.setCursor(12, 24);
    gDisplay.print("AI Buddy");
    gDisplay.drawLine(12, 32, 188, 32, GxEPD_BLACK);
    gDisplay.setTextSize(2);
    gDisplay.setCursor(12, 66);
    gDisplay.print(stateName(gState));
    gDisplay.setTextSize(1);
    gDisplay.setCursor(12, 90);
    gDisplay.print(gStateDetail);
    gDisplay.drawCircle(100, 135, 34, GxEPD_BLACK);
    if (gState == DeviceState::Online) {
      gDisplay.fillCircle(88, 128, 3, GxEPD_BLACK);
      gDisplay.fillCircle(112, 128, 3, GxEPD_BLACK);
      gDisplay.drawLine(84, 146, 100, 154, GxEPD_BLACK);
      gDisplay.drawLine(100, 154, 116, 146, GxEPD_BLACK);
    } else {
      gDisplay.drawLine(86, 128, 94, 128, GxEPD_BLACK);
      gDisplay.drawLine(106, 128, 114, 128, GxEPD_BLACK);
      gDisplay.drawLine(88, 150, 112, 150, GxEPD_BLACK);
    }
    gDisplay.setTextSize(1);
    gDisplay.setCursor(12, 190);
    gDisplay.print("PWR 1.5s: WPS  BOOT 3s: reset");
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

bool parseWebsocketUrl(const String& url, String* host, uint16_t* port, String* path) {
  if (!url.startsWith("ws://")) {
    return false;
  }
  const int authorityStart = 5;
  const int pathStart = url.indexOf('/', authorityStart);
  const String authority = pathStart < 0 ? url.substring(authorityStart) : url.substring(authorityStart, pathStart);
  const int colon = authority.lastIndexOf(':');
  *host = colon < 0 ? authority : authority.substring(0, colon);
  *port = colon < 0 ? 80 : static_cast<uint16_t>(authority.substring(colon + 1).toInt());
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
  }
}

bool fetchDeviceConfig() {
  setState(DeviceState::FetchingConfig, gConfig.backendBaseUrl);
  HTTPClient http;
  const String url = gConfig.backendBaseUrl + "/device/config?device_id=" + gConfig.deviceId;
  if (!http.begin(url)) {
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
  if (!parseWebsocketUrl(gSessionUrl, &host, &port, &path)) {
    setState(DeviceState::Error, "Expected ws:// session URL");
    return false;
  }
  setState(DeviceState::ConnectingSession, host);
  path += (path.indexOf('?') >= 0 ? "&" : "?");
  path += "token=" + gConfig.deviceToken;
  gSocket.begin(host.c_str(), port, path.c_str());
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
