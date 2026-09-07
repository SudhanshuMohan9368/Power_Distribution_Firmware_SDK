# Power Distribution Board — Command Protocol

Two independent command interfaces control the same output state
(`smpsOn`, `fanOn`, `uvOn`, `lightOn`, `heaterOn`) and NeoPixel FSM: CAN bus
and USB serial. Both trigger `applyOutputs()` and (mostly) `sendStatus()`
after any change, so state stays consistent regardless of which interface
issued the command.

- CAN: 500 kbps, standard 11-bit IDs, 8-byte frames — RX `0x6A5`, TX `0x6B5`
- Serial: USB CDC, 115200 8N1, newline-terminated ASCII lines (`\r` or `\n`)

Source of truth: [`src/can_comm/can_handler.cpp`](src/can_comm/can_handler.cpp),
[`src/serial_io/serial_io.cpp`](src/serial_io/serial_io.cpp),
[`inc/board.h`](inc/board.h).

## CAN bus

### Framing

- RX ID `CAN_ID_CMD = 0x6A5` (master → board). Any other ID, any extended
  (29-bit) frame, or a frame with `dlc < 2` is ignored by the software
  acceptance filter in `processCan()` — can2040 has no hardware
  filter/mask registers, so every frame reaching the wire is screened here.
- TX ID `CAN_ID_RESP = 0x6B5` (board → master), always `dlc = 8`.
- **TX gate**: the board stays silent on `0x6B5` until it receives its
  first valid `0x6A5` command frame (`canActive` latches true then, for the
  life of the process). `sendAck()` always fires on a valid command
  regardless of this gate; `sendStatus()`'s CAN frame is suppressed until
  the gate opens (its serial/JSON output is not).

### Command frame layout (`0x6A5`, master → board)

| Byte | Name | Meaning |
|---|---|---|
| `d[0]` | `fn` | Function code — see table below |
| `d[1]` | `sub` | Sub-code / parameter, meaning depends on `fn` |
| `d[2]` | `d2` | Extra parameter (present if `dlc > 2`, else 0) |
| `d[3]` | `d3` | Extra parameter (present if `dlc > 3`, else 0) |
| `d[4]` | `d4` | Extra parameter (present if `dlc > 4`, else 0) — only `fn=0x0A` reads this |
| `d[5..6]` | — | Unused |
| `d[7]` | `onoff` | `0x01` = ON, `0x00` = OFF (present if `dlc > 7`, else 0) |

### Function codes (`fn`)

| `fn` | Function | `sub` / `d2` / `d3` / `d4` | `onoff` (`d[7]`) |
|---|---|---|---|
| `0x01` | SMPS / machine power | `d2`: `0x00`=soft off, `0x01`=hard off (only read when `onoff=0x00`) | `0x01`=ON (cancels pending soft-off, blocked if emergency active); `0x00`=OFF |
| `0x02` | Poll sensors | — | `sendAck(0x02, 0, 0)` — reply's `d[6]` sensor byte *is* the poll result |
| `0x03` | Heater relay (GP16, AC supply) | — | `0x01`=ON (blocked if emergency active); `0x00`=OFF |
| `0x04` | Work light (GP17) | — | `0x01`=ON / `0x00`=OFF |
| `0x05` | UV lamp (GP19) | — | `0x01`=ON / `0x00`=OFF |
| `0x06` | HEPA fan (GP18, PWM) | — | `0x01`=ON / `0x00`=OFF — drives at the last speed set via `fn=0x0B` (default 100%) |
| `0x07` | NeoPixel state | `sub`: state code (table below); `d2`: PROGRESS count | — |
| `0x08` | NeoPixel manual colour, all 25px | `sub`=R, `d2`=G, `d3`=B | — |
| `0x09` | Full status request | — | triggers `sendStatus()` |
| `0x0A` | NeoPixel count+colour fill | `sub`=count (0-25), `d2`=R, `d3`=G, `d4`=B — lights the first `count` pixels that colour, rest off | — |
| `0x0B` | HEPA fan PWM speed | `sub`=percent (0-100, clamped) — only takes effect while the fan is ON (`fn=0x06`); orthogonal to on/off | — |
| other | — | — | logged as `unknown fn`, no reply |

Every handled `fn` (except `0x09`, which just calls `sendStatus()`) replies
with an ACK frame; most also follow up with a STATUS frame.

