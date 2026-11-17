/*
 * Minimal USB device stack for the STM32F103 (vendor: daedaleanai/stm32f103_usb,
 * adapted for CDC/ACM). Register level, no ST HAL.
 *
 * Topology: one device, one configuration, two interfaces (CDC control +
 * CDC data), endpoint 0 for control requests and bulk endpoints 0x01/0x81
 * with 64-byte packets in both directions. VID/PID 0483:5740 identifies the
 * device as an STM32 Virtual COM Port, so no vendor driver is needed on
 * Windows, Linux or macOS.
 *
 * Only the USB_LP_CAN1_RX0 interrupt is used; the handler must call
 * usb_recv(), which drives the whole stack (see usb.h).
 *
 * Not implemented (by design):
 *   - suspend/wakeup handling
 *   - string descriptors
 *   - SET_DESCRIPTOR, feature requests other than endpoint HALT
 *
 * The linker script must define:
 *     USB     = 0x40005C00;
 *     USB_PMA = 0x40006000;
 */
#include "usb.h"
#include "stm32f103usb.h"

/* ------------------------------------------------------------------------- */
/* 1. Descriptors                                                            */
/* ------------------------------------------------------------------------- */

static uint8_t device_descriptor[] = {
    18,            // bLength
    0x01,          // bDescriptorType: DEVICE
    0x00, 0x02,    // bcdUSB: 2.00
    2,             // bDeviceClass: CDC
    0, 0,          // bDeviceSubClass, bDeviceProtocol
    64,            // bMaxPacketSize0
    0x83, 0x04,    // idVendor:  0x0483 (STMicroelectronics)
    0x40, 0x57,    // idProduct: 0x5740 (Virtual COM Port)
    0x00, 0x02,    // bcdDevice: 2.00
    0, 0, 0,       // manufacturer / product / serial number strings not set
    1,             // bNumConfigurations
};

static uint8_t config_descriptor[] = {
    // Configuration 0 header
    9,                                // bLength
    0x02,                             // bDescriptorType: CONFIGURATION
    9 + 9 + 5 + 4 + 5 + 9 + 7 + 7, 0, // wTotalLength
    2,                                // bNumInterfaces: CDC control + CDC data
    1,                                // bConfigurationValue
    0,                                // iConfiguration not set
    0x80,                             // bmAttributes: bus powered
    50,                               // bMaxPower: 100 mA

    // Interface 0: CDC control
    9,    // bLength
    0x04, // bDescriptorType: INTERFACE
    0, 0, // bInterfaceNumber, bAlternateSetting
    0,    // bNumEndpoints
    0x02, // bInterfaceClass:    CDC
    0x02, // bInterfaceSubClass: ACM
    0,    // bInterfaceProtocol: NONE
    0,    // iInterface not set

    // CDC Header Functional Descriptor, CDC spec 5.2.3.1, table 26
    5,          // bFunctionLength
    0x24,       // bDescriptorType:    CS_INTERFACE
    0x00,       // bDescriptorSubtype: HEADER
    0x10, 0x01, // bcdCDC: 1.10

    // Abstract Control Management Functional Descriptor, CDC spec 5.2.3.3, table 28
    4,    // bFunctionLength
    0x24, // bDescriptorType:    CS_INTERFACE
    0x02, // bDescriptorSubtype: ACM
    0x00, // bmCapabilities: none

    // Union Functional Descriptor, CDC spec 5.2.3.8, table 33
    5,    // bFunctionLength
    0x24, // bDescriptorType:    CS_INTERFACE
    0x06, // bDescriptorSubtype: UNION
    0,    // bMasterInterface
    1,    // bSlaveInterface0

    // Interface 1: CDC data
    9,    // bLength
    0x04, // bDescriptorType: INTERFACE
    1, 0, // bInterfaceNumber, bAlternateSetting
    2,    // bNumEndpoints
    0x0A, // bInterfaceClass: USB_CLASS_DATA
    0,    // bInterfaceSubClass
    0,    // bInterfaceProtocol
    0,    // iInterface not set

    // Endpoint 0x01: bulk OUT
    7,     // bLength
    0x05,  // bDescriptorType: ENDPOINT
    0x01,  // bEndpointAddress: 1-OUT
    0x02,  // bmAttributes: BULK
    64, 0, // wMaxPacketSize
    0,     // bInterval, ignored for BULK

    // Endpoint 0x81: bulk IN
    7,     // bLength
    0x05,  // bDescriptorType: ENDPOINT
    0x81,  // bEndpointAddress: 1-IN
    0x02,  // bmAttributes: BULK
    64, 0, // wMaxPacketSize
    0,     // bInterval, ignored for BULK
};

