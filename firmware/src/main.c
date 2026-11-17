#include "board.h"
#include "timing.h"
#include "i2c_master.h"
#include "capture.h"
#include "usb_cdc.h"
#include "protocol.h"
#include "stats.h"
#include "stm32f1xx.h"

uint32_t SystemCoreClock = 72000000UL;

void SystemInit(void)
{
    /* HSE 8 MHz -> PLL x9 -> 72 MHz SYSCLK; USB clock = PLL/1.5 = 48 MHz. */
    FLASH->ACR = FLASH_ACR_LATENCY_2 | FLASH_ACR_PRFTBE;
    RCC->CR |= RCC_CR_HSEON;
    while ((RCC->CR & RCC_CR_HSERDY) == 0u) { }

    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_PLLMULL | RCC_CFGR_PPRE1 |
                               RCC_CFGR_PPRE2 | RCC_CFGR_HPRE |
                               RCC_CFGR_USBPRE | RCC_CFGR_ADCPRE)) |
                RCC_CFGR_PLLSRC | RCC_CFGR_PLLMULL9 |
                RCC_CFGR_PPRE1_DIV2;  /* USBPRE=0: USB clock = PLL/1.5 = 48 MHz */
    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0u) { }
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) { }
}

static void watchdog_init(void)
{
    IWDG->KR = 0x5555u;
    IWDG->PR = 4u;        /* /64 of ~40 kHz LSI -> ~625 Hz */
    IWDG->RLR = 1000u;    /* ~1.6 s */
    IWDG->KR = 0xAAAAu;
    IWDG->KR = 0xCCCCu;
}

int main(void)
{
    board_init();
    timing_init();
    stats_init();
    i2c_init();
    capture_init();
    usb_cdc_init();
    delay_ms(50);
    usb_cdc_reenumerate();   /* force host to re-read descriptors */
    watchdog_init();

    uint32_t last_feed = timing_us();
    uint32_t busy_since = 0u;

    for (;;) {
        protocol_poll();
        usb_cdc_poll();

        uint32_t now = timing_us();
        if ((uint32_t)(now - last_feed) > 200000u) {
            IWDG->KR = 0xAAAAu;
            last_feed = now;
            board_led_toggle();
        }

        if (i2c_is_busy()) {
            if (busy_since == 0u) busy_since = now;
            else if ((uint32_t)(now - busy_since) > 2000000u) {
                i2c_bus_clear();
                busy_since = 0u;
            }
        } else {
            busy_since = 0u;
        }
    }
}