**SMPS soft-off vs hard-off** (`fn=0x01`, `onoff=0x00`):
- `d2=0x00` (default) → soft off: SMPS turns off now, auto-restarts after
  `SOFT_OFF_DELAY_MS` (10 s) unless emergency is active or another command
  cancels it first.
- `d2=0x01` → hard off: stays off until an explicit ON command. Serial `M0`
  and the emergency-stop path always behave as hard off.

**NeoPixel state sub-codes** (`fn=0x07`, byte `sub`):

| `sub` | State |
|---|---|
| `0x50` | INIT |
| `0x51` | PROGRESS (`d2` = 0–25 count) — green only: brighter green fill over a dim green base |
| `0x52` | ERROR |
| `0x53` | COMPLETE |
| `0x54` | IDLE |
| `0x55` | OFF |
| `0x56` | RUNNING |

(`MANUAL`, `RAINBOW`, and `PIXEL_FILL` states exist in the `NeoState` enum
but have no `fn=0x07` sub-code — MANUAL is reached only via `fn=0x08`,
PIXEL_FILL only via `fn=0x0A`, RAINBOW only via the serial `NEO RAINBOW`
command.)

### ACK frame (`0x6B5`, board → master, `sendAck`)

Sent in reply to every handled command except `0x09`.

| Byte | Value |
|---|---|
| `d[0]` | echoed `fn` |
| `d[1]` | echoed `sub` |
| `d[2]` | echoed `d2` |
| `d[3..5]` | `0x00` |
| `d[6]` | live sensor-state byte (bit0=limit, bit1=hall, bit2=emergency) |
| `d[7]` | `0x02` (frame-type tag: ACK) |

### STATUS frame (`0x6B5`, board → master, `sendStatus`)

Sent after most state changes and on `fn=0x09`; suppressed on CAN until
`canActive` is true (the JSON serial line is not).

| Byte | Value |
|---|---|
| `d[0]` | `0xFF` (frame-type tag: STATUS) |
| `d[1]` | output bits — see table below |
| `d[2]` | sensor-state byte (bit0=limit, bit1=hall, bit2=emergency) |
| `d[3]` | `NeoState` enum value (0=IDLE … 8=OFF, 9=PIXEL_FILL) |
| `d[4]` | ADC3 raw reading, high byte |
| `d[5]` | ADC3 raw reading, low byte |
| `d[6]` | `0x00` |
| `d[7]` | `0x01` (frame-type tag: STATUS) |

Output bits (`d[1]`), same masks used by the serial `SET`/`ALL` commands:

| Bit | Mask | Output |
|---|---|---|
| 0 | `0x01` | SMPS (reads back OFF while emergency is active, even if `smpsOn` is logically true) |
| 1 | `0x02` | UV lamp |
| 2 | `0x04` | HEPA fan |
| 3 | `0x08` | Work light |
| 4 | `0x10` | Heater relay (same emergency-masking as SMPS) |
| 5–7 | — | reserved |

Sensor-state byte (`ACK d[6]` and `STATUS d[2]`, identical encoding):

| Bit | Meaning |
|---|---|
| 0 | Limit switch triggered (GP12 active LOW) |
| 1 | Hall sensor detected / door closed (GP14 active LOW) |
| 2 | Emergency stop active (GP21 active LOW) |

## Serial commands (USB CDC, ASCII, newline-terminated)

Case-insensitive. Also printed live by the `HELP` command.

| Command | Effect |
|---|---|
| `HELP` | Print the command summary |
| `STATUS` | Print the JSON status line (see below) |
| `M1` / `M0` | SMPS ON / OFF — serial OFF is **always** hard off (clears `softOffPending`); `M1` blocked if emergency active |
| `UV1` / `UV0` | UV lamp (GP19) ON/OFF |
| `F1` / `F0` | HEPA fan (GP18, PWM) ON/OFF |
| `FAN <0-100>` | HEPA fan PWM speed % -- only takes effect while the fan is ON |
| `L1` / `L0` | Work light (GP17) ON/OFF |
| `H1` / `H0` | Heater relay (GP16) ON/OFF — blocked ON if emergency active |
| `ADC` | Print raw ADC3 reading and computed 24V-rail voltage |
| `LIMIT` | Print limit-switch (GP12) state |
| `HALL` | Print hall-sensor (GP14) state |
| `ALL1` | All outputs ON (SMPS/Heater skipped if emergency active) |
| `ALL0` | All outputs OFF |
| `SET <mask> <val>` | Set only the outputs selected by `mask` to the corresponding bits of `val` (same bit layout as the STATUS `d[1]` table above). Numbers accept decimal, `0x..` hex, or trailing `h` hex (e.g. `SET 0x03 1`, `SET 3 01h`) |
| `NEO IDLE` / `INIT` / `RUNNING` / `COMPLETE` / `ERROR` / `RAINBOW` / `OFF` | Set NeoPixel FSM state |
| `NEO PROGRESS <0-25>` | Set PROGRESS state with fill count |
| `NEO <R> <G> <B>` | Manual solid colour (decimal 0–255 each) |

