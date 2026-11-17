#include "usb_cdc.h"
#include "usbd_cdc_if.h"

uint16_t usb_cdc_available(void) { return CDC_GetRxBufferBytesAvailable_FS(); }

uint16_t usb_cdc_read(uint8_t *buf, uint16_t max_len)
{
    uint16_t n = CDC_GetRxBufferBytesAvailable_FS();
    if (n > max_len) n = max_len;
    if (n == 0) return 0;
    if (CDC_ReadRxBuffer_FS(buf, n) != USB_CDC_RX_BUFFER_OK) return 0;
    return n;
}

void usb_cdc_write(const uint8_t *buf, uint16_t len)
{
    (void)CDC_Transmit_FS((uint8_t *)buf, len);
}

uint16_t usb_cdc_tx_pending(void) { return 0; }
void usb_cdc_poll(void) { }
void usb_cdc_reenumerate(void) { }