/* ------------------------------------------------------------------------- */
/* 2. Setup request codes                                                    */
/* ------------------------------------------------------------------------- */

enum {
    REQ_TYPE_TX        = 1 << 7, // bit 7: direction, 1 = device->host
    REQ_TYPE_INTERFACE = 0x01,   // bits 1..0: recipient
    REQ_TYPE_ENDPOINT  = 0x02,
    REQ_TYPE_OTHER     = 0x03,

    // USB standard request codes, USB 2.0 table 9-4
    REQ_GET_STATUS        = (0 << 8) | REQ_TYPE_TX,
    REQ_CLR_FEATURE       = (1 << 8),  // 1: remote wakeup, 2: test mode
    REQ_SET_FEATURE       = (3 << 8),
    REQ_SET_ADDRESS       = (5 << 8),
    REQ_GET_DESCRIPTOR    = (6 << 8) | REQ_TYPE_TX,
    REQ_GET_CONFIGURATION = (8 << 8) | REQ_TYPE_TX,
    REQ_SET_CONFIGURATION = (9 << 8),
    REQ_GET_INTERFACE     = (10 << 8) | REQ_TYPE_TX | REQ_TYPE_INTERFACE,
    REQ_SET_INTERFACE     = (11 << 8) | REQ_TYPE_INTERFACE,

    // CDC class requests, CDC 1.2 table 3
    REQ_SET_LINE_CODING   = (0x20 << 8) | 0x21,
    REQ_GET_LINE_CODING   = (0x21 << 8) | 0xA1,
    REQ_SET_CONTROL_LINE  = (0x22 << 8) | 0x21,

    // Recipient-specific variants we care about
    REQ_GET_STATUS_INTERFACE  = REQ_GET_STATUS | REQ_TYPE_INTERFACE,
    REQ_GET_STATUS_ENDPOINT   = REQ_GET_STATUS | REQ_TYPE_ENDPOINT,
    REQ_CLR_FEATURE_ENDPOINT  = REQ_CLR_FEATURE | REQ_TYPE_ENDPOINT, // clear HALT
    REQ_SET_FEATURE_ENDPOINT  = REQ_SET_FEATURE | REQ_TYPE_ENDPOINT, // set HALT
};

/* The most recently received SETUP request.
 * req: low byte = bmRequestType, high byte = bRequest. */
static struct {
    uint16_t req;
    uint16_t val;
    uint16_t idx;
    uint16_t len;
} ctrl_req;

static uint8_t ctrl_data_out_pending; // SET_LINE_CODING data stage in progress
static uint8_t line_coding[7] = {0, 0xC2, 1, 0, 0, 0, 8}; // 115200-8-N-1

static enum usb_state_t device_state = USB_UNATTACHED;

/* ------------------------------------------------------------------------- */
/* 3. Packet-memory helpers                                                  */
/* ------------------------------------------------------------------------- */

static inline uint16_t read_le16(const uint8_t* src) {
    return ((uint16_t)(src[0])) | (((uint16_t)(src[1])) << 8);
}

// copy buf[:min(len,sz)] from the rx packet buffer of endpoint ep
static size_t read_buffer(uint8_t ep, uint8_t* buf, size_t sz) {
    uint16_t len = usb_ep_get_rx_count(ep);
    if (len > sz) {
        len = sz;
    }

    const uint16_t* src = usb_ep_rx_buf(ep);
    for (size_t i = 0; 2 * i + 1 < len; ++i) {
        buf[2 * i]     = src[2 * i];
        buf[2 * i + 1] = src[2 * i] >> 8;
    }
    if (len & 1) {
        buf[len - 1] = src[len - 1];
    }

    return len;
}

// copy buf[:len] to the tx packet buffer of endpoint ep
static size_t write_buffer(uint8_t ep, const uint8_t* buf, size_t len) {
    if (len > 64) {
        len = 64;
    }

    uint16_t* dst = usb_ep_tx_buf(ep);
    for (size_t i = 0; 2 * i + 1 < len; ++i) {
        dst[2 * i] = read_le16(buf + 2 * i);
    }
    if (len & 1) {
        dst[len - 1] = buf[len - 1];
    }

    usb_ep_set_tx_count(ep, len);
    return len;
}

/* ------------------------------------------------------------------------- */
/* 4. Control request handlers                                               */
/* ------------------------------------------------------------------------- */

