# Power Distribution CAN

RP2040 firmware for the Power Distribution Board — SMPS, UV lamp, HEPA fan,
work light, heater relay, a 25-pixel NeoPixel status strip, and safety
interlocks, addressable over CAN bus (0x6A5/0x6B5, 500kbps) and USB serial
(115200 8N1).

## Provenance

This is a structural port, not a new implementation. The reference firmware
(protocol, pin map, safety behavior) lives at
`Documents/PlatformIO/Projects/Power_distribution_Board` and is built with
PlatformIO / the earlephilhower Arduino core. This project reimplements the
same behavior 1:1 on bare-metal pico-sdk + CMake, following the structure of
the Lapras control board firmware (`Lapras_Control_Firmware_V1-main`):
- `lib/can` — the same can2040 PIO software-CAN driver, copied from Lapras
- one CMake library per module under `src/`, linked into a single executable
- `inc/board.h` — pin map + CAN protocol constants with `static_assert`
  pin-conflict checks (ported from the reference firmware's `pins.h`)

Unlike Lapras, this board targets the stock Raspberry Pi Pico (`PICO_BOARD
pico`) with the standard boot2 and flash layout — no custom bootloader
carve-out, so the built `.elf`/`.uf2` can be flashed directly with OpenOCD or
by dragging the `.uf2` onto the board in BOOTSEL mode.

## Layout

| Path | Contents |
|---|---|
| `inc/board.h` | Pin map, CAN IDs, output bit masks, timing constants |
| `inc/debounce.h` | Debounce struct + `millis()` helper |
| `src/neopixel/` | `ws2812.pio` + PIO driver + `NeoFSM` status-strip state machine |
| `src/can_comm/` | can2040 wrapper (RX ring buffer) + CAN protocol handler |
| `src/serial_io/` | USB-serial text command handler |
| `src/main.cpp` | Global state, GPIO/ADC/watchdog setup, main loop, `sendAck`/`sendStatus` |

## Build

```
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -S . -B build
cmake --build build
```

## Prebuilt firmware

Ready-to-flash `.uf2`, `.bin` and `.elf` files are in [`release/`](release/)
(and attached to each GitHub Release). See [`release/README.md`](release/README.md)
for checksums and flashing steps.

## Flash

**BOOTSEL (no tools):** hold BOOTSEL while plugging in USB, then drag
`release/power_distribution_can.uf2` (or `build/src/power_distribution_can.uf2`)
onto the `RPI-RP2` drive.

**Debug probe (CMSIS-DAP):**

```
openocd -f platform/openocd.cfg -c "program build/src/power_distribution_can.elf verify reset exit"
```

> A board running this firmware does not enter BOOTSEL on its own — hold the
> BOOTSEL button while connecting USB.

## Test GUI

[`tools/power_distribution_gui.py`](tools/power_distribution_gui.py) is a
Tkinter test panel that drives the board over USB serial (and optionally CAN):
output ON/OFF, fan speed, NeoPixel states/colours, live status LEDs, 24 V rail,
CAN diagnostics, a serial console and a raw CAN sender.

```
pip install pyserial          # required
pip install python-can        # optional, only for the CAN link
python tools/power_distribution_gui.py
```

Pick the board's COM port ("USB Serial Device") and press **Connect**. The
Python install must include Tk (the python.org installer does).

See [`PROTOCOL.md`](PROTOCOL.md) for the full CAN and serial command
reference (function codes, frame layouts, safety interlocks). The
in-firmware `HELP` serial command prints a quick summary.