Unrecognized input prints `ERR: unknown command -- type HELP`.

### STATUS JSON (serial, printed by `STATUS`, `HELP`-adjacent events, and most state changes)

```json
{"smps":0,"uv":0,"fan":0,"fanspd":0,"light":0,"heater":0,"emergency":0,"limit":0,"hall":0,"powerbtn":0,"v24":0.00,"neo":0,"drops":0,"buserr":0,"txfail":0,"txtry":0,"txok":0,"restart_in":7}
```

`restart_in` (seconds until SMPS soft-off auto-restart) is only present
while `softOffPending` is true. `fanspd` is the active PWM duty % (0 when
the fan is off, regardless of what speed was last set).

`drops`/`buserr`/`txfail`/`txtry`/`txok` are free-running diagnostic
counters from [`can_bus.cpp`](src/can_comm/can_bus.cpp), never reset
except on reboot:

| Field | Meaning |
|---|---|
| `drops` | RX ring (16 deep) was full when a valid `CAN_ID_CMD` frame arrived; the ISR discarded it |
| `buserr` | can2040 reported a bit-stuff/CRC/form/ACK error on the wire |
| `txfail` | `can2040_transmit()`'s internal 4-deep TX queue was full when `sendAck`/`sendStatus` tried to send |
| `txtry` | Total transmit attempts -- every `can_bus_transmit()` call, whether it succeeded or not |
| `txok` | Total frames can2040 confirmed actually went out on the wire (`CAN2040_NOTIFY_TX`) -- always `<= txtry`; a growing gap between them means frames are being queued but not landing on the bus |

### Live CAN diagnostics line (serial, unprompted, ~1Hz)

Independent of `STATUS` and not gated behind `ENABLE_DEBUG_LOG` -- fixed
1Hz cost regardless of bus load, printed automatically so the counters
are visible on a plain serial monitor with no other input needed:

```
CANDBG rx_drop=0 bus_err=0 tx_fail=0 tx_try=0 tx_ok=0
```

Same five counters as the STATUS JSON's `drops`/`buserr`/`txfail`/`txtry`/
`txok`, just on their own line every ~1s (`CAN_DIAG_PRINT_MS` in
[`inc/board.h`](inc/board.h)) instead of only on demand. See
`serviceCanDiagPrint()` in [`src/main.cpp`](src/main.cpp).

## Build-time debug tracing

Per-event `CAN:`/`EVENT:` trace lines (state changes, button/sensor
events) are compiled out by default -- see [`inc/debug_log.h`](inc/debug_log.h).
They cost USB IRQ time that competes with can2040's real-time PIO IRQ, so
they're opt-in only, via:

```
cmake -DENABLE_DEBUG_LOG=ON -S . -B build
```

The STATUS JSON line itself (above) is unaffected by this flag -- it's
part of the serial protocol, not debug tracing.

## Safety interlocks (apply regardless of interface)

- **Emergency stop** (GP21 active LOW): forces SMPS and heater relay off
  immediately, cancels any pending soft-off restart, and blocks new ON
  commands for both (CAN and serial) until released.
- **Door / hall interlock** (GP14): UV lamp is force-OFF the instant the
  door opens (hall goes CLEAR), on either interface.
- **Power button** (GP20): start-only, active LOW — turns SMPS ON if it's
  currently off and emergency isn't active; does nothing otherwise (not a
  toggle).
- **Watchdog**: 2 s timeout: `WATCHDOG_MS` in [`inc/board.h`](inc/board.h);
  a hardware reset here re-runs `setup()`, which defaults every output to
  OFF and `softOffPending` to false (hard-off equivalent).
