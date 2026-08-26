#include "ws2812_driver.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "ws2812.pio.h"
#include <cmath>

Ws2812Strip::Ws2812Strip(PIO pio, uint sm, uint pin, uint count)
    : pio_(pio), sm_(sm), pin_(pin), count_(count) {}

void Ws2812Strip::begin() {
    uint offset = pio_add_program(pio_, &ws2812_program);
    ws2812_program_init(pio_, sm_, offset, pin_, 800000.f, false);

    dmaChan_ = dma_claim_unused_channel(false);  // -1 if none free -- show() falls back
    if (dmaChan_ < 0) return;

    dma_channel_config c = dma_channel_get_default_config(dmaChan_);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(pio_, sm_, true));
    dma_channel_configure(dmaChan_, &c, &pio_->txf[sm_], txBuf_, 0, false);
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
    // Wait for the *previous* show()'s transfer to drain before overwriting
    // txBuf_ -- DMA reads directly from it. In practice this never actually
    // blocks: update() calls show() at most every ~10-20ms, and a full
    // 25-pixel transfer finishes in under 1ms.
    if (dmaChan_ >= 0) dma_channel_wait_for_finish_blocking(dmaChan_);

    uint n = count_ < MAX_PIXELS ? count_ : MAX_PIXELS;
    for (uint i = 0; i < n; i++) {
        uint8_t r = (uint8_t)((uint16_t)r_[i] * brightness_ / 255);
        uint8_t g = (uint8_t)((uint16_t)g_[i] * brightness_ / 255);
        uint8_t b = (uint8_t)((uint16_t)b_[i] * brightness_ / 255);
        // WS2812B wants GRB order on the wire, left-justified in the 32-bit
        // FIFO word (out_shift configured for 24-bit autopull, MSB first).
        uint32_t grb = ((uint32_t)g << 16) | ((uint32_t)r << 8) | (uint32_t)b;
        txBuf_[i] = grb << 8u;
    }

    if (dmaChan_ >= 0) {
        dma_channel_set_read_addr(dmaChan_, txBuf_, false);
        dma_channel_set_trans_count(dmaChan_, n, true);
    } else {
        for (uint i = 0; i < n; i++) pio_sm_put_blocking(pio_, sm_, txBuf_[i]);
    }
}
