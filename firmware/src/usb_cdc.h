#ifndef USB_CDC_H
#define USB_CDC_H

#include <stdint.h>

#define USB_RX_BUF_SIZE 1024u

void usb_cdc_init(void);
uint16_t usb_cdc_read(uint8_t *buf, uint16_t max_len);  /* pop from RX ring */
uint16_t usb_cdc_available(void);
void usb_cdc_write(const uint8_t *buf, uint16_t len);   /* queue on TX EP */
uint16_t usb_cdc_tx_pending(void);
void usb_cdc_poll(void);
void usb_cdc_reenumerate(void);

#endif
