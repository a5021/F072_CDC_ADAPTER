#include "usb_cdc.h"
#include "stm32f0xx.h"

#define PMA_BASE      0x40006000UL
#define BTABLE_TBL    ((volatile uint16_t *)(PMA_BASE))
#define PMA(n)        ((volatile uint16_t *)(PMA_BASE + (n)))

#define EP0_RX_ADDR   0x40u
#define EP0_TX_ADDR   0x80u
#define EP1_RX_ADDR   0xC0u
#define EP1_TX_ADDR   0x100u
#define EP2_TX_ADDR   0x140u

#define EP_TYPE_CONTROL 0x0000u
#define EP_TYPE_BULK    0x0400u
#define EP_TYPE_INTR    0x0600u

#define EP_TX_NAK   0x0020u
#define EP_TX_VALID 0x0030u
#define EP_TX_STALL 0x0010u
#define EP_RX_VALID 0x3000u
#define EP_RX_NAK   0x2000u

#define EP_CTR_RX 0x8000u
#define EP_CTR_TX 0x0080u
#define EP_SETUP  0x0800u
#define EP_MASK   USB_EPREG_MASK

static volatile uint8_t rx_ring[USB_RX_BUF_SIZE];
static volatile uint16_t rx_head, rx_tail;
static volatile uint16_t rx_overflow;

static uint8_t tx_buf[USB_RX_BUF_SIZE];
static uint16_t tx_len, tx_off;
static volatile uint8_t tx_active;

static uint8_t setup[8];
static uint16_t ctrl_data_len, ctrl_data_off;
static const uint8_t *ctrl_data;
static uint8_t ctrl_out;            /* 1 = data stage is OUT (SET_LINE_CODING) */
static uint8_t line_coding[7] = { 0x80, 0x25, 0x00, 0x00, 0x00, 0x00, 0x08 };
static uint8_t pending_address;

static const uint8_t dev_desc[] = {
    18, 0x01,                   /* bLength, bDescriptorType */
    0x00, 0x02,                 /* bcdUSB 2.0 */
    0x02, 0x02, 0x01,           /* CDC: class/subclass/protocol */
    0x40,                       /* bMaxPacketSize0 */
    0x83, 0x04,                 /* idVendor 0x0483 */
    0x40, 0x57,                 /* idProduct 0x5740 */
    0x00, 0x01,                 /* bcdDevice */
    0x00, 0x01, 0x02, 0x03,     /* strings */
    0x01                        /* bNumConfigurations */
};

static const uint8_t cfg_desc[] = {
    /* configuration */
    9, 0x02, 0x43, 0x00, 0x02, 0x02, 0x00, 0x80, 0x32,
    /* interface 0: CDC control */
    9, 0x04, 0x00, 0x00, 0x01, 0x02, 0x02, 0x01, 0x00,
    /* header */
    5, 0x24, 0x00, 0x10, 0x01,
    /* ACM */
    4, 0x24, 0x02, 0x02,
    /* union */
    5, 0x24, 0x06, 0x00, 0x01,
    /* call management */
    5, 0x24, 0x01, 0x00, 0x01,
    /* interface 1: CDC data */
    9, 0x04, 0x01, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00,
    /* EP1 IN bulk */
    7, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00,
    /* EP1 OUT bulk */
    7, 0x05, 0x01, 0x02, 0x40, 0x00, 0x00,
    /* EP2 IN interrupt */
    7, 0x05, 0x82, 0x03, 0x10, 0x00, 0x01
};

static const uint8_t str0[] = { 4, 0x03, 0x09, 0x04 };
static const uint8_t str1[] = { 12, 0x03, 'a',0, '5',0, '0',0, '2',0, '1',0 };
static const uint8_t str2[] = {
    34, 0x03, 'F',0, '0',0, '7',0, '2',0, ' ',0, 'I',0, '2',0, 'C',0,
    ' ',0, 'A',0, 'd',0, 'a',0, 'p',0, 't',0, 'e',0, 'r',0
};
static const uint8_t str3[] = { 10, 0x03, '0',0, '0',0, '0',0, '1',0 };

