/*
  RP2040 -- Power Distribution Board firmware
  Ported from the reference PlatformIO/Arduino firmware
  (Documents/PlatformIO/Projects/Power_distribution_Board) onto the bare-
  metal pico-sdk/CMake structure used by the Lapras control board firmware.

  PIN MAP:
    GP2   CAN_TX          GP3   CAN_RX
    GP7   CAN_STBY        GP9   SMPS MOSFET (HIGH=ON)
    GP10  CAN_STANDBY2    GP12  Limit switch (active LOW)
    GP13  NeoPixel 25px   GP14  Hall sensor  (active LOW = door closed)
    GP15  Level shifter EN (HIGH=enabled)
    GP16  Heater relay    GP17  Light 1   (heater relay: AC supply to heater board, CAN/serial-only, default OFF)
    GP18  HEPA Fan PWM    GP19  UV lamp
    GP20  Power button    INPUT active LOW
    GP21  Emergency stop  INPUT active LOW
    GP24  CAN LED         GP25  Heartbeat LED
    GP29  ADC3 (24V)

  CAN PROTOCOL -- RX 0x6A5  TX 0x6B5  500kbps  8-byte frames
  SERIAL -- USB CDC, 115200 baud (fixed by USB, no config needed), newline
  terminated (see HELP command)
*/

#include "board.h"
#include "debounce.h"
#include "debug_log.h"
#include "rgb_states.h"
#include "can_bus.h"
#include "can_handler.h"
#include "serial_io.h"

#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "hardware/watchdog.h"
#include <cstdio>

// -- NeoPixel + state machine ------------------------------------
static Ws2812Strip pixel(pio1, 0, PIN_NEOPIXEL, NEO_N);
NeoFSM neo(pixel);

// -- Output state --------------------------------------------------
bool smpsOn   = false;
bool fanOn    = false;   // HEPA fan (GP18) -- PWM
uint8_t fanSpeed = 100;  // fan PWM duty %, applied only while fanOn (default full speed)
bool uvOn     = false;   // UV lamp (GP19)
bool lightOn  = false;   // work light (GP17)
bool heaterOn = false;   // heater board AC supply relay (GP16) -- default OFF, CAN/serial-only

// -- Debounce instances ---------------------------------------------
Debounce db_emergency{PIN_EMERGENCY, true, true, 0};
Debounce db_powerbtn{PIN_POWER_BTN, true, true, 0};
Debounce db_limit{PIN_LIMIT_SW, true, true, 0};
Debounce db_hall{PIN_MAG_HALL, true, true, 0};

// -- CAN gate -- suppress TX until first 0x6A5 command received
bool canActive = false;

// -- Soft-off -- CAN only; serial / emergency are always hard off
bool     softOffPending   = false;
uint32_t softOffRestartAt = 0;

// -- LED / heartbeat timing -------------------------------------------
static uint32_t lastHeartbeatMs = 0;
static bool     heartbeatState  = false;
static uint32_t canPulseOffAt   = 0;

// -- CAN heartbeat -- autonomous STATUS broadcast, independent of commands --
static uint32_t lastCanHeartbeatMs = 0;

// -- Serial line buffer ------------------------------------------------
static char    serialBuf[96];
static uint8_t serialIdx = 0;

// -- Helpers --------------------------------------------------------

bool emergencyActive() {
    return db_emergency.stableState == 0;  // active LOW
}

// Sensor state byte: bit0=limit, bit1=hall, bit2=emergency
static uint8_t getSensorState() {
    uint8_t s = 0;
    if (db_limit.stableState == 0) s |= (1 << 0);
    if (db_hall.stableState == 0)  s |= (1 << 1);
    if (emergencyActive())         s |= (1 << 2);
    return s;
}

static void setFanPwm(uint8_t percent) {
    uint16_t level = (uint16_t)((uint32_t)(percent > 100 ? 100 : percent) * FAN_PWM_WRAP / 100);
    pwm_set_chan_level(pwm_gpio_to_slice_num(PIN_FAN), pwm_gpio_to_channel(PIN_FAN), level);
}

// Actual duty applied to the fan -- floors to FAN_MIN_DUTY_PCT while ON so a
// low requested speed doesn't silently stall the fan (safe, per the fan's
// locked-rotor protection, but not useful); 0 while OFF.
uint8_t effectiveFanSpeed() {
    if (!fanOn) return 0;
    return fanSpeed < FAN_MIN_DUTY_PCT ? FAN_MIN_DUTY_PCT : fanSpeed;
}