// Control flows, USB 2.0 sections 5.5.5 and 8.5.3:
//   req.len > 0 &&  (req.req & REQ_TYPE_TX)  SETUP (DATA-IN)* STATUS-OUT
//   req.len > 0 && !(req.req & REQ_TYPE_TX)  SETUP (DATA-OUT)* STATUS-IN ZLP
//   req.len == 0                             SETUP STATUS-IN ZLP
// We only implement the DATA-OUT flow for SET_LINE_CODING; every other
// non-zero-length OUT request is stalled. All responses fit in one 64-byte
// packet, so the DATA-IN flow needs no multi-packet handling.

// false on failure, true on success
static int handle_set_request(void) {
    switch (ctrl_req.req) {
    case REQ_SET_ADDRESS:
        // the address is applied after the ACK has been sent; check validity here
        return (device_state != USB_CONFIGURED) && (ctrl_req.val < 128) && (ctrl_req.idx == 0);

    case REQ_SET_CONFIGURATION:
        switch (device_state) {
        case USB_ADDRESS:
            switch (ctrl_req.val) {
            case 1:
                // configure endpoints 0x01/0x81
                usb_ep_config(1, USB_EP_TYPE_BULK, 0x01);
                usb_ep_set_stat_rx(1, USB_EP_STAT_VALID);
                usb_ep_set_stat_tx(1, USB_EP_STAT_NAK);
                device_state = USB_CONFIGURED;
                // fallthrough
            case 0:
                return 1;
            }
            return 0;

        case USB_CONFIGURED:
            switch (ctrl_req.val) {
            case 0:
                // unconfigure endpoints 0x01/0x81
                usb_ep_reset(1);
                device_state = USB_ADDRESS;
                // fallthrough
            case 1:
                return 1;
            }
            // fallthrough
        default:
            break;
        }
        return 0;

    case REQ_SET_CONTROL_LINE: return 1; // DTR/RTS ignored, port is always ready
    case REQ_SET_LINE_CODING:  return (ctrl_req.len == 7);
    case REQ_SET_INTERFACE:    return (device_state == USB_CONFIGURED) && (ctrl_req.idx == 0);

    case REQ_CLR_FEATURE_ENDPOINT:
        if ((device_state != USB_CONFIGURED) || ((ctrl_req.idx & 0xf) != 1))
            return 0;
        if (ctrl_req.idx & 0x80) {
            usb_ep_set_stat_tx(1, USB_EP_STAT_NAK);
            usb_ep_clr_dtog_tx(1);
        } else {
            usb_ep_set_stat_rx(1, USB_EP_STAT_NAK);
            usb_ep_clr_dtog_rx(1);
        }
        return 1;

    case REQ_SET_FEATURE_ENDPOINT:
        if ((device_state != USB_CONFIGURED) || ((ctrl_req.idx & 0xf) != 1))
            return 0;
        if (ctrl_req.idx & 0x80) {
            usb_ep_set_stat_tx(1, USB_EP_STAT_STALL);
        } else {
            usb_ep_set_stat_rx(1, USB_EP_STAT_STALL);
        }
        return 1;
    }

    return 0;
}

// false on failure, true on success
static int handle_get_request(void) {
    uint8_t data[2] = {0, 0};
    size_t  len     = 0;

    switch (ctrl_req.req) {
    case REQ_GET_DESCRIPTOR:
        if (ctrl_req.idx != 0)
            return 0;
        switch (ctrl_req.val) {
        case 0x0100: // DEVICE
            len = write_buffer(0, device_descriptor, sizeof device_descriptor);
            break;
        case 0x0200: // CONFIGURATION
            len = write_buffer(0, config_descriptor, sizeof config_descriptor);
            break;
        default:
            return 0;
        }
        if (len > ctrl_req.len)
            usb_ep_set_tx_count(0, ctrl_req.len);
        return 1;

    case REQ_GET_STATUS:
    case REQ_GET_STATUS_INTERFACE:
        if ((device_state == USB_DEFAULT) || (ctrl_req.val != 0) ||
            (ctrl_req.idx != 0) || (ctrl_req.len != 2))
            return 0;
        break; // data stays zero: bus powered, no remote wakeup

    case REQ_GET_STATUS_ENDPOINT:
        if ((device_state == USB_DEFAULT) || (ctrl_req.val != 0) || (ctrl_req.len != 2))
            return 0;
        switch (ctrl_req.idx) {
        case 0x00: data[0] = (usb_ep_get_stat_rx(0) == USB_EP_STAT_STALL) ? 1 : 0; break;
        case 0x80: data[0] = (usb_ep_get_stat_tx(0) == USB_EP_STAT_STALL) ? 1 : 0; break;
        case 0x01: data[0] = (usb_ep_get_stat_rx(1) == USB_EP_STAT_STALL) ? 1 : 0; break;
        case 0x81: data[0] = (usb_ep_get_stat_tx(1) == USB_EP_STAT_STALL) ? 1 : 0; break;
        default:   return 0;
        }
        break;

    case REQ_GET_LINE_CODING:
        return write_buffer(0, line_coding, ctrl_req.len < 7 ? ctrl_req.len : 7) > 0;

    case REQ_GET_CONFIGURATION:
        if ((device_state == USB_DEFAULT) || (ctrl_req.len != 1))
            return 0;
        data[0] = (device_state == USB_CONFIGURED) ? 1 : 0;
        break;

    case REQ_GET_INTERFACE:
        if ((device_state != USB_CONFIGURED) || (ctrl_req.len != 1))
            return 0;
        break; // data stays zero: only alternate setting 0 exists
    }

    return write_buffer(0, data, ctrl_req.len) == ctrl_req.len;
}