static void pma_write(uint16_t addr, const uint8_t *src, uint16_t len)
{
    volatile uint16_t *p = PMA(addr);
    uint16_t i = 0;
    while (i + 1u < len) { *p++ = (uint16_t)(src[i] | ((uint16_t)src[i + 1u] << 8)); i += 2u; }
    if (i < len) *p = src[i];
}

static void pma_read(uint16_t addr, uint8_t *dst, uint16_t len)
{
    volatile uint16_t *p = PMA(addr);
    uint16_t i = 0;
    while (i + 1u < len) { uint16_t w = *p++; dst[i] = (uint8_t)w; dst[i + 1u] = (uint8_t)(w >> 8); i += 2u; }
    if (i < len) dst[i] = (uint8_t)*p;
}

static void ep_tx_start(const uint8_t *data, uint16_t len, uint16_t addr,
                        volatile uint16_t *count_reg)
{
    uint16_t n = len > 64u ? 64u : len;
    pma_write(addr, data, n);
    *count_reg = n;
}

static void usb_reset_init(void)
{
    USB->CNTR = 0;
    USB->BTABLE = 0;
    USB->DADDR = 0;

    /* Descriptor table: TX_ADDR, TX_COUNT, RX_ADDR, RX_COUNT per EP. */
    BTABLE_TBL[0] = EP0_TX_ADDR;  BTABLE_TBL[1] = 0;
    BTABLE_TBL[2] = EP0_RX_ADDR;  BTABLE_TBL[3] = (1u << 6);  /* 2 x 32-byte blocks */
    BTABLE_TBL[4] = EP1_TX_ADDR;  BTABLE_TBL[5] = 0;
    BTABLE_TBL[6] = EP1_RX_ADDR;  BTABLE_TBL[7] = (1u << 6);
    BTABLE_TBL[8] = EP2_TX_ADDR;  BTABLE_TBL[9] = 0;
    BTABLE_TBL[10] = 0;           BTABLE_TBL[11] = 0;

    USB->EP0R = EP_TYPE_CONTROL | EP_TX_NAK | EP_RX_VALID;
    USB->EP1R = EP_TYPE_BULK | 0x01u | EP_TX_NAK | EP_RX_VALID;
    USB->EP2R = EP_TYPE_INTR | 0x02u | EP_TX_NAK;

    USB->ISTR = 0;
    USB->CNTR = USB_CNTR_CTRM | USB_CNTR_RESETM | USB_CNTR_SUSPM | USB_CNTR_WKUPM;

    tx_len = tx_off = 0;
    tx_active = 0;
    pending_address = 0;
}

void usb_cdc_init(void)
{
    rx_head = rx_tail = 0;
    rx_overflow = 0;

    RCC->APB1ENR |= RCC_APB1ENR_USBEN;
    RCC->CFGR3 &= ~RCC_CFGR3_USBSW;
    if ((RCC->CFGR & RCC_CFGR_SWS_Msk) == RCC_CFGR_SWS_PLL)
        RCC->CFGR3 |= RCC_CFGR3_USBSW_PLLCLK;   /* PLL 48 MHz */
    /* else: HSI48 selected, USBSW=0 */

    NVIC_EnableIRQ(USB_IRQn);
    usb_reset_init();
    USB->BCDR |= USB_BCDR_DPPU;              /* connect pull-up */
}

static void send_ctrl(const uint8_t *data, uint16_t len)
{
    ctrl_data = data;
    ctrl_data_len = len;
    ctrl_data_off = 0;
    uint16_t n = len < 64u ? len : 64u;
    ep_tx_start(data, n, EP0_TX_ADDR, &BTABLE_TBL[1]);
    BTABLE_TBL[0] = EP0_TX_ADDR;
    USB->EP0R = (USB->EP0R & EP_MASK & ~(EP_CTR_RX | EP_CTR_TX)) | EP_TX_VALID | EP_RX_VALID;
}

