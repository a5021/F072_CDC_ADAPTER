/*
 * CDC data plumbing: byte ring buffer on the RX side, packet-framed stream
 * on the TX side, plus the USB event pump.
 *
 * The USB interrupt handler copies each received bulk-OUT packet into the
 * RX ring; the main loop drains it via usb_cdc_read(). Outgoing bytes are
 * queued in a TX buffer and handed to the USB stack 64 bytes at a time by
 * usb_cdc_poll(); a zero-length packet is appended after any multiple of
 * 64 bytes so the host sees the message boundary.
 */
#include "usb_cdc.h"
#include "stm32f103_md.h"
#include "usb.h"

#define RING_SIZE  1024u // power of two, index masked with RING_SIZE-1
#define RING_MASK  (RING_SIZE - 1u)
#define TX_MAX     1024u
#define EP_SIZE    64u    // bulk endpoint packet size

static uint8_t  rx_ring[RING_SIZE];
static volatile uint16_t rx_head, rx_tail;

static uint8_t  tx_buf[TX_MAX];
static uint16_t tx_len, tx_pos; // queued bytes / bytes already handed to usb_send
static uint8_t  tx_zlp;         // zero-length packet still to be sent

void usb_cdc_init(void) {
    rx_head = rx_tail = 0;
    tx_len = tx_pos = 0;
    tx_zlp = 0;

    // Force a fresh enumeration: drive D+ (PA12) low for ~100 ms BEFORE the
    // USB peripheral is clocked. Once the USB cell is enabled it owns PA12
    // (embedded 1.5 k pull-up), and the GPIO can no longer pull D+ low -
    // the host then never sees a disconnect and keeps a stale COM port.
    GPIOA.CRH = (GPIOA.CRH & ~(0xFu << 16)) | (0x2u << 16); // PA12: output 2 MHz
    GPIOA.BRR = (1u << 12);
    for (volatile uint32_t i = 0; i < 3000000u; ++i) {}

    RCC.APB1ENR |= RCC_APB1ENR_USBEN;
    GPIOA.CRH = (GPIOA.CRH & ~(0xFu << 16)) | (0x4u << 16); // PA12: back to the USB peripheral

    usb_init();
}

uint16_t usb_cdc_available(void) { return (uint16_t)((rx_head - rx_tail) & RING_MASK); }

uint16_t usb_cdc_read(uint8_t *buf, uint16_t max_len) {
    uint16_t n = 0;
    while (n < max_len && rx_head != rx_tail) {
        buf[n++] = rx_ring[rx_tail];
        rx_tail  = (uint16_t)((rx_tail + 1u) & RING_MASK);
    }
    return n;
}

void usb_cdc_write(const uint8_t *buf, uint16_t len) {
    if (tx_pos < tx_len) return; // previous message still being transmitted
    if (len > TX_MAX) len = TX_MAX;

    for (uint16_t i = 0; i < len; ++i) tx_buf[i] = buf[i];
    tx_len = len;
    tx_pos = 0;
    tx_zlp = (len && (len & (EP_SIZE - 1u)) == 0) ? 1u : 0u;

    usb_cdc_poll();
}

uint16_t usb_cdc_tx_pending(void) { return (uint16_t)(tx_len - tx_pos + tx_zlp); }

void usb_cdc_poll(void) {
    if (tx_pos >= tx_len) {
        // whole message sent; emit the zero-length terminator if needed
        if (!tx_zlp || !tx_len) return;
        if (usb_send(0, 0) == 0) return;
        tx_zlp = 0;
        return;
    }

    uint16_t n = (uint16_t)(tx_len - tx_pos);
    if (n > EP_SIZE) n = EP_SIZE;
    tx_pos = (uint16_t)(tx_pos + usb_send(tx_buf + tx_pos, n));
}

// USB interrupt: drive the stack and copy received bytes into the RX ring.
void USB_LP_CAN1_RX0_IRQ_Handler(void) {
    uint8_t packet[EP_SIZE];
    size_t  n = usb_recv(packet, sizeof packet);

    for (size_t i = 0; i < n; ++i) {
        uint16_t next = (uint16_t)((rx_head + 1u) & RING_MASK);
        if (next == rx_tail) break; // ring full, drop the rest of the packet
        rx_ring[rx_head] = packet[i];
        rx_head = next;
    }
}

void usb_cdc_reenumerate(void) { usb_init(); }
