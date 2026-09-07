#ifndef POWER_DISTRIBUTION_CAN_SERIAL_IO_H
#define POWER_DISTRIBUTION_CAN_SERIAL_IO_H

#include "board.h"
#include "debounce.h"
#include "rgb_states.h"
#include <cstdint>

// -- Shared output state — defined in main.cpp ------------------
extern bool smpsOn;
extern bool fanOn;
extern uint8_t fanSpeed;
extern bool uvOn;
extern bool lightOn;
extern bool heaterOn;

// -- Shared sensor state — defined in main.cpp -------------------
extern Debounce db_emergency;
extern Debounce db_powerbtn;
extern Debounce db_limit;
extern Debounce db_hall;

// -- Soft-off state — defined in main.cpp ------------------------
extern bool softOffPending;  // serial M0 must clear this (serial = always hard off)

// -- Shared NeoPixel FSM — defined in main.cpp -------------------
extern NeoFSM neo;

// -- Shared functions — defined in main.cpp ----------------------
void  applyOutputs();
bool  emergencyActive();
void  sendStatus();
float read24V();

// -- Public API ------------------------------------------------------
void handleSerialLine(char *line);

#endif //POWER_DISTRIBUTION_CAN_SERIAL_IO_H
