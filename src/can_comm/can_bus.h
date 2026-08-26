#ifndef POWER_DISTRIBUTION_CAN_CAN_BUS_H
#define POWER_DISTRIBUTION_CAN_CAN_BUS_H

#include "can2040.h"
#include "hardware/pio.h"
#include <cstdint>

// Thin can2040 (PIO software CAN) wrapper. can2040 is IRQ/callback driven —
// this module buffers received frames from the callback into a small ring
// so the main loop can poll it the same way the reference firmware polled
// RP2040PIO_CAN's CAN.available()/CAN.read().

void can_bus_init(PIO pio, uint32_t bitrate, uint8_t rx_pin, uint8_t tx_pin);

bool can_bus_available();
can2040_msg can_bus_read();
bool can_bus_transmit(const can2040_msg &msg);

// True if the frame is a standard (non-extended) 11-bit ID frame with the
// given id. can2040 only masks off bits 0-10 in msg.id, so an extended
// (29-bit) frame whose low bits alias a standard ID must be rejected
// explicitly by checking CAN2040_ID_EFF — see can_handler.cpp.
inline bool can_msg_is_standard_id(const can2040_msg &msg, uint16_t id) {
    return !(msg.id & CAN2040_ID_EFF) && (msg.id & 0x7FFu) == id;
}

#endif //POWER_DISTRIBUTION_CAN_CAN_BUS_H
