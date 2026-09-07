# Prebuilt firmware

Built from branch `feature/fan-pwm-control`, commit `d8a569f`.

No toolchain needed to use these — just flash one of them onto the board.

## Flash with BOOTSEL (easiest, no tools required)

1. Hold the **BOOTSEL** button on the Pico and plug it into USB (or press
   BOOTSEL + tap RESET if the board has a separate reset button).
2. It shows up as a USB drive named `RPI-RP2`.
3. Drag `power_distribution_can.uf2` onto that drive.
4. The board reboots automatically running the new firmware.

## Flash with a debug probe (OpenOCD + Picoprobe/CMSIS-DAP)

```
openocd -f interface/cmsis-dap.cfg -c "transport select swd" -f target/rp2040.cfg \
  -c "program power_distribution_can.elf verify reset exit"
```

(Or use this repo's `platform/openocd.cfg` if you have the full source tree.)

## What's what

| File | Use |
|---|---|
| `power_distribution_can.uf2` | Drag-and-drop flashing via BOOTSEL |
| `power_distribution_can.elf` | Flashing/debugging via OpenOCD + GDB (has debug symbols) |

See [`../PROTOCOL.md`](../PROTOCOL.md) in the main repo for the full CAN/serial command reference.
