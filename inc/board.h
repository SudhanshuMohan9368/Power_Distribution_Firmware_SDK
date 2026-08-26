#ifndef POWER_DISTRIBUTION_CAN_BOARD_H
#define POWER_DISTRIBUTION_CAN_BOARD_H

#include <cstdint>

// Ported 1:1 from the reference PlatformIO/Arduino firmware's include/pins.h
// (Documents/PlatformIO/Projects/Power_distribution_Board) onto the
// pico-sdk/CMake structure used by the Lapras control board firmware.

// -- CAN --------------------------------------------------------
constexpr uint8_t PIN_CAN_TX    = 2;
constexpr uint8_t PIN_CAN_RX    = 3;
constexpr uint8_t PIN_CAN_STBY  = 7;
constexpr uint8_t PIN_CAN_STBY2 = 10;

// -- Outputs ------------------------------------------------------
constexpr uint8_t PIN_SMPS         = 9;    // SMPS MOSFET, HIGH = ON
constexpr uint8_t PIN_FAN          = 18;   // HEPA fan, 12V on/off (no PWM)
constexpr uint8_t PIN_UV           = 19;   // UV lamp
constexpr uint8_t PIN_LIGHT_1      = 17;   // work light
constexpr uint8_t PIN_HEATER_RELAY = 16;   // heater board AC supply relay -- default OFF, CAN/serial-only
constexpr uint8_t PIN_NEOPIXEL     = 13;
constexpr uint8_t PIN_LED_CAN      = 24;
constexpr uint8_t PIN_LED_STATUS   = 25;

// -- Inputs -------------------------------------------------------
constexpr uint8_t PIN_EMERGENCY = 21;  // active LOW
constexpr uint8_t PIN_POWER_BTN = 20;  // active LOW, start-only
constexpr uint8_t PIN_LIMIT_SW  = 12;  // active LOW
constexpr uint8_t PIN_MAG_HALL  = 14;  // active LOW, open-drain sensor needs pull-up
constexpr uint8_t PIN_ADC3      = 29;  // 24V rail sense
constexpr uint8_t ADC3_CHANNEL  = 3;   // GPIO29 = ADC input 3 on RP2040

// -- 24V rail divider -- verify R3_OHMS against actual BOM --------
constexpr float R2_OHMS = 10000.0f;
constexpr float R3_OHMS = 1000.0f;

// -- CAN IDs --------------------------------------------------------
constexpr uint16_t CAN_ID_CMD  = 0x6A5;  // master -> board
constexpr uint16_t CAN_ID_RESP = 0x6B5;  // board -> master
constexpr uint32_t CAN_BITRATE = 500000; // 500 kbps

// -- Output bit masks -- STATUS frame d[1] -------------------------
// bit0=SMPS bit1=UV bit2=Fan bit3=Light bit4=Heater relay bits5-7=reserved
constexpr uint8_t BIT_SMPS   = 1 << 0;
constexpr uint8_t BIT_UV     = 1 << 1;
constexpr uint8_t BIT_FAN    = 1 << 2;
constexpr uint8_t BIT_LIGHT  = 1 << 3;
constexpr uint8_t BIT_HEATER = 1 << 4;

// -- Timing -----------------------------------------------------------
constexpr uint32_t DEBOUNCE_MS       = 50;
constexpr uint32_t HEARTBEAT_MS      = 1000;
constexpr uint32_t CAN_PULSE_MS      = 120;
constexpr uint32_t SOFT_OFF_DELAY_MS = 10000;  // CAN soft-off auto-restart delay
constexpr uint32_t WATCHDOG_MS       = 2000;
constexpr uint32_t CAN_HEARTBEAT_MS  = 3000;  // autonomous STATUS broadcast interval

// -- Compile-time pin conflict checks ------------------------------
static_assert(PIN_FAN       != PIN_NEOPIXEL,     "PIN conflict: FAN vs NEOPIXEL");
static_assert(PIN_UV        != PIN_NEOPIXEL,     "PIN conflict: UV vs NEOPIXEL");
static_assert(PIN_FAN       != PIN_UV,           "PIN conflict: FAN vs UV");
static_assert(PIN_LIMIT_SW  != PIN_MAG_HALL,     "PIN conflict: LIMIT_SW vs MAG_HALL");
static_assert(PIN_CAN_TX    != PIN_CAN_RX,       "PIN conflict: CAN_TX vs CAN_RX");
static_assert(PIN_SMPS      != PIN_FAN,          "PIN conflict: SMPS vs FAN");
static_assert(PIN_SMPS      != PIN_UV,           "PIN conflict: SMPS vs UV");
static_assert(PIN_LIGHT_1   != PIN_HEATER_RELAY, "PIN conflict: LIGHT_1 vs HEATER_RELAY");
static_assert(PIN_LED_CAN   != PIN_LED_STATUS,   "PIN conflict: LED_CAN vs LED_STATUS");
static_assert(PIN_EMERGENCY != PIN_POWER_BTN,    "PIN conflict: EMERGENCY vs POWER_BTN");

#endif //POWER_DISTRIBUTION_CAN_BOARD_H