void applyOutputs() {
    bool smpsActual   = smpsOn && !emergencyActive();
    bool heaterActual = heaterOn && !emergencyActive();  // AC heater relay -- emergency stop always wins
    gpio_put(PIN_SMPS, smpsActual);
    setFanPwm(effectiveFanSpeed());
    gpio_put(PIN_UV, uvOn);
    gpio_put(PIN_LIGHT_1, lightOn);
    gpio_put(PIN_HEATER_RELAY, heaterActual);
}

static void forceMachineOff() {
    smpsOn         = false;
    heaterOn       = false;  // AC heater relay -- hard off on emergency, no auto-restart
    softOffPending = false;  // emergency / watchdog reset = hard off, never restart
    applyOutputs();
}

void canActivityPulse() {
    gpio_put(PIN_LED_CAN, true);
    canPulseOffAt = millis() + CAN_PULSE_MS;
}

float read24V() {
    adc_select_input(ADC3_CHANNEL);
    uint16_t raw = adc_read();
    return (raw / 4095.0f * 3.3f) * (R2_OHMS + R3_OHMS) / R3_OHMS;
}

// ACK: echo fn/sub/d2, append live sensor_state, d[7]=0x02.
void sendAck(uint8_t fn, uint8_t sub, uint8_t d2) {
    can2040_msg m{};
    m.id  = CAN_ID_RESP;
    m.dlc = 8;
    m.data[0] = fn;
    m.data[1] = sub;
    m.data[2] = d2;
    m.data[3] = 0x00;
    m.data[4] = 0x00;
    m.data[5] = 0x00;
    m.data[6] = getSensorState();
    m.data[7] = 0x02;
    can_bus_transmit(m);
    canActivityPulse();
}

void sendStatus() {
    uint8_t bits = 0;
    if (smpsOn && !emergencyActive()) bits |= BIT_SMPS;
    if (uvOn)    bits |= BIT_UV;
    if (fanOn)   bits |= BIT_FAN;
    if (lightOn) bits |= BIT_LIGHT;
    if (heaterOn && !emergencyActive()) bits |= BIT_HEATER;

    adc_select_input(ADC3_CHANNEL);
    uint16_t raw = adc_read();

    if (canActive) {
        can2040_msg m{};
        m.id  = CAN_ID_RESP;
        m.dlc = 8;
        m.data[0] = 0xFF;
        m.data[1] = bits;
        m.data[2] = getSensorState();
        m.data[3] = (uint8_t)neo.state;
        m.data[4] = (raw >> 8) & 0xFF;
        m.data[5] = raw & 0xFF;
        m.data[6] = 0x00;
        m.data[7] = 0x01;
        can_bus_transmit(m);
        canActivityPulse();
    }

    printf("{\"smps\":%d,\"uv\":%d,\"fan\":%d,\"fanspd\":%d,\"light\":%d,\"heater\":%d,"
           "\"emergency\":%d,\"limit\":%d,\"hall\":%d,\"powerbtn\":%d,"
           "\"v24\":%.2f,\"neo\":%d,\"drops\":%lu,\"buserr\":%lu,\"txfail\":%lu,"
           "\"txtry\":%lu,\"txok\":%lu",
           smpsOn && !emergencyActive() ? 1 : 0,
           uvOn ? 1 : 0,
           fanOn ? 1 : 0,
           effectiveFanSpeed(),
           lightOn ? 1 : 0,
           heaterOn && !emergencyActive() ? 1 : 0,
           emergencyActive() ? 1 : 0,
           db_limit.stableState == 0 ? 1 : 0,
           db_hall.stableState == 0 ? 1 : 0,
           db_powerbtn.stableState == 0 ? 1 : 0,
           (double)read24V(),
           (int)neo.state,
           (unsigned long)can_bus_rx_dropped(),
           (unsigned long)can_bus_bus_errors(),
           (unsigned long)can_bus_tx_failed(),
           (unsigned long)can_bus_tx_attempted(),
           (unsigned long)can_bus_tx_sent());

    if (softOffPending) {
        auto t = (int32_t)(softOffRestartAt - millis());
        printf(",\"restart_in\":%d", t > 0 ? t / 1000 : 0);
    }
    printf("}\n");
}

