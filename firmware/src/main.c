#include "board.h"
#include "timing.h"
#include "i2c_master.h"
#include "capture.h"
#include "usb_cdc.h"
#include "protocol.h"
#include "stats.h"
#include "stm32f0xx.h"

uint32_t SystemCoreClock = 48000000UL;

void SystemInit(void)
{
    /* HSE 8 MHz -> PLL x6 -> 48 MHz; HSI48 + CRS as fallback. */
    FLASH->ACR = FLASH_ACR_LATENCY | FLASH_ACR_PRFTBE;
    volatile uint32_t spin = 0;
    RCC->CR |= RCC_CR_HSEON;
    while ((RCC->CR & RCC_CR_HSERDY) == 0u && spin++ < 500000u) { }

    if (RCC->CR & RCC_CR_HSERDY) {
        RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_PLLSRC_Msk & ~RCC_CFGR_PLLMUL_Msk &
                     ~RCC_CFGR_PPRE_Msk & ~RCC_CFGR_HPRE_Msk) |
                    RCC_CFGR_PLLSRC_HSE_PREDIV | RCC_CFGR_PLLMUL6;
        RCC->CR |= RCC_CR_PLLON;
        while ((RCC->CR & RCC_CR_PLLRDY) == 0u) { }
        RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW_Msk) | RCC_CFGR_SW_PLL;
        while ((RCC->CFGR & RCC_CFGR_SWS_Msk) != RCC_CFGR_SWS_PLL) { }
    } else {
        RCC->APB1ENR |= RCC_APB1ENR_CRSEN;
        RCC->CR2 |= RCC_CR2_HSI48ON;
        while ((RCC->CR2 & RCC_CR2_HSI48RDY) == 0u) { }
        CRS->CFGR = (48000u - 1u) | (0x20u << 16) | (uint32_t)CRS_CFGR_SYNCSRC_1;
        CRS->CR = CRS_CR_CEN | CRS_CR_AUTOTRIMEN;
        RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW_Msk) | RCC_CFGR_SW_HSI48;
        while ((RCC->CFGR & RCC_CFGR_SWS_Msk) != RCC_CFGR_SWS_HSI48) { }
    }
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
