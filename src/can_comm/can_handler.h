#ifndef POWER_DISTRIBUTION_CAN_CAN_HANDLER_H
#define POWER_DISTRIBUTION_CAN_CAN_HANDLER_H

#include "board.h"
#include "rgb_states.h"
#include <cstdint>

// -- Shared output state — defined in main.cpp ------------------
extern bool smpsOn;
extern bool fanOn;       // gates the fan PWM output on/off
extern uint8_t fanSpeed; // PWM duty % (0-100), applied only while fanOn
extern bool uvOn;
extern bool lightOn;   // work light (GP17)
extern bool heaterOn;  // heater relay (GP16), default OFF

// -- CAN gate — defined in main.cpp ------------------------------
extern bool canActive;

// -- Soft-off state — defined in main.cpp ------------------------
extern bool     softOffPending;    // true = SMPS turned off via CAN soft-off
extern uint32_t softOffRestartAt;  // millis() target to restart SMPS

// -- Shared NeoPixel FSM — defined in main.cpp -------------------
extern NeoFSM neo;

// -- Shared functions — defined in main.cpp ----------------------
void applyOutputs();
bool emergencyActive();
void canActivityPulse();
void sendStatus();

// ACK: echoes fn/sub/d2, appends live sensor_state, d[7]=0x02 always.
void sendAck(uint8_t fn, uint8_t sub, uint8_t d2 = 0);

// -- Public API ----------------------------------------------------
void processCan();

#endif //POWER_DISTRIBUTION_CAN_CAN_HANDLER_H