static void handle_setup(void)
{
    pma_read(EP0_RX_ADDR, setup, 8);
    uint8_t type = setup[0], req = setup[1];
    uint16_t value = (uint16_t)(setup[2] | ((uint16_t)setup[3] << 8));
    uint16_t length = (uint16_t)(setup[6] | ((uint16_t)setup[7] << 8));
    ctrl_out = 0;

    if ((type & 0x1F) == 0x00) {            /* standard */
        switch (req) {
        case 0x00:  /* GET_STATUS */
            { static const uint8_t st[2] = {0, 0}; send_ctrl(st, length < 2u ? length : 2u); }
            return;
        case 0x05:  /* SET_ADDRESS */
            pending_address = (uint8_t)value;
            send_ctrl(0, 0);
            return;
        case 0x06:  /* GET_DESCRIPTOR */
            {
                uint8_t dt = (uint8_t)(value >> 8);
                const uint8_t *p = 0; uint16_t n = 0;
                switch (dt) {
                case 1: p = dev_desc; n = sizeof(dev_desc); break;
                case 2: p = cfg_desc; n = sizeof(cfg_desc); break;
                case 3:
                    switch (value & 0xFFu) {
                    case 0: p = str0; n = sizeof(str0); break;
                    case 1: p = str1; n = sizeof(str1); break;
                    case 2: p = str2; n = sizeof(str2); break;
                    case 3: p = str3; n = sizeof(str3); break;
                    default: break;
                    }
                    break;
                default: break;
                }
                if (n > length) n = length;
                send_ctrl(p, n);
            }
            return;
        case 0x09:  /* SET_CONFIGURATION */
            send_ctrl(0, 0);
            return;
        case 0x0A:  /* GET_INTERFACE */
            { static const uint8_t st[1] = {0}; send_ctrl(st, length ? 1u : 0u); }
            return;
        default: break;
        }
    } else if ((type & 0x1F) == 0x02) {     /* CDC class */
        switch (req) {
        case 0x20:  /* SET_LINE_CODING */
            ctrl_out = 1;
            BTABLE_TBL[2] = EP0_RX_ADDR;
            BTABLE_TBL[3] = (1u << 6);
            USB->EP0R = (USB->EP0R & EP_MASK & ~(EP_CTR_RX | EP_CTR_TX)) | EP_RX_VALID;
            return;
        case 0x21:  /* GET_LINE_CODING */
            send_ctrl(line_coding, length < 7u ? length : 7u);
            return;
        case 0x22:  /* SET_CONTROL_LINE_STATE */
        case 0x23:  /* SEND_BREAK */
            send_ctrl(0, 0);
            return;
        default: break;
        }
    }
    USB->EP0R = (USB->EP0R & EP_MASK & ~(EP_CTR_RX | EP_CTR_TX)) | EP_TX_STALL | EP_RX_VALID;
}

static void handle_tx(volatile uint16_t *epr, uint8_t ep)
{
    *epr = (*epr & EP_MASK) & ~EP_CTR_TX;

    if (ep == 0) {
        if (pending_address) {
            USB->DADDR = (uint8_t)(USB_DADDR_EF | pending_address);
            pending_address = 0;
        }
        ctrl_data_off += (ctrl_data_len < 64u) ? ctrl_data_len : 64u;
        if (ctrl_data_off < ctrl_data_len) {
            uint16_t n = ctrl_data_len - ctrl_data_off;
            if (n > 64u) n = 64u;
            ep_tx_start(ctrl_data + ctrl_data_off, n, EP0_TX_ADDR, &BTABLE_TBL[1]);
            *epr = (*epr & EP_MASK & ~EP_CTR_TX) | EP_TX_VALID;
        } else {
            BTABLE_TBL[1] = 0;
            *epr = (*epr & EP_MASK & ~EP_CTR_TX) | EP_TX_NAK;
        }
        return;
    }
    if (ep == 1) {
        if (tx_off >= tx_len) { tx_active = 0; return; }
        uint16_t n = tx_len - tx_off;
        if (n > 64u) n = 64u;
        ep_tx_start(tx_buf + tx_off, n, EP1_TX_ADDR, &BTABLE_TBL[5]);
        tx_off = (uint16_t)(tx_off + n);
        if (tx_off >= tx_len) {
            if (n == 64u) {
                BTABLE_TBL[5] = 0;
                tx_off++;   /* marker: one extra empty transfer */
            } else {
                tx_active = 0;
                return;
            }
        }
        *epr = (*epr & EP_MASK & ~EP_CTR_TX) | EP_TX_VALID | EP_RX_VALID;
        return;
    }
}

