#include "usb_cdc.h"
#include "stm32f103_md.h"
#include "usb.h"

static uint8_t rr[1024];
static uint16_t rh, rt_;
static uint8_t tb[1024];
static uint16_t tl, to;
static uint8_t tz;

void usb_cdc_init(void)
{
    rh = rt_ = 0; tl = to = 0; tz = 0;
    /* force re-enumeration: pull D+ low then release */
    GPIOA.CRH = (GPIOA.CRH & ~0xFu) | 0x2u;
    GPIOA.BRR = (1u << 12);
    for (volatile uint32_t i=0;i<3000000u;i++) {}
    GPIOA.CRH = (GPIOA.CRH & ~0xFu) | 0x4u;
    usb_init();
}
uint16_t usb_cdc_available(void) { return (uint16_t)((rh - rt_) & 1023u); }
uint16_t usb_cdc_read(uint8_t *b, uint16_t m)
{
    uint16_t k = 0;
    while (k < m && rh != rt_) { b[k++] = rr[rt_]; rt_ = (uint16_t)((rt_ + 1u) & 1023u); }
    return k;
}
void usb_cdc_write(const uint8_t *b, uint16_t n)
{
    if (to < tl) return;
    if (n > 1024) n = 1024;
    for (uint16_t i = 0; i < n; i++) tb[i] = b[i];
    tl = n; to = 0; tz = (n && (n & 63u) == 0) ? 1u : 0u;
    usb_cdc_poll();
}
uint16_t usb_cdc_tx_pending(void) { return (uint16_t)(tl - to + tz); }
void usb_cdc_poll(void)
{
    if (to >= tl) { if (!tz || !tl) return; if (usb_send(0,0)==0) return; tz=0; return; }
    uint16_t n = tl - to; if (n > 64) n = 64;
    to = (uint16_t)(to + usb_send(tb + to, n));
}
void USB_LP_CAN1_RX0_IRQ_Handler(void)
{
    uint8_t tmp[64];
    size_t n = usb_recv(tmp, sizeof tmp);
    for (size_t i=0;i<n;i++){ uint16_t nx=(uint16_t)((rh+1u)&1023u); if(nx==rt_)break; rr[rh]=tmp[i]; rh=nx; }
}
void usb_cdc_reenumerate(void) { usb_init(); }
