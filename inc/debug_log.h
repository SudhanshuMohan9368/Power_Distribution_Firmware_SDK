#ifndef POWER_DISTRIBUTION_CAN_DEBUG_LOG_H
#define POWER_DISTRIBUTION_CAN_DEBUG_LOG_H

#include <cstdio>

// Verbose per-event tracing on the CAN/input-event hot path -- off by
// default. Every printf() here goes out over USB CDC and competes with
// can2040's PIO IRQ for USB IRQ time; on a live bus this is pure overhead,
// not protocol (see PROTOCOL.md -- the STATUS JSON line is the actual
// serial protocol output and is never gated behind this). Build with
// -DENABLE_DEBUG_LOG=1 (or the CMake option of the same name) to bring
// it back for bench debugging.
#ifndef ENABLE_DEBUG_LOG
#define ENABLE_DEBUG_LOG 0
#endif

#if ENABLE_DEBUG_LOG
#define DLOG(...) printf(__VA_ARGS__)
#else
#define DLOG(...) ((void)0)
#endif

#endif //POWER_DISTRIBUTION_CAN_DEBUG_LOG_H