static void serviceHeartbeatAndCanLed() {
    if (millis() - lastHeartbeatMs >= HEARTBEAT_MS) {
        lastHeartbeatMs = millis();
        heartbeatState  = !heartbeatState;
        gpio_put(PIN_LED_STATUS, heartbeatState);
    }
    if (canPulseOffAt && (int32_t)(millis() - canPulseOffAt) >= 0) {
        gpio_put(PIN_LED_CAN, false);
        canPulseOffAt = 0;
    }
}

// Lets the master detect a live board even before it has sent any command --
// opens the CAN TX gate itself instead of waiting on a 0x6A5 command, since
// that's the whole point of a heartbeat. sendStatus() below still checks
// canActive, so it must be set here first or the frame is silently skipped.
static void serviceCanHeartbeat() {
    if (millis() - lastCanHeartbeatMs >= CAN_HEARTBEAT_MS) {
        lastCanHeartbeatMs = millis();
        if (!canActive) {
            canActive = true;
            DLOG("CAN: heartbeat -- TX now active\n");
        }
        sendStatus();
    }
}

// Serial-only packet diagnostics -- no CAN traffic, doesn't touch canActive,
// just a fixed-rate printf so the counters are visible on a plain serial
// monitor without needing the debug probe. Fixed 1Hz cost regardless of bus
// load, unlike the per-event CAN:/EVENT: traces gated behind ENABLE_DEBUG_LOG.
static uint32_t lastCanDiagPrintMs = 0;
static void serviceCanDiagPrint() {
    if (millis() - lastCanDiagPrintMs >= CAN_DIAG_PRINT_MS) {
        lastCanDiagPrintMs = millis();
        printf("CANDBG rx_drop=%lu bus_err=%lu tx_fail=%lu tx_try=%lu tx_ok=%lu\n",
               (unsigned long)can_bus_rx_dropped(),
               (unsigned long)can_bus_bus_errors(),
               (unsigned long)can_bus_tx_failed(),
               (unsigned long)can_bus_tx_attempted(),
               (unsigned long)can_bus_tx_sent());
    }
}

static void initGpioOutput(uint8_t pin, bool initial) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, initial);
}

static void initGpioInput(uint8_t pin, bool pullup) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    if (pullup) gpio_pull_up(pin);
}

// -- Setup ------------------------------------------------------------
static void setup() {
    stdio_init_all();

    // Keep both CAN transceivers in standby (silent, bus not driven) until
    // can_bus_init() below has configured GP2 as a PIO-driven recessive-idle
    // output. Enabling them any earlier leaves TXD floating while the
    // transceiver is live, which can drive the bus dominant and corrupt
    // arbitration for every other node -- do not reorder this.
    initGpioOutput(PIN_CAN_STBY, true);
    initGpioOutput(PIN_CAN_STBY2, true);
    initGpioOutput(PIN_SMPS, false);
    initGpioOutput(PIN_UV, false);
    initGpioOutput(PIN_LIGHT_1, false);
    initGpioOutput(PIN_HEATER_RELAY, false);  // default OFF until CAN/serial command
    initGpioOutput(PIN_LEVEL_SHIFTER_EN, true);  // must be HIGH to enable the level-shifter IC
    initGpioOutput(PIN_LED_CAN, false);
    initGpioOutput(PIN_LED_STATUS, true);

    // HEPA fan speed -- 3-wire fan (red=24V, black=GND, yellow=PWM on this
    // pin), 25kHz duty cycle. Starts at 0% (fanOn=false); applyOutputs()
    // sets the real level once something turns it on.
    gpio_set_function(PIN_FAN, GPIO_FUNC_PWM);
    {
        uint fanSlice = pwm_gpio_to_slice_num(PIN_FAN);
        pwm_set_wrap(fanSlice, FAN_PWM_WRAP);
        pwm_set_chan_level(fanSlice, pwm_gpio_to_channel(PIN_FAN), 0);
        pwm_set_enabled(fanSlice, true);
    }

    initGpioInput(PIN_EMERGENCY, true);
    initGpioInput(PIN_POWER_BTN, true);
    initGpioInput(PIN_LIMIT_SW, false);
    initGpioInput(PIN_MAG_HALL, true);  // open-drain output needs pull-up to release

    // NeoPixel init before CAN -- claims its own PIO block (pio1) first.
    pixel.begin();
    pixel.setBrightness(200);
    pixel.clear();
    pixel.show();
    neo.setState(NeoState::IDLE);

    adc_init();
    adc_gpio_init(PIN_ADC3);

    sleep_ms(1500);
    printf("POWER DISTRIBUTION BOARD FW -- Type HELP\n");

    can_bus_init(pio0, CAN_BITRATE, PIN_CAN_RX, PIN_CAN_TX);
    printf("CAN OK @ 500k  RX=0x6A5  TX=0x6B5\n");

    // GP2 is now actively driven recessive-idle by the PIO CAN core --
    // safe to bring both transceivers out of standby.
    gpio_put(PIN_CAN_STBY, false);
    gpio_put(PIN_CAN_STBY2, false);

    lastHeartbeatMs     = millis();
    lastCanHeartbeatMs  = millis();
    lastCanDiagPrintMs  = millis();

    // Watchdog -- resets the board if the main loop stalls for > 2s.
    watchdog_enable(WATCHDOG_MS, true);
}

