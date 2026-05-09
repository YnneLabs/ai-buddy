#pragma once

#include <Arduino.h>

namespace board {

constexpr uint8_t PIN_EPD_POWER = 6;
constexpr uint8_t PIN_EPD_BUSY = 8;
constexpr uint8_t PIN_EPD_RST = 9;
constexpr uint8_t PIN_EPD_DC = 10;
constexpr uint8_t PIN_EPD_CS = 11;
constexpr uint8_t PIN_EPD_SCLK = 12;
constexpr uint8_t PIN_EPD_MOSI = 13;

constexpr uint8_t PIN_BTN_TOP = 0;      // BOOT button on the board
constexpr uint8_t PIN_BTN_BOTTOM = 18;  // Lower side button (PWR on most boards)
constexpr uint8_t PIN_VBAT_HOLD = 17;

constexpr uint8_t PIN_I2C_SDA = 47;
constexpr uint8_t PIN_I2C_SCL = 48;

constexpr uint8_t PIN_I2S_MCLK = 14;
constexpr uint8_t PIN_I2S_BCLK = 15;
constexpr uint8_t PIN_I2S_DIN = 16;   // Codec ADC -> ESP32
constexpr uint8_t PIN_I2S_LRCK = 43;
constexpr uint8_t PIN_I2S_DOUT = 44;  // ESP32 -> Codec DAC

constexpr uint8_t PIN_AUDIO_PWR = 42;
constexpr uint8_t PIN_AUDIO_CTRL = 46;
constexpr uint8_t PIN_STATUS_LED = 3;

constexpr uint8_t I2C_ADDR_RTC = 0x51;
constexpr uint8_t I2C_ADDR_ES8311 = 0x18;

}  // namespace board
