#include "rgb_states.h"
#include "debounce.h"  // millis()
#include <cmath>

// Smooth sine breathe, 0..period -> minB..maxB..minB
static uint8_t neoBreath(uint32_t phase, uint16_t period, uint8_t minB, uint8_t maxB) {
    float x = (float)(phase % period) / (float)period;
    float s = sinf(x * 3.14159265f);
    return (uint8_t)(minB + s * (float)(maxB - minB));
}

// Standard full-saturation/full-value HSV->RGB sweep (0..65535 -> 6 colour
// regions). RAINBOW is a cosmetic, serial-only debug state, so this is a
// straightforward equivalent rather than a byte-for-byte port of Adafruit's
// ColorHSV() bit-twiddled implementation.
static void hueToRgb(uint16_t hue16, uint8_t &r, uint8_t &g, uint8_t &b) {
    float h = (hue16 / 65536.0f) * 6.0f;
    int i = (int)h;
    float f = h - (float)i;
    auto p = (uint8_t)0;
    auto q = (uint8_t)(255.0f * (1.0f - f));
    auto t = (uint8_t)(255.0f * f);
    switch (i % 6) {
        case 0: r = 255; g = t;   b = p;   break;
        case 1: r = q;   g = 255; b = p;   break;
        case 2: r = p;   g = 255; b = t;   break;
        case 3: r = p;   g = q;   b = 255; break;
        case 4: r = t;   g = p;   b = 255; break;
        default: r = 255; g = p; b = q;    break;
    }
}

void NeoFSM::setState(NeoState s, uint8_t param) {
    state        = s;
    progressN    = param;
    errBlinkOn   = false;
    errToggleMs  = millis();
    lastUpdateMs = millis();
}

void NeoFSM::setManual(uint8_t r, uint8_t g, uint8_t b) {
    state   = NeoState::MANUAL;
    manualR = r; manualG = g; manualB = b;
}

void NeoFSM::setPixelFill(uint8_t count, uint8_t r, uint8_t g, uint8_t b) {
    state     = NeoState::PIXEL_FILL;
    fillCount = count > NEO_N ? NEO_N : count;
    manualR = r; manualG = g; manualB = b;
}

void NeoFSM::fillAll(uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < NEO_N; i++) px.setPixelColor(i, r, g, b);
    px.show();
}

void NeoFSM::update() {
    uint32_t now = millis();

    switch (state) {

        case NeoState::IDLE: {
            if (now - lastUpdateMs >= 20) {
                lastUpdateMs = now;
                uint8_t br = neoBreath(now, 3000, 6, 180);
                uint8_t g = Ws2812Strip::gamma8(br);
                fillAll(g, g, g);
            }
            break;
        }

        case NeoState::INIT: {
            if (now - lastUpdateMs >= 20) {
                lastUpdateMs = now;
                uint8_t br = neoBreath(now, 2000, 20, 255);
                uint8_t r = Ws2812Strip::gamma8(br);
                uint8_t g = Ws2812Strip::gamma8((uint8_t)((uint16_t)br * 140 / 255));
                fillAll(r, g, 0);
            }
            break;
        }

        case NeoState::PROGRESS: {
            if (now - lastUpdateMs >= 20) {
                lastUpdateMs = now;
                uint8_t baseDim = (uint8_t)(50 - (uint16_t)progressN * 35 / NEO_N);
                uint8_t gBr = neoBreath(now, 1200, 160, 255);

                for (int i = 0; i < NEO_N; i++) {
                    if (i < progressN) {
                        uint8_t g = Ws2812Strip::gamma8(gBr);
                        px.setPixelColor(i, 0, g, 0);
                    } else {
                        uint8_t g = Ws2812Strip::gamma8(baseDim);
                        px.setPixelColor(i, 0, g, 0);
                    }
                }
                px.show();
            }
            break;
        }

        case NeoState::RUNNING: {
            if (now - lastUpdateMs >= 20) {
                lastUpdateMs = now;
                uint8_t br = neoBreath(now, 2200, 60, 255);
                uint8_t g = Ws2812Strip::gamma8((uint8_t)((uint16_t)br * 120 / 255));
                uint8_t b = Ws2812Strip::gamma8(br);
                fillAll(0, g, b);
            }
            break;
        }

        case NeoState::COMPLETE: {
            if (now - lastUpdateMs >= 20) {
                lastUpdateMs = now;
                uint8_t br = neoBreath(now, 2800, 40, 220);
                uint8_t g = Ws2812Strip::gamma8(br);
                uint8_t b = Ws2812Strip::gamma8((uint8_t)((uint16_t)br * 30 / 255));
                fillAll(0, g, b);
            }
            break;
        }

        case NeoState::ERROR_ST: {
            if (now - errToggleMs >= 150) {
                errToggleMs = now;
                errBlinkOn = !errBlinkOn;
                if (errBlinkOn) fillAll(Ws2812Strip::gamma8(255), 0, 0);
                else fillAll(0, 0, 0);
            }
            break;
        }

        case NeoState::MANUAL: {
            if (now - lastUpdateMs >= 50) {
                lastUpdateMs = now;
                fillAll(Ws2812Strip::gamma8(manualR), Ws2812Strip::gamma8(manualG), Ws2812Strip::gamma8(manualB));
            }
            break;
        }

        case NeoState::RAINBOW: {
            if (now - lastUpdateMs >= 10) {
                lastUpdateMs = now;
                rainbowHue += 256;
                for (int i = 0; i < NEO_N; i++) {
                    uint16_t h = rainbowHue + (uint16_t)i * (65536 / NEO_N);
                    uint8_t r, g, b;
                    hueToRgb(h, r, g, b);
                    px.setPixelColor(i, Ws2812Strip::gamma8(r), Ws2812Strip::gamma8(g), Ws2812Strip::gamma8(b));
                }
                px.show();
            }
            break;
        }

        case NeoState::OFF: {
            fillAll(0, 0, 0);
            state   = NeoState::MANUAL;
            manualR = manualG = manualB = 0;
            break;
        }

        case NeoState::PIXEL_FILL: {
            if (now - lastUpdateMs >= 50) {
                lastUpdateMs = now;
                uint8_t r = Ws2812Strip::gamma8(manualR);
                uint8_t g = Ws2812Strip::gamma8(manualG);
                uint8_t b = Ws2812Strip::gamma8(manualB);
                for (int i = 0; i < NEO_N; i++) {
                    if (i < fillCount) px.setPixelColor(i, r, g, b);
                    else                px.setPixelColor(i, 0, 0, 0);
                }
                px.show();
            }
            break;
        }
    }
}