// -- Loop -----------------------------------------------------------
[[noreturn]] static void loop() {
    while (true) {
        watchdog_update();  // must be first -- unconditional

        // 1. Debounce all inputs
        bool emgChanged  = debounceUpdate(db_emergency);
        bool btnChanged  = debounceUpdate(db_powerbtn);
        bool limChanged  = debounceUpdate(db_limit);
        bool hallChanged = debounceUpdate(db_hall);

        // 2. CAN soft-off auto-restart timer
        if (softOffPending && !emergencyActive() &&
            (int32_t)(millis() - softOffRestartAt) >= 0) {
            softOffPending = false;
            smpsOn         = true;
            applyOutputs();
            DLOG("EVENT: soft-off timer -> SMPS ON (auto-restart)\n");
            if (canActive) sendStatus();
        }

        // 3. Emergency stop
        if (emgChanged) {
            if (emergencyActive()) {
                DLOG("EVENT: EMERGENCY pressed -> SMPS forced OFF\n");
                forceMachineOff();
                sendStatus();
            } else {
                DLOG("EVENT: emergency released -> power button enabled\n");
                sendStatus();
            }
        }

        // 4. Power button -- START ONLY, not a toggle
        if (btnChanged && db_powerbtn.stableState == 0) {
            if (emergencyActive()) {
                DLOG("EVENT: power ignored -> emergency active\n");
            } else if (!smpsOn) {
                smpsOn         = true;
                softOffPending = false;  // manual start cancels any pending soft-off timer
                applyOutputs();
                DLOG("EVENT: power button -> SMPS ON (start)\n");
                sendStatus();
            } else {
                DLOG("EVENT: power button ignored -> SMPS already ON\n");
            }
        }

        // 5. Limit switch event
        if (limChanged) {
            DLOG("EVENT: limit GP12 %s\n", db_limit.stableState == 0 ? "TRIGGERED" : "OPEN");
            sendStatus();
        }

        // 6. Hall sensor / door interlock -- cut UV (GP19) when door opens
        if (hallChanged) {
            if (db_hall.stableState != 0) {
                // Magnet gone = door open -- UV safety interlock
                uvOn = false;
                applyOutputs();
                DLOG("EVENT: hall GP14 CLEAR (door open) -> UV OFF\n");
            } else {
                DLOG("EVENT: hall GP14 DETECTED (door closed)\n");
            }
            sendStatus();
        }

        // 7. CAN receive -- ID 0x6A5 command frames
        processCan();

        // 8. LED blink + CAN-activity LED pulse timeout (~1s)
        serviceHeartbeatAndCanLed();

        // 9. Autonomous CAN STATUS heartbeat -- disabled
        // serviceCanHeartbeat();

        // 9b. Serial-only CAN packet diagnostics (~1Hz) -- see PROTOCOL.md
        serviceCanDiagPrint();

        // 10. NeoPixel state machine -- always called, handles its own timing
        neo.update();

        // 11. Serial command reader -- non-blocking
        int c;
        while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
            if (c == '\r' || c == '\n') {
                if (serialIdx > 0) {
                    serialBuf[serialIdx] = 0;
                    handleSerialLine(serialBuf);
                    serialIdx = 0;
                }
            } else if (c >= 32 && c < 127) {
                if (serialIdx < sizeof(serialBuf) - 1)
                    serialBuf[serialIdx++] = (char)c;
            }
        }
    }
}

int main() {
    setup();
    loop();
}
