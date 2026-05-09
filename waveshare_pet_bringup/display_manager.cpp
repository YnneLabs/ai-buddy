#include "display_manager.h"

#include <GxEPD2_BW.h>
#include <SPI.h>

#include "board_pins.h"

namespace {

using DisplayType = GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT>;
DisplayType display(GxEPD2_154_D67(board::PIN_EPD_CS, board::PIN_EPD_DC, board::PIN_EPD_RST, board::PIN_EPD_BUSY));

constexpr int16_t kCenterX = 100;
constexpr int16_t kCenterY = 102;
constexpr int16_t kFaceRadius = 48;

}  // namespace

bool DisplayManager::begin() {
  pinMode(board::PIN_EPD_POWER, OUTPUT);
  digitalWrite(board::PIN_EPD_POWER, LOW);
  delay(20);

  SPI.begin(board::PIN_EPD_SCLK, -1, board::PIN_EPD_MOSI, board::PIN_EPD_CS);
  display.init(115200, true, 10, false);
  display.setRotation(0);
  display.setTextColor(GxEPD_BLACK);
  display.setTextSize(1);
  display.setFullWindow();
  return true;
}

void DisplayManager::renderHome(const String& timeText,
                                const String& climateText,
                                PetEmotion emotion,
                                OverlayState overlay,
                                bool forceFullRefresh) {
  (void)forceFullRefresh;
  display.setFullWindow();

  display.firstPage();
  do {
    drawFrame(timeText, climateText, emotion, overlay);
  } while (display.nextPage());
}

void DisplayManager::renderSleep(const String& timeText, PetEmotion emotion) {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setFont(nullptr);
    display.setTextSize(2);
    display.setCursor(10, 18);
    display.print(timeText);
    drawFace(emotion);
    display.setTextSize(1);
    display.setCursor(78, 188);
    display.print("sleep");
  } while (display.nextPage());
}

void DisplayManager::sleep() {
  display.hibernate();
}

void DisplayManager::drawFrame(const String& timeText,
                               const String& climateText,
                               PetEmotion emotion,
                               OverlayState overlay) {
  display.fillScreen(GxEPD_WHITE);
  display.setFont(nullptr);
  display.setTextSize(2);
  display.setCursor(10, 18);
  display.print(timeText);
  display.setTextSize(1);
  display.setCursor(10, 32);
  display.print(climateText);

  drawIcon();
  drawFace(emotion);
  drawEmotionLabel(emotion);
  drawOverlay(overlay);
}

void DisplayManager::drawFace(PetEmotion emotion) {
  display.drawCircle(kCenterX, kCenterY, kFaceRadius, GxEPD_BLACK);

  switch (emotion) {
    case PetEmotion::Neutral:
      display.fillCircle(kCenterX - 16, kCenterY - 10, 4, GxEPD_BLACK);
      display.fillCircle(kCenterX + 16, kCenterY - 10, 4, GxEPD_BLACK);
      display.drawLine(kCenterX - 18, kCenterY + 18, kCenterX + 18, kCenterY + 18, GxEPD_BLACK);
      break;
    case PetEmotion::Happy:
      display.fillCircle(kCenterX - 16, kCenterY - 10, 4, GxEPD_BLACK);
      display.fillCircle(kCenterX + 16, kCenterY - 10, 4, GxEPD_BLACK);
      display.drawLine(kCenterX - 20, kCenterY + 10, kCenterX - 8, kCenterY + 22, GxEPD_BLACK);
      display.drawLine(kCenterX - 8, kCenterY + 22, kCenterX + 8, kCenterY + 22, GxEPD_BLACK);
      display.drawLine(kCenterX + 8, kCenterY + 22, kCenterX + 20, kCenterY + 10, GxEPD_BLACK);
      break;
    case PetEmotion::Sleepy:
      display.drawLine(kCenterX - 22, kCenterY - 10, kCenterX - 10, kCenterY - 10, GxEPD_BLACK);
      display.drawLine(kCenterX + 10, kCenterY - 10, kCenterX + 22, kCenterY - 10, GxEPD_BLACK);
      display.drawLine(kCenterX - 14, kCenterY + 16, kCenterX + 14, kCenterY + 16, GxEPD_BLACK);
      display.setTextSize(1);
      display.setCursor(kCenterX + 24, kCenterY - 24);
      display.print("Z");
      break;
    case PetEmotion::Surprised:
      display.drawCircle(kCenterX - 16, kCenterY - 10, 5, GxEPD_BLACK);
      display.drawCircle(kCenterX + 16, kCenterY - 10, 5, GxEPD_BLACK);
      display.drawCircle(kCenterX, kCenterY + 18, 8, GxEPD_BLACK);
      break;
    case PetEmotion::Sad:
      display.fillCircle(kCenterX - 16, kCenterY - 10, 4, GxEPD_BLACK);
      display.fillCircle(kCenterX + 16, kCenterY - 10, 4, GxEPD_BLACK);
      display.drawLine(kCenterX - 16, kCenterY + 28, kCenterX, kCenterY + 16, GxEPD_BLACK);
      display.drawLine(kCenterX, kCenterY + 16, kCenterX + 16, kCenterY + 28, GxEPD_BLACK);
      display.drawPixel(kCenterX + 26, kCenterY + 2, GxEPD_BLACK);
      display.drawPixel(kCenterX + 27, kCenterY + 6, GxEPD_BLACK);
      break;
    default:
      break;
  }
}

void DisplayManager::drawOverlay(OverlayState overlay) {
  if (overlay == OverlayState::None) {
    return;
  }

  display.fillRect(140, 8, 50, 22, GxEPD_BLACK);
  display.setTextColor(GxEPD_WHITE);
  display.setFont(nullptr);
  display.setTextSize(1);
  display.setCursor(148, 23);
  display.print(overlay == OverlayState::Recording ? "REC" : "PLAY");
  display.setTextColor(GxEPD_BLACK);
}

void DisplayManager::drawIcon() {
  display.drawRect(174, 8, 14, 8, GxEPD_BLACK);
  display.fillRect(188, 10, 2, 4, GxEPD_BLACK);
  display.fillRect(176, 10, 8, 4, GxEPD_BLACK);
}

void DisplayManager::drawEmotionLabel(PetEmotion emotion) {
  const char* label = emotionName(emotion);
  display.setFont(nullptr);
  display.setTextSize(1);
  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t w = 0;
  uint16_t h = 0;
  display.getTextBounds(label, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((display.width() - static_cast<int16_t>(w)) / 2, 188);
  display.print(label);
}
