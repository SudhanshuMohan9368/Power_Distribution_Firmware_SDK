#include "ws2812_driver.h"
#include "hardware/clocks.h"
#include "ws2812.pio.h"
#include <cmath>

Ws2812Strip::Ws2812Strip(PIO pio, uint sm, uint pin, uint count)
    : pio_(pio), sm_(sm), pin_(pin), count_(count) {}

void Ws2812Strip::begin() {
    uint offset = pio_add_program(pio_, &ws2812_program);
    ws2812_program_init(pio_, sm_, offset, pin_, 800000.f, false);
}

uint8_t Ws2812Strip::gamma8(uint8_t v) {
    return (uint8_t)(powf((float)v / 255.0f, 2.8f) * 255.0f + 0.5f);
}

void Ws2812Strip::setPixelColor(uint index, uint8_t r, uint8_t g, uint8_t b) {
    if (index >= count_ || index >= MAX_PIXELS) return;
    r_[index] = r;
    g_[index] = g;
    b_[index] = b;
}

void Ws2812Strip::clear() {
    for (uint i = 0; i < count_ && i < MAX_PIXELS; i++) {
        r_[i] = g_[i] = b_[i] = 0;
    }
}

void Ws2812Strip::show() {
    for (uint i = 0; i < count_ && i < MAX_PIXELS; i++) {
        uint8_t r = (uint8_t)((uint16_t)r_[i] * brightness_ / 255);
        uint8_t g = (uint8_t)((uint16_t)g_[i] * brightness_ / 255);
        uint8_t b = (uint8_t)((uint16_t)b_[i] * brightness_ / 255);
        // WS2812B wants GRB order on the wire, left-justified in the 32-bit
        // FIFO word (out_shift configured for 24-bit autopull, MSB first).
        uint32_t grb = ((uint32_t)g << 16) | ((uint32_t)r << 8) | (uint32_t)b;
        pio_sm_put_blocking(pio_, sm_, grb << 8u);
    }
}