static void handle_rx(volatile uint16_t *epr, uint8_t ep)
{
    uint16_t count = (BTABLE_TBL[ep == 0 ? 3 : 7] & 0x3FFu);
    *epr = (*epr & EP_MASK) & ~EP_CTR_RX;

    if (ep == 0) {
        if (ctrl_out) {
            pma_read(EP0_RX_ADDR, line_coding, count > 7u ? 7u : count);
            ctrl_out = 0;
            USB->EP0R = (USB->EP0R & EP_MASK & ~(EP_CTR_RX | EP_CTR_TX)) | EP_TX_NAK | EP_RX_VALID;
        } else if (*epr & EP_SETUP) {
            handle_setup();
        } else {
            USB->EP0R = (USB->EP0R & EP_MASK & ~(EP_CTR_RX | EP_CTR_TX)) | EP_TX_NAK | EP_RX_VALID;
        }
        return;
    }

    if (ep == 1) {
        uint8_t tmp[64];
        pma_read(EP1_RX_ADDR, tmp, count);
        uint16_t space = (uint16_t)((rx_head - rx_tail) & (USB_RX_BUF_SIZE - 1u));
        space = (uint16_t)(USB_RX_BUF_SIZE - 1u - space);
        if (count <= space) {
            for (uint16_t i = 0; i < count; i++) {
                rx_ring[rx_head] = tmp[i];
                rx_head = (uint16_t)((rx_head + 1u) & (USB_RX_BUF_SIZE - 1u));
            }
        } else {
            rx_overflow++;
        }
        BTABLE_TBL[6] = EP1_RX_ADDR;
        BTABLE_TBL[7] = (1u << 6);
        USB->EP1R = (USB->EP1R & EP_MASK & ~EP_CTR_RX) | EP_RX_VALID | EP_TX_NAK;
    }
}

void USB_IRQHandler(void)
{
    uint16_t istr = USB->ISTR;
    if (istr & USB_ISTR_RESET) {
        USB->ISTR = (uint16_t)~USB_ISTR_RESET;
        usb_reset_init();
        return;
    }
    if (istr & USB_ISTR_SUSP) USB->ISTR = (uint16_t)~USB_ISTR_SUSP;
    if (istr & USB_ISTR_WKUP) USB->ISTR = (uint16_t)~USB_ISTR_WKUP;
    if (istr & USB_ISTR_CTR) {
        uint8_t ep = (uint8_t)(istr & USB_ISTR_EP_ID);
        volatile uint16_t *epr = &USB->EP0R + (ep * 2u);
        uint16_t e = *epr;
        if (e & EP_CTR_TX) handle_tx(epr, ep);
        if (e & EP_CTR_RX) handle_rx(epr, ep);
        USB->ISTR = (uint16_t)~USB_ISTR_CTR;
    }
}

uint16_t usb_cdc_available(void)
{
    return (uint16_t)((rx_head - rx_tail) & (USB_RX_BUF_SIZE - 1u));
}

uint16_t usb_cdc_read(uint8_t *buf, uint16_t max_len)
{
    uint16_t n = 0;
    while ((n < max_len) && (rx_head != rx_tail)) {
        buf[n++] = rx_ring[rx_tail];
        rx_tail = (uint16_t)((rx_tail + 1u) & (USB_RX_BUF_SIZE - 1u));
    }
    return n;
}

void usb_cdc_write(const uint8_t *buf, uint16_t len)
{
    if (tx_active || len == 0u) return;
    if (len > USB_RX_BUF_SIZE) len = USB_RX_BUF_SIZE;
    for (uint16_t i = 0; i < len; i++) tx_buf[i] = buf[i];
    tx_len = len;
    tx_off = 0;

    uint16_t n = len < 64u ? len : 64u;
    ep_tx_start(buf, n, EP1_TX_ADDR, &BTABLE_TBL[5]);
    tx_off = n;
    tx_active = 1;
    USB->EP1R = (USB->EP1R & EP_MASK & ~EP_CTR_TX) | EP_TX_VALID;
}

uint16_t usb_cdc_tx_pending(void) { return tx_active ? (uint16_t)(tx_len - tx_off) : 0u; }

void usb_cdc_poll(void)
{
    /* USB is interrupt driven; hook kept for future flow control. */
}
