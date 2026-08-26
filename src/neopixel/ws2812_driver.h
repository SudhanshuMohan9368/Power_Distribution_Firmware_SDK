#ifndef POWER_DISTRIBUTION_CAN_WS2812_DRIVER_H
#define POWER_DISTRIBUTION_CAN_WS2812_DRIVER_H

#include "hardware/pio.h"
#include <cstdint>

// Minimal pico-sdk PIO WS2812B driver — replaces Adafruit_NeoPixel (Arduino-
// only) from the reference firmware. Caller is expected to gamma-correct
// colours before calling setPixelColor(), matching the reference firmware's
// call-site pattern of `px.gamma32(px.Color(...))`.
class Ws2812Strip {
public:
    Ws2812Strip(PIO pio, uint sm, uint pin, uint count);

    void begin();
    void setBrightness(uint8_t b) { brightness_ = b; }
    void setPixelColor(uint index, uint8_t r, uint8_t g, uint8_t b);
    void clear();
    void show();

    static uint8_t gamma8(uint8_t v);

private:
    static constexpr uint MAX_PIXELS = 64;

    PIO  pio_;
    uint sm_;
    uint pin_;
    uint count_;
    uint8_t brightness_ = 255;
    uint8_t r_[MAX_PIXELS]{};
    uint8_t g_[MAX_PIXELS]{};
    uint8_t b_[MAX_PIXELS]{};
};

#endif //POWER_DISTRIBUTION_CAN_WS2812_DRIVER_H
