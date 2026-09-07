#include "can_bus.h"
#include "board.h"
#include <RP2040.h>
#include <core_cm0plus.h>
#include "hardware/clocks.h"
#include "hardware/irq.h"

static can2040 cbus{};
static PIO can_pio;

static constexpr uint32_t RX_QUEUE_LEN = 16;
static volatile can2040_msg rx_queue[RX_QUEUE_LEN];
static volatile uint32_t rx_head = 0;  // next slot to write (IRQ context)
static volatile uint32_t rx_tail = 0;  // next slot to read (main loop)

// Stats -- so "is it dropping frames / seeing bus errors" is a number to
// read off STATUS instead of a guess. Single-writer-per-counter (IRQ owns
// rx_dropped/bus_errors, mainline owns tx_failed), so plain volatile is
// enough on this single-core target -- no atomics needed.
static volatile uint32_t rx_dropped_count = 0;
static volatile uint32_t bus_error_count  = 0;
static volatile uint32_t tx_failed_count  = 0;
static volatile uint32_t tx_attempt_count = 0;  // every can_bus_transmit() call, success or fail
static volatile uint32_t tx_sent_count    = 0;  // can2040 confirmed the frame actually went out

static void can2040_cb(struct can2040 *cd, uint32_t notify, struct can2040_msg *msg) {
    if (notify & CAN2040_NOTIFY_ERROR) { bus_error_count++; return; }
    if (notify == CAN2040_NOTIFY_TX) { tx_sent_count++; return; }
    if (notify != CAN2040_NOTIFY_RX) return;

    // Filter here, not in processCan() -- every frame on the bus reaches this
    // callback (can2040 has no hardware acceptance filter), so anything not
    // addressed to us would otherwise occupy a ring-buffer slot for nothing.
    if (!can_msg_is_standard_id(*msg, CAN_ID_CMD) || msg->dlc < 2) return;

    uint32_t next_head = (rx_head + 1) % RX_QUEUE_LEN;
    if (next_head == rx_tail) { rx_dropped_count++; return; }  // queue full, drop frame

    rx_queue[rx_head].id  = msg->id;
    rx_queue[rx_head].dlc = msg->dlc;
    for (uint32_t i = 0; i < msg->dlc && i < 8; i++) rx_queue[rx_head].data[i] = msg->data[i];
    rx_head = next_head;
}

static void pio_irq_handler() {
    can2040_pio_irq_handler(&cbus);
}

void can_bus_init(PIO pio, uint32_t bitrate, uint8_t rx_pin, uint8_t tx_pin) {
    can_pio = pio;

    can2040_setup(&cbus, pio_get_index(pio));
    can2040_callback_config(&cbus, can2040_cb);

    auto irqn = IRQn_Type((2 * pio_get_index(pio)) + PIO0_IRQ_0_IRQn);
    irq_set_exclusive_handler(irqn, pio_irq_handler);
    // Highest priority (0) -- can2040 is a bit-banged, cycle-critical PIO
    // implementation. USB's IRQ is left at its unconfigured default, which
    // is also the highest level, so a lower priority here would let heavy
    // USB CDC traffic (this firmware printf()s on nearly every CAN event)
    // preempt and delay CAN bit servicing mid-frame -- seen on the bus as
    // stuff/CRC/form errors, not just a dropped frame on this node.
    NVIC_SetPriority(irqn, 0);

    can2040_start(&cbus, clock_get_hz(clk_sys), bitrate, rx_pin, tx_pin);
    NVIC_EnableIRQ(irqn);
}

bool can_bus_available() {
    return rx_head != rx_tail;
}

can2040_msg can_bus_read() {
    can2040_msg msg{};
    if (rx_head == rx_tail) return msg;
    msg.id  = rx_queue[rx_tail].id;
    msg.dlc = rx_queue[rx_tail].dlc;
    for (uint32_t i = 0; i < msg.dlc && i < 8; i++) msg.data[i] = rx_queue[rx_tail].data[i];
    rx_tail = (rx_tail + 1) % RX_QUEUE_LEN;
    return msg;
}

bool can_bus_transmit(const can2040_msg &msg) {
    tx_attempt_count++;
    can2040_msg m = msg;
    bool ok = can2040_transmit(&cbus, &m) == 0;
    if (!ok) tx_failed_count++;
    return ok;
}

uint32_t can_bus_rx_dropped()   { return rx_dropped_count; }
uint32_t can_bus_bus_errors()   { return bus_error_count; }
uint32_t can_bus_tx_failed()    { return tx_failed_count; }
uint32_t can_bus_tx_attempted() { return tx_attempt_count; }
uint32_t can_bus_tx_sent()      { return tx_sent_count; }
