#pragma once

#include <Arduino.h>

// Mirrors the validated pin mapping in waveshare_pet_bringup/board_pins.h.
namespace board {

constexpr uint8_t PIN_EPD_POWER = 6;
constexpr uint8_t PIN_EPD_BUSY = 8;
constexpr uint8_t PIN_EPD_RST = 9;
constexpr uint8_t PIN_EPD_DC = 10;
constexpr uint8_t PIN_EPD_CS = 11;
constexpr uint8_t PIN_EPD_SCLK = 12;
constexpr uint8_t PIN_EPD_MOSI = 13;

constexpr uint8_t PIN_BTN_TOP = 0;
constexpr uint8_t PIN_VBAT_HOLD = 17;

}  // namespace board
