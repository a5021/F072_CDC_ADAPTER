/*
 * F103_CDC_I2C: USB CDC to I2C adapter firmware for STM32F103CB (Blue Pill).
 *
 * Startup (clock, vector table, .data/.bss init) is done in vendor/boot.c.
 * This file configures the peripherals the adapter uses and runs the main
 * loop: protocol_poll() decodes host frames from the CDC stream, and
 * usb_cdc_poll() feeds queued responses back to the USB stack.
 *
 * Pin assignments (LQFP48):
 *     PA9   USART1 TX  out  AF_PP 50 MHz    (bench diagnostics)
 *     PA10  USART1 RX  in   pull-up
 *     PA11  USB D-     automatic
 *     PA12  USB D+     automatic
 *     PA13  SWDIO      in/out               ST-Link programmer
 *     PA14  SWCLK      in/out               ST-Link programmer
 *     PC13  LED0       out  OUT_OD 2 MHz    on-board activity LED
 */
#include "stm32f103_md.h"

#include "board.h"
#include "capture.h"
#include "clock.h"
#include "gpio2.h"
#include "i2c_master.h"
#include "printf.h"
#include "protocol.h"
#include "stats.h"
#include "timing.h"
#include "usart.h"
#include "usb_cdc.h"
#include "usb.h"

enum {
    USART1_TX_PIN = PA9,
    USART1_RX_PIN = PA10,
    LED0_PIN      = PC13,
};

/* clang-format off */
static struct gpio_config_t {
    enum GPIO_Pin  pins;
    enum GPIO_Conf mode;
} pin_cfgs[] = {
    {PAAll, Mode_IN},           // reset
    {PBAll, Mode_IN},           // reset
    {PCAll, Mode_IN},           // reset
    {USART1_TX_PIN, Mode_AF_PP_50MHz},
    {USART1_RX_PIN, Mode_IPU},
    {LED0_PIN, Mode_Out_OD_2MHz},
    {0, 0},                     // sentinel
};

// Priority grouping: 4 group bits, 4 subpriority bits (prio[7:6] / prio[5:4]).
enum { IRQ_PRIORITY_GROUPING = 5 };

static struct {
    enum IRQn_Type irq;
    uint8_t        group, sub;
} irqprios[] = {
    {SysTick_IRQn,         0,   0},
    {USB_LP_CAN1_RX0_IRQn, 1,   0}, // USB is serviced above everything else
    {USART1_IRQn,          2,   0},
    {None_IRQn,            0xff, 0xff},
};
/* clang-format on */

static struct Ringbuffer usart1tx;

void          USART1_IRQ_Handler(void) { usart_irq_handler(&USART1, &usart1tx); }
static size_t u1puts(const char* buf, size_t len) { return usart_puts(&USART1, &usart1tx, buf, len); }

static inline void led0_off(void) { digitalHi(LED0_PIN); } // LED is active low

int main(void) {
    uint8_t rf = (RCC.CSR >> 24) & 0xfc;
    RCC.CSR |= RCC_CSR_RMVF; // clear the reset flags

    SysTick_Config(1U << 24); // tick at 72 MHz / 2^24

    NVIC_SetPriorityGrouping(IRQ_PRIORITY_GROUPING);
    for (int i = 0; irqprios[i].irq != None_IRQn; ++i) {
        NVIC_SetPriority(irqprios[i].irq, NVIC_EncodePriority(IRQ_PRIORITY_GROUPING, irqprios[i].group, irqprios[i].sub));
    }

    RCC.APB2ENR |= RCC_APB2ENR_USART1EN | RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN | RCC_APB2ENR_IOPCEN;
    delay(10); // let the clocks and peripherals start up

    for (const struct gpio_config_t* p = pin_cfgs; p->pins; ++p) {
        gpioConfig(p->pins, p->mode);
    }

    led0_off();

    usart_init(&USART1, 921600);
    cbprintf(u1puts, "SWREV:%s\n", __REVISION__);
    cbprintf(u1puts, "CPUID:%08lx\n", SCB.CPUID);
    cbprintf(u1puts, "RESET:%02x%s%s%s%s%s%s\n", rf,
             rf & 0x80 ? " LPWR" : "", rf & 0x40 ? " WWDG" : "", rf & 0x20 ? " IWDG" : "",
             rf & 0x10 ? " SFT" : "", rf & 0x08 ? " POR" : "", rf & 0x04 ? " PIN" : "");
    usart_wait(&USART1);

    board_init();
    timing_init();
    stats_init();
    i2c_init();
    capture_init();
    usb_cdc_init();

    // Freeze the pin configuration only after every init that touches GPIO
    // (board_init: I2C/TRIG/nRST/LED, usb_cdc_init: the PA12 pulse). LCKR
    // silently ignores later CR writes, so locking earlier kills those pins.
    gpioLock(PAAll & ~(1u << 12)); // keep PA12 (USB D+) free for the enumeration pulse
    gpioLock(PBAll & ~((1u << 6u) | (1u << 7u))); // keep I2C pins free for i2c_bus_clear()
    gpioLock(PCAll);

    NVIC_EnableIRQ(USB_LP_CAN1_RX0_IRQn);

    for (;;) {
        protocol_poll(); // decode host frames, dispatch commands
        usb_cdc_poll();  // hand queued responses to the USB stack
    } // forever

    return 0;
}
