#include "board.h"
#include "timing.h"
#include "stm32f103_md.h"

static void set_pin_mode(volatile uint32_t *cr, uint8_t pin, uint8_t mode)
{
    uint8_t shift = (uint8_t)((pin & 7u) * 4u);
    *cr &= ~(0xFu << shift);
    *cr |= (uint32_t)mode << shift;
}

void board_init(void)
{
    RCC.APB2ENR |= RCC_APB2ENR_AFIOEN | RCC_APB2ENR_IOPAEN |
                    RCC_APB2ENR_IOPBEN | RCC_APB2ENR_IOPCEN;

    /* I2C1: PB6 SCL / PB7 SDA - AF open drain, no internal pull-up. */
    set_pin_mode(&GPIOB.CRL, 6, 0xDu);
    set_pin_mode(&GPIOB.CRL, 7, 0xDu);
    GPIOB.BSRR = GPIO_PIN_6 | GPIO_PIN_7;

    /* Status LED (PC13), button (PA0): left in their default board role. */
    set_pin_mode(&GPIOC.CRH, 13, 0x3u);
    GPIOC.BRR = GPIO_PIN_13;
    set_pin_mode(&GPIOA.CRL, 0, 0x4u);

    /* TRIG (PB12) and nRST (PB1): push-pull outputs, idle high. */
    set_pin_mode(&GPIOB.CRH, 12, 0x3u);
    set_pin_mode(&GPIOB.CRL, 1, 0x3u);
    GPIOB.BSRR = GPIO_PIN_12 | GPIO_PIN_1;
}

void board_led_on(void)  { GPIOC.BSRR = GPIO_PIN_13; }
void board_led_off(void) { GPIOC.BRR  = GPIO_PIN_13; }
void board_led_toggle(void) { GPIOC.ODR ^= GPIO_PIN_13; }
int board_button_pressed(void) { return (GPIOA.IDR & GPIO_PIN_0) != 0; }

void board_gpio_set(uint16_t mask, uint16_t value)
{
    GPIOB.BSRR = (uint32_t)(mask & value) |
                  ((uint32_t)(mask & (uint16_t)~value) << 16u);
}

uint16_t board_gpio_get(uint16_t mask) { return (uint16_t)(GPIOB.IDR & mask); }

void board_trigger_pulse(uint16_t us)
{
    GPIOB.BRR = GPIO_PIN_12;
    delay_us(us);
    GPIOB.BSRR = GPIO_PIN_12;
}

void board_nrst_assert(uint16_t us)
{
    GPIOB.BRR = GPIO_PIN_1;
    delay_us(us);
    GPIOB.BSRR = GPIO_PIN_1;
}