/* ------------------------------------------------------------------------- */
/* 5. Endpoint 0 event handling                                              */
/* ------------------------------------------------------------------------- */

static void handle_ep0(void) {
    // DATA-OUT stage of SET_LINE_CODING
    if (ctrl_data_out_pending && (USB.EPR[0] & USB_EPRx_CTR_RX)) {
        uint16_t n = usb_ep_get_rx_count(0);
        if (n > 7) n = 7;
        const uint16_t* src = usb_ep_rx_buf(0);
        for (uint16_t i = 0; i < n; ++i) {
            uint16_t w  = src[2 * (i / 2)];
            line_coding[i] = (i & 1) ? (uint8_t)(w >> 8) : (uint8_t)w;
        }
        ctrl_data_out_pending = 0;
        usb_ep_clr_ctr_rx(0);
        usb_ep_set_tx_count(0, 0); // STATUS-IN ZLP
        usb_ep_set_stat_tx(0, USB_EP_STAT_VALID);
        return;
    }

    switch (USB.EPR[0] & (USB_EPRx_CTR_RX | USB_EPRx_SETUP | USB_EPRx_CTR_TX)) {

    case USB_EPRx_CTR_RX | USB_EPRx_SETUP:
        if (usb_ep_get_rx_count(0) != 8)
            break;

        const uint16_t* src = usb_ep_rx_buf(0);
        ctrl_req.req = src[0];
        ctrl_req.val = src[2];
        ctrl_req.idx = src[4];
        ctrl_req.len = src[6];

        usb_ep_clr_ctr_rx(0);

        // non-zero length OUT request: only SET_LINE_CODING has a data stage
        if ((ctrl_req.len > 0) && !(ctrl_req.req & REQ_TYPE_TX)) {
            if (ctrl_req.req == REQ_SET_LINE_CODING) {
                ctrl_data_out_pending = 1;
                usb_ep_set_stat_rx(0, USB_EP_STAT_VALID);
                return;
            }
            break;
        }

        if (ctrl_req.len == 0) {
            if (!handle_set_request())
                break;
            usb_ep_set_tx_count(0, 0); // STATUS-IN ZLP
        } else {
            if (!handle_get_request()) // sets up the reply buffer
                break;
        }

        usb_ep_set_stat_tx(0, USB_EP_STAT_VALID);
        return;

    case USB_EPRx_CTR_TX: // IN: reply of a GET, or the ACK of a SET, was sent
        usb_ep_clr_ctr_tx(0);

        if (ctrl_req.len == 0) {
            // last request was a SET; apply a deferred SET_ADDRESS now
            if (ctrl_req.req == REQ_SET_ADDRESS) {
                usb_daddr_set_add(ctrl_req.val);
                USB.DADDR |= USB_DADDR_EF;
                device_state = (ctrl_req.val == 0) ? USB_DEFAULT : USB_ADDRESS;
            }
        } else {
            // last request was a GET; arm EP0 for the host's STATUS-OUT
            usb_ep_set_stat_rx(0, USB_EP_STAT_VALID);
        }
        return;

    case USB_EPRx_CTR_RX: // OUT: final STATUS ZLP of a GET
        if (usb_ep_get_rx_count(0) != 0)
            break;
        // normally cleared by the STATUS_OUT mechanism, but apparently not
        usb_ep_clr_ctr_rx(0);
        return;
    }

    // we did not handle the transfer: abort it
    usb_ep_set_stat_rx(0, USB_EP_STAT_STALL);
    usb_ep_set_stat_tx(0, USB_EP_STAT_STALL);
}

