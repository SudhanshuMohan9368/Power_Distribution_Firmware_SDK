#ifndef POWER_DISTRIBUTION_CAN_DEBOUNCE_H
#define POWER_DISTRIBUTION_CAN_DEBOUNCE_H

#include "pico/stdlib.h"
#include "board.h"

struct Debounce {
    uint8_t  pin;
    bool     stableState;
    bool     lastRead;
    uint32_t lastChangeMs;
};

inline uint32_t millis() {
    return to_ms_since_boot(get_absolute_time());
}

inline bool debounceUpdate(Debounce &db) {
    bool raw = gpio_get(db.pin);
    if (raw != db.lastRead) {
        db.lastRead     = raw;
        db.lastChangeMs = millis();
    }
    if ((millis() - db.lastChangeMs) >= DEBOUNCE_MS &&
        db.stableState != db.lastRead) {
        db.stableState = db.lastRead;
        return true;
    }
    return false;
}

#endif //POWER_DISTRIBUTION_CAN_DEBOUNCE_H
