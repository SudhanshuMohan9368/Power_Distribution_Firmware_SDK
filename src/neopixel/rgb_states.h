#ifndef POWER_DISTRIBUTION_CAN_RGB_STATES_H
#define POWER_DISTRIBUTION_CAN_RGB_STATES_H

// Ported from the reference firmware's include/rgb_states.h (Adafruit_
// NeoPixel + Arduino millis()) onto the pico-sdk Ws2812Strip driver.
//
// STATES
// ------
//  IDLE      White breathe, all 25px. Default at boot.
//  INIT      Amber/warm-yellow breathe, all 25px.
//  PROGRESS  Green fills left->right over dim amber base.
//  RUNNING   Solid blue, gentle breathe, all 25px.
//  COMPLETE  Solid green, gentle breathe, all 25px.
//  ERROR     Hard red blink, 150ms on/off, all 25px.
//  MANUAL    Fixed colour (NEO R G B / CAN 0x08).
//  RAINBOW   Hue sweep across the strip.
//  OFF       All pixels off.
//
// CAN d[0]=0x07 sub-codes:
//   0x50=INIT 0x51=PROGRESS(d2=count) 0x52=ERROR 0x53=COMPLETE
//   0x54=IDLE 0x55=OFF 0x56=RUNNING

#include "ws2812_driver.h"
#include <cstdint>

#define NEO_N 25

enum class NeoState : uint8_t {
    IDLE     = 0,
    INIT     = 1,
    PROGRESS = 2,
    RUNNING  = 3,
    COMPLETE = 4,
    ERROR_ST = 5,
    MANUAL   = 6,
    RAINBOW  = 7,
    OFF      = 8
};

class NeoFSM {
public:
    explicit NeoFSM(Ws2812Strip &strip) : px(strip) {}

    Ws2812Strip &px;

    NeoState state        = NeoState::IDLE;
    uint8_t  progressN    = 0;
    uint8_t  manualR = 0, manualG = 0, manualB = 0;
    uint16_t rainbowHue   = 0;
    uint32_t lastUpdateMs = 0;

    bool     errBlinkOn  = false;
    uint32_t errToggleMs = 0;

    void setState(NeoState s, uint8_t param = 0);
    void setManual(uint8_t r, uint8_t g, uint8_t b);
    void fillAll(uint8_t r, uint8_t g, uint8_t b);

    // Call every loop() iteration — non-blocking.
    void update();
};

#endif //POWER_DISTRIBUTION_CAN_RGB_STATES_H