/* ------------------------------------------------------------------------- */
/* 6. Public API                                                             */
/* ------------------------------------------------------------------------- */

static const char* const state_names[] = {
    "UNATTACHED", "DEFAULT", "ADDRESS", "CONFIGURED", "SUSPENDED",
};

enum usb_state_t usb_state(void) { return device_state; }

const char* usb_state_str(enum usb_state_t s) {
    if ((size_t)s < sizeof state_names / sizeof state_names[0])
        return state_names[s];
    return "UNDEFINED";
}

void usb_init(void) {
    device_state = USB_UNATTACHED;

    USB.CNTR   = USB_CNTR_FRES; // hold in reset, clear power down
    USB.ISTR   = 0;
    USB.DADDR  = 0;
    USB.BTABLE = 0;

    for (int i = 0; i < 8; ++i) {
        usb_ep_reset(i);
    }

    // 512 bytes of packet memory: EP0 TX/RX at 1*64/2*64, EP1 TX/RX at 3*64/4*64
    USB_PMA.btable[0].ADDR_TX = 1 * 64; // control endpoint 0x00/0x80
    USB_PMA.btable[0].ADDR_RX = 2 * 64;
    usb_ep_set_rx_size(0, 64);

    USB_PMA.btable[1].ADDR_TX = 3 * 64; // bulk endpoint 0x01/0x81
    USB_PMA.btable[1].ADDR_RX = 4 * 64;
    usb_ep_set_rx_size(1, 64);

    // leave reset, enable correct-transfer and bus-reset interrupts
    USB.CNTR = USB_CNTR_CTRM | USB_CNTR_RESETM;
}

void usb_shutdown(void) {
    USB.CNTR = USB_CNTR_FRES;
    USB.ISTR = 0;
    USB.CNTR = USB_CNTR_FRES | USB_CNTR_PDWN | USB_CNTR_LPMODE;

    device_state = USB_UNATTACHED;
}

size_t usb_recv(uint8_t* buf, size_t sz) {
    uint16_t istr = USB.ISTR;

    USB.ISTR &= ~(USB_ISTR_SOF | USB_ISTR_ESOF | USB_ISTR_ERR | USB_ISTR_PMAOVR);

    if (istr & USB_ISTR_RESET) {
        USB.ISTR &= ~(USB_ISTR_RESET | USB_ISTR_WKUP | USB_ISTR_SUSP);
        USB.CNTR &= ~(USB_CNTR_RESUME | USB_CNTR_FSUSP | USB_CNTR_LPMODE |
                      USB_CNTR_PDWN | USB_CNTR_FRES);

        usb_ep_config(0, USB_EP_TYPE_CONTROL, 0);
        usb_ep_set_stat_tx(0, USB_EP_STAT_STALL); // only SETUP will succeed
        usb_ep_set_stat_rx(0, USB_EP_STAT_STALL);

        for (int i = 1; i < 8; ++i) {
            usb_ep_reset(i);
        }

        usb_daddr_set_add(0);
        USB.DADDR |= USB_DADDR_EF;

        device_state = USB_DEFAULT;
        return 0;
    }

    if (istr & USB_ISTR_CTR) {
        uint8_t ep = usb_istr_get_ep_id();
        if (ep == 0) {
            handle_ep0();
            return 0;
        }

        // ep == 1 (bulk). IN: the last transmit succeeded and the hardware
        // has toggled DTOG; just clear the flag so usb_send() may proceed.
        if (USB.EPR[ep] & USB_EPRx_CTR_TX) {
            usb_ep_clr_ctr_tx(ep);
        }

        if (USB.EPR[ep] & USB_EPRx_CTR_RX) {
            // OUT transaction: hand the packet to the caller and re-arm
            usb_ep_clr_ctr_rx(ep);

            size_t len = read_buffer(ep, buf, sz);
            usb_ep_set_stat_rx(ep, USB_EP_STAT_VALID);
            return len;
        }
    }

    return 0;
}

size_t usb_send(const uint8_t* buf, size_t len) {
    // previous packet still in flight, or device not configured yet
    if (usb_ep_get_stat_tx(1) != USB_EP_STAT_NAK) {
        return 0;
    }

    len = write_buffer(1, buf, len);
    usb_ep_set_stat_tx(1, USB_EP_STAT_VALID);

    return len;
}
