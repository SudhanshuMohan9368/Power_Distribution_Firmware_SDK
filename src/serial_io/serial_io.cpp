#include "serial_io.h"
#include "hardware/adc.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static bool startsWith(const char *s, const char *pfx) {
    while (*pfx) {
        if (tolower((unsigned char)*s++) != tolower((unsigned char)*pfx++)) return false;
    }
    return true;
}

static int parseByte(const char *tok, bool &ok) {
    ok = true;
    if (!tok || !*tok) { ok = false; return 0; }
    if (tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X'))
        return (int)strtol(tok, nullptr, 16);
    size_t n = strlen(tok);
    if (n > 1 && (tok[n - 1] == 'h' || tok[n - 1] == 'H')) {
        char tmp[10];
        n = n - 1 < 9 ? n - 1 : 9;
        memcpy(tmp, tok, n);
        tmp[n] = 0;
        return (int)strtol(tmp, nullptr, 16);
    }
    char *endp = nullptr;
    long v = strtol(tok, &endp, 10);
    if (endp && *endp == 0) return (int)v;
    return (int)strtol(tok, nullptr, 16);
}

static void setFromMask(uint8_t mask, uint8_t values) {
    if (mask & BIT_SMPS) {
        if (emergencyActive() && (values & BIT_SMPS)) {
            printf("ERR: emergency active -- SMPS blocked\n");
            smpsOn = false;
        } else {
            smpsOn = (values & BIT_SMPS) != 0;
        }
    }
    if (mask & BIT_UV) uvOn = (values & BIT_UV) != 0;
    if (mask & BIT_FAN) fanOn = (values & BIT_FAN) != 0;
    if (mask & BIT_LIGHT) lightOn = (values & BIT_LIGHT) != 0;
    if (mask & BIT_HEATER) {
        if (emergencyActive() && (values & BIT_HEATER)) {
            printf("ERR: emergency active -- Heater blocked\n");
            heaterOn = false;
        } else {
            heaterOn = (values & BIT_HEATER) != 0;
        }
    }
    applyOutputs();
}

void handleSerialLine(char *line) {
    while (*line == ' ' || *line == '\t') ++line;
    if (*line == 0) return;

    if (startsWith(line, "HELP")) {
        printf("\n=== COMMANDS ===\n");
        printf("STATUS / HELP\n");
        printf("M1/M0           SMPS ON / HARD-OFF (serial is always hard off)\n");
        printf("UV1/UV0         UV lamp (GP19) ON/OFF\n");
        printf("F1/F0           HEPA fan ON/OFF (PWM)\n");
        printf("FAN <0-100>     HEPA fan PWM speed %% (only takes effect while ON)\n");
        printf("L1/L0           Work light (GP17) ON/OFF\n");
        printf("H1/H0           Heater relay (GP16) ON/OFF -- AC supply to heater board\n");
        printf("ADC / LIMIT / HALL\n");
        printf("ALL1 / ALL0  /  SET <mask> <val>\n");
        printf("  mask bits: 0x01=SMPS 0x02=UV 0x04=Fan 0x08=Light 0x10=Heater\n");
        printf("--- NeoPixel ---\n");
        printf("NEO IDLE / INIT / RUNNING / COMPLETE / ERROR / RAINBOW / OFF\n");
        printf("NEO PROGRESS <0-25>\n");
        printf("NEO <R> <G> <B>\n");
        printf("================\n\n");
        return;
    }

    if (startsWith(line, "STATUS")) { sendStatus(); return; }

    // SMPS
    if (toupper((unsigned char)line[0]) == 'M' && (line[1] == '1' || line[1] == '0')) {
        if (line[1] == '1' && emergencyActive()) {
            printf("ERR: emergency active\n");
            return;
        }
        smpsOn = (line[1] == '1');
        if (!smpsOn) softOffPending = false;  // serial OFF = hard off, cancel CAN restart timer
        applyOutputs();
        printf("SMPS %s\n", smpsOn ? "ON" : "OFF");
        sendStatus();
        return;
    }

    // UV lamp (GP19)
    if (startsWith(line, "UV1")) { uvOn = true;  applyOutputs(); printf("UV ON\n");  sendStatus(); return; }
    if (startsWith(line, "UV0")) { uvOn = false; applyOutputs(); printf("UV OFF\n"); sendStatus(); return; }

    // HEPA fan ON/OFF (PWM)
    if (toupper((unsigned char)line[0]) == 'F' && line[1] == '1' && line[2] == 0) {
        fanOn = true;  applyOutputs(); printf("Fan ON (speed=%d%%)\n", fanSpeed);  sendStatus(); return;
    }
    if (toupper((unsigned char)line[0]) == 'F' && line[1] == '0' && line[2] == 0) {
        fanOn = false; applyOutputs(); printf("Fan OFF\n"); sendStatus(); return;
    }

    // HEPA fan PWM speed, 0-100%. Only takes effect while the fan is ON.
    if (startsWith(line, "FAN ")) {
        char *p = line + 4;
        while (*p == ' ') p++;
        bool ok = false;
        int v = parseByte(p, ok);
        if (!ok || v < 0 || v > 100) { printf("ERR: FAN <0-100>\n"); return; }
        fanSpeed = (uint8_t)v;
        applyOutputs();
        printf("Fan speed %d%%\n", fanSpeed);
        sendStatus();
        return;
    }

    // Work light (GP17)
    if (toupper((unsigned char)line[0]) == 'L' && (line[1] == '1' || line[1] == '0') && line[2] == 0) {
        lightOn = (line[1] == '1');
        applyOutputs();
        printf("Light %s\n", lightOn ? "ON" : "OFF");
        sendStatus(); return;
    }

    // Heater relay (GP16) -- AC supply to heater board
    if (toupper((unsigned char)line[0]) == 'H' && (line[1] == '1' || line[1] == '0') && line[2] == 0) {
        if (line[1] == '1' && emergencyActive()) {
            printf("ERR: emergency active -- Heater blocked\n");
            sendStatus(); return;
        }
        heaterOn = (line[1] == '1');
        applyOutputs();
        printf("Heater %s\n", heaterOn ? "ON" : "OFF");
        sendStatus(); return;
    }

    // Sensors / ADC
    if (startsWith(line, "ADC")) {
        adc_select_input(ADC3_CHANNEL);
        uint16_t raw = adc_read();
        float v = read24V();
        printf("ADC raw=%u  V24=%.2fV\n", raw, v);
        return;
    }
    if (startsWith(line, "LIMIT")) {
        printf("Limit GP12: %s\n", gpio_get(PIN_LIMIT_SW) == 0 ? "TRIGGERED" : "OPEN");
        return;
    }
    if (startsWith(line, "HALL")) {
        printf("Hall GP14: %s\n", gpio_get(PIN_MAG_HALL) == 0 ? "DETECTED" : "CLEAR");
        return;
    }

    // ALL1 / ALL0
    if (startsWith(line, "ALL1")) {
        if (!emergencyActive()) { smpsOn = true; heaterOn = true; }
        uvOn = true; lightOn = true; fanOn = true;
        applyOutputs();
        printf(emergencyActive() ? "All ON (SMPS/Heater blocked)\n" : "All outputs ON\n");
        sendStatus(); return;
    }
    if (startsWith(line, "ALL0")) {
        smpsOn = false; uvOn = false; fanOn = false; lightOn = false; heaterOn = false;
        applyOutputs();
        printf("All OFF\n"); sendStatus(); return;
    }

    // SET <mask> <val>
    if (startsWith(line, "SET")) {
        char *ctx = nullptr;
        strtok_r(line, " \t", &ctx);
        char *tM = strtok_r(nullptr, " \t", &ctx);
        char *tV = strtok_r(nullptr, " \t", &ctx);
        bool ok1 = false, ok2 = false;
        int mask = parseByte(tM, ok1);
        int val  = parseByte(tV, ok2);
        if (!ok1 || !ok2) { printf("ERR: SET <mask> <val>\n"); return; }
        setFromMask((uint8_t)mask, (uint8_t)val);
        sendStatus(); return;
    }

    // NEO commands
    if (startsWith(line, "NEO ")) {
        char *rest = line + 4;
        while (*rest == ' ') rest++;

        if (startsWith(rest, "IDLE"))     { neo.setState(NeoState::IDLE);     printf("Neo: IDLE\n");     return; }
        if (startsWith(rest, "INIT"))     { neo.setState(NeoState::INIT);     printf("Neo: INIT\n");     return; }
        if (startsWith(rest, "RUNNING"))  { neo.setState(NeoState::RUNNING);  printf("Neo: RUNNING\n");  return; }
        if (startsWith(rest, "COMPLETE")) { neo.setState(NeoState::COMPLETE); printf("Neo: COMPLETE\n"); return; }
        if (startsWith(rest, "ERROR"))    { neo.setState(NeoState::ERROR_ST); printf("Neo: ERROR\n");    return; }
        if (startsWith(rest, "RAINBOW"))  { neo.setState(NeoState::RAINBOW);  printf("Neo: RAINBOW\n");  return; }
        if (startsWith(rest, "OFF"))      { neo.setState(NeoState::OFF);      printf("Neo: OFF\n");      return; }

        if (startsWith(rest, "PROGRESS")) {
            char *p = rest + 8;
            while (*p == ' ') p++;
            int v = atoi(p);
            if (v < 0) v = 0;
            if (v > NEO_N) v = NEO_N;
            auto n = (uint8_t)v;
            neo.setState(NeoState::PROGRESS, n);
            printf("Neo: PROGRESS %d\n", n);
            return;
        }

        char *ctx = nullptr;
        char *tr = strtok_r(rest, " ", &ctx);
        char *tg = strtok_r(nullptr, " ", &ctx);
        char *tb = strtok_r(nullptr, " ", &ctx);
        if (tr && tg && tb) {
            auto r = (uint8_t)atoi(tr);
            auto g = (uint8_t)atoi(tg);
            auto b = (uint8_t)atoi(tb);
            neo.setManual(r, g, b);
            printf("Neo: manual R=%d G=%d B=%d\n", r, g, b);
        } else {
            printf("ERR: NEO <R> <G> <B> or IDLE/INIT/RUNNING/PROGRESS N/COMPLETE/ERROR/RAINBOW/OFF\n");
        }
        return;
    }

    printf("ERR: unknown command -- type HELP\n");
}
