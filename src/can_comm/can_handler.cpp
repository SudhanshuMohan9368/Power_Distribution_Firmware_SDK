#include "can_handler.h"
#include "can_bus.h"
#include "debounce.h"  // millis()
#include "debug_log.h"
#include "pico/stdlib.h"

void processCan() {
    while (can_bus_available()) {
        can2040_msg msg = can_bus_read();

        // Backstop -- can_bus.cpp's ISR callback already filters to standard-
        // frame CAN_ID_CMD with dlc >= 2 before queuing, so this should never
        // trip. Kept here in case that filter is ever relaxed.
        if (!can_msg_is_standard_id(msg, CAN_ID_CMD) || msg.dlc < 2) continue;

        canActivityPulse();
        if (!canActive) {
            canActive = true;
            DLOG("CAN: first 0x6A5 command -- TX now active\n");
        }

        uint8_t fn    = msg.data[0];
        uint8_t sub   = msg.data[1];
        uint8_t d2    = (msg.dlc > 2) ? msg.data[2] : 0;
        uint8_t d3    = (msg.dlc > 3) ? msg.data[3] : 0;
        uint8_t d4    = (msg.dlc > 4) ? msg.data[4] : 0;
        uint8_t onoff = (msg.dlc > 7) ? msg.data[7] : 0;   // 0x01=ON 0x00=OFF

        switch (fn) {

            case 0x01:  // SMPS / machine power
                // d[7]=0x01              -> ON  (cancels any pending soft-off)
                // d[7]=0x00, d[2]=0x00   -> SOFT OFF (auto-restart in 10s) [default]
                // d[7]=0x00, d[2]=0x01   -> HARD OFF (stays off until explicit ON)
                if (onoff == 0x01) {
                    if (emergencyActive()) {
                        DLOG("CAN: SMPS blocked -- emergency active\n");
                        sendAck(fn, sub);
                    } else {
                        smpsOn         = true;
                        softOffPending = false;
                        applyOutputs();
                        DLOG("CAN: SMPS ON\n");
                        sendAck(fn, sub);
                        sendStatus();
                    }
                } else {
                    smpsOn = false;
                    applyOutputs();
                    if (d2 == 0x01) {
                        softOffPending = false;
                        DLOG("CAN: SMPS HARD OFF (permanent)\n");
                    } else {
                        softOffPending   = true;
                        softOffRestartAt = millis() + SOFT_OFF_DELAY_MS;
                        DLOG("CAN: SMPS SOFT OFF (restart in 10s)\n");
                    }
                    sendAck(fn, sub);
                    sendStatus();
                }
                break;

            case 0x05:  // UV lamp (GP19)
                uvOn = (onoff == 0x01);
                applyOutputs();
                DLOG("CAN: UV %s\n", uvOn ? "ON" : "OFF");
                sendAck(fn, sub);
                sendStatus();
                break;

            case 0x06:  // HEPA fan (GP18) -- PWM  d[7]=0x01=ON 0x00=OFF
                fanOn = (onoff == 0x01);
                applyOutputs();
                DLOG("CAN: Fan %s (speed=%d%%)\n", fanOn ? "ON" : "OFF", fanSpeed);
                sendAck(fn, sub);
                sendStatus();
                break;

            case 0x0B:  // HEPA fan PWM speed -- d[1]=percent (0-100), applied only while fanOn
                fanSpeed = sub > 100 ? 100 : sub;
                applyOutputs();
                DLOG("CAN: Fan speed=%d%%\n", fanSpeed);
                sendAck(fn, sub);
                sendStatus();
                break;

            case 0x04:  // Work light (GP17)
                lightOn = (onoff == 0x01);
                applyOutputs();
                DLOG("CAN: Light %s\n", lightOn ? "ON" : "OFF");
                sendAck(fn, sub);
                sendStatus();
                break;

            case 0x03:  // Heater relay (GP16) -- AC supply to heater board, default OFF, CAN-only
                if (onoff == 0x01) {
                    if (emergencyActive()) {
                        DLOG("CAN: Heater blocked -- emergency active\n");
                        sendAck(fn, sub);
                    } else {
                        heaterOn = true;
                        applyOutputs();
                        DLOG("CAN: Heater relay ON\n");
                        sendAck(fn, sub);
                        sendStatus();
                    }
                } else {
                    heaterOn = false;
                    applyOutputs();
                    DLOG("CAN: Heater relay OFF\n");
                    sendAck(fn, sub);
                    sendStatus();
                }
                break;

            case 0x07:  // NeoPixel state -- d[1]=state code  d[2]=count (PROGRESS)
                switch (sub) {
                    case 0x50: neo.setState(NeoState::INIT);          break;
                    case 0x51: neo.setState(NeoState::PROGRESS, d2);  break;
                    case 0x52: neo.setState(NeoState::ERROR_ST);      break;
                    case 0x53: neo.setState(NeoState::COMPLETE);      break;
                    case 0x54: neo.setState(NeoState::IDLE);          break;
                    case 0x55: neo.setState(NeoState::OFF);           break;
                    case 0x56: neo.setState(NeoState::RUNNING);       break;
                    default: break;
                }
                DLOG("CAN: Neo state=0x%02x\n", sub);
                sendAck(fn, sub, d2);
                break;

            case 0x08:  // NeoPixel colour -- d[1]=R  d[2]=G  d[3]=B
                neo.setManual(sub, d2, d3);
                DLOG("CAN: Neo R=%d G=%d B=%d\n", sub, d2, d3);
                sendAck(fn, sub, d2);
                break;

            case 0x0A:  // NeoPixel count+colour fill -- d[1]=count d[2]=R d[3]=G d[4]=B
                neo.setPixelFill(sub, d2, d3, d4);
                DLOG("CAN: Neo fill count=%d R=%d G=%d B=%d\n", sub, d2, d3, d4);
                sendAck(fn, sub, d2);
                break;

            case 0x02:  // Poll sensors -- ACK frame == sensor poll response format
                sendAck(fn, 0, 0);
                break;

            case 0x09:  // Full status request
                sendStatus();
                break;

            default:
                DLOG("CAN: unknown fn=0x%02x\n", fn);
                break;
        }
    }
}
