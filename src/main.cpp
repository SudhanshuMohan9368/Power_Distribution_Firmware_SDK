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
    GP16  Heater relay    GP17  Light 1   (heater relay: AC supply to heater board, CAN/serial-only, default OFF)
    GP18  HEPA Fan        GP19  UV lamp
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
#include "rgb_states.h"
#include "can_bus.h"
#include "can_handler.h"
#include "serial_io.h"

#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/watchdog.h"
#include <cstdio>

// -- NeoPixel + state machine ------------------------------------
static Ws2812Strip pixel(pio1, 0, PIN_NEOPIXEL, NEO_N);
NeoFSM neo(pixel);

// -- Output state --------------------------------------------------
bool smpsOn   = false;
bool fanOn    = false;   // HEPA fan (GP18) -- 12V on/off, no PWM
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

void applyOutputs() {
    bool smpsActual   = smpsOn && !emergencyActive();
    bool heaterActual = heaterOn && !emergencyActive();  // AC heater relay -- emergency stop always wins
    gpio_put(PIN_SMPS, smpsActual);
    gpio_put(PIN_FAN, fanOn);
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

    printf("{\"smps\":%d,\"uv\":%d,\"fan\":%d,\"light\":%d,\"heater\":%d,"
           "\"emergency\":%d,\"limit\":%d,\"hall\":%d,\"powerbtn\":%d,"
           "\"v24\":%.2f,\"neo\":%d",
           smpsOn && !emergencyActive() ? 1 : 0,
           uvOn ? 1 : 0,
           fanOn ? 1 : 0,
           lightOn ? 1 : 0,
           heaterOn && !emergencyActive() ? 1 : 0,
           emergencyActive() ? 1 : 0,
           db_limit.stableState == 0 ? 1 : 0,
           db_hall.stableState == 0 ? 1 : 0,
           db_powerbtn.stableState == 0 ? 1 : 0,
           (double)read24V(),
           (int)neo.state);

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
            printf("CAN: heartbeat -- TX now active\n");
        }
        sendStatus();
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
    initGpioOutput(PIN_FAN, false);
    initGpioOutput(PIN_UV, false);
    initGpioOutput(PIN_LIGHT_1, false);
    initGpioOutput(PIN_HEATER_RELAY, false);  // default OFF until CAN/serial command
    initGpioOutput(PIN_LED_CAN, false);
    initGpioOutput(PIN_LED_STATUS, true);

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

    lastHeartbeatMs    = millis();
    lastCanHeartbeatMs = millis();

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
            printf("EVENT: soft-off timer -> SMPS ON (auto-restart)\n");
            if (canActive) sendStatus();
        }

        // 3. Emergency stop
        if (emgChanged) {
            if (emergencyActive()) {
                printf("EVENT: EMERGENCY pressed -> SMPS forced OFF\n");
                forceMachineOff();
                sendStatus();
            } else {
                printf("EVENT: emergency released -> power button enabled\n");
                sendStatus();
            }
        }

        // 4. Power button -- START ONLY, not a toggle
        if (btnChanged && db_powerbtn.stableState == 0) {
            if (emergencyActive()) {
                printf("EVENT: power ignored -> emergency active\n");
            } else if (!smpsOn) {
                smpsOn         = true;
                softOffPending = false;  // manual start cancels any pending soft-off timer
                applyOutputs();
                printf("EVENT: power button -> SMPS ON (start)\n");
                sendStatus();
            } else {
                printf("EVENT: power button ignored -> SMPS already ON\n");
            }
        }

        // 5. Limit switch event
        if (limChanged) {
            printf("EVENT: limit GP12 %s\n", db_limit.stableState == 0 ? "TRIGGERED" : "OPEN");
            sendStatus();
        }

        // 6. Hall sensor / door interlock -- cut UV (GP19) when door opens
        if (hallChanged) {
            if (db_hall.stableState != 0) {
                // Magnet gone = door open -- UV safety interlock
                uvOn = false;
                applyOutputs();
                printf("EVENT: hall GP14 CLEAR (door open) -> UV OFF\n");
            } else {
                printf("EVENT: hall GP14 DETECTED (door closed)\n");
            }
            sendStatus();
        }

        // 7. CAN receive -- ID 0x6A5 command frames
        processCan();

        // 8. LED blink + CAN-activity LED pulse timeout (~1s)
        serviceHeartbeatAndCanLed();

        // 9. Autonomous CAN STATUS heartbeat (~3s), independent of commands
        serviceCanHeartbeat();

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
