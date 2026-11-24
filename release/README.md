# Prebuilt firmware — v1.1.0

Built from branch `feature/fan-pwm-control` (includes HEPA fan PWM speed
control with a 25 % minimum duty floor, autonomous CAN heartbeat disabled).

No toolchain needed — just flash one of these files onto the board.

| File | Use |
|---|---|
| `power_distribution_can.uf2` | Drag-and-drop flashing via BOOTSEL (easiest) |
| `power_distribution_can.bin` | Raw flash image, load at `0x10000000` (picotool / custom programmers) |
| `power_distribution_can.elf` | OpenOCD + GDB flashing/debugging (has debug symbols) |

SHA-256:

```
0871001e020bd484e8efee1c998117fcd64ff3ca52f61ebaaa8134165cfc3018  power_distribution_can.uf2
a8c2939b4f29e73741a8493e6e5c63af06fbc450d8b79d9060543ba6527dd0eb  power_distribution_can.bin
dfa8baec780e8d28437cf87a8199dfa0fb0351199f67d414718303b6e7d84482  power_distribution_can.elf
```

## Flash with BOOTSEL (no tools required)

1. Unplug the board. Hold the **BOOTSEL** button and plug it into USB.
2. It appears as a USB drive named `RPI-RP2`.
3. Drag `power_distribution_can.uf2` onto that drive.
4. The board reboots automatically running the new firmware.

> A board already running this firmware does not reboot into BOOTSEL by
> itself — always hold BOOTSEL while plugging in.

## Flash with a debug probe (OpenOCD + CMSIS-DAP / Picoprobe)

```
openocd -f interface/cmsis-dap.cfg -c "transport select swd" -c "adapter speed 5000" \
  -f target/rp2040.cfg -c "program power_distribution_can.elf verify reset exit"
```

Or, from the full source tree: `platform/openocd.cfg`.

## Flash the .bin with picotool

```
picotool load -x power_distribution_can.bin --offset 0x10000000
```

## Test it

Use the bundled test GUI: [`../tools/power_distribution_gui.py`](../tools/power_distribution_gui.py)
(see the main README). Full command reference: [`../PROTOCOL.md`](../PROTOCOL.md).
