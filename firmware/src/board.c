#include "board.h"
#include "timing.h"
#include "stm32f0xx.h"

#define GPIO_AF1_I2C1 0x01u

void board_init(void)
{
    RCC->AHBENR |= RCC_AHBENR_GPIOAEN | RCC_AHBENR_GPIOBEN | RCC_AHBENR_GPIOCEN;

    /* SWD: PA13/PA14, AF0 (default after reset, set explicitly). */
    GPIOA->AFR[1] &= ~(0xFu << (4u * (13u - 8u))) & ~(0xFu << (4u * (14u - 8u)));
    GPIOA->MODER &= ~(GPIO_MODER_MODER13 | GPIO_MODER_MODER14);
    GPIOA->MODER |= GPIO_MODER_MODER13_1 | GPIO_MODER_MODER14_1;

    /* Status LED (on-board) and button: left in their default board role. */
    GPIOC->MODER &= ~GPIO_MODER_MODER13;
    GPIOC->MODER |= GPIO_MODER_MODER13_0;
    GPIOC->OTYPER &= ~GPIO_OTYPER_OT_13;
    GPIOC->OSPEEDR &= ~GPIO_OSPEEDER_OSPEEDR13;
    GPIOC->BRR = GPIO_PIN_13;

    GPIOA->MODER &= ~GPIO_MODER_MODER0;
    GPIOA->PUPDR &= ~GPIO_PUPDR_PUPDR0;

    /* I2C1: PB6 SCL, PB7 SDA - AF1 open drain, no internal pull-up. */
    GPIOB->MODER &= ~(GPIO_MODER_MODER6 | GPIO_MODER_MODER7);
    GPIOB->MODER |= GPIO_MODER_MODER6_1 | GPIO_MODER_MODER7_1;
    GPIOB->OTYPER |= GPIO_OTYPER_OT_6 | GPIO_OTYPER_OT_7;
    GPIOB->OSPEEDR |= GPIO_OSPEEDER_OSPEEDR6 | GPIO_OSPEEDER_OSPEEDR7;
    GPIOB->PUPDR &= ~(GPIO_PUPDR_PUPDR6 | GPIO_PUPDR_PUPDR7);
    GPIOB->AFR[0] &= ~(0xFu << (4u * 6u)) & ~(0xFu << (4u * 7u));
    GPIOB->AFR[0] |= (GPIO_AF1_I2C1 << (4u * 6u)) | (GPIO_AF1_I2C1 << (4u * 7u));

    /* TRIG / nRST: push-pull outputs, inactive high. */
    GPIOB->MODER &= ~(GPIO_MODER_MODER12 | GPIO_MODER_MODER1);
    GPIOB->MODER |= GPIO_MODER_MODER12_0 | GPIO_MODER_MODER1_0;
    GPIOB->OTYPER &= ~(GPIO_OTYPER_OT_12 | GPIO_OTYPER_OT_1);
    GPIOB->BSRR = GPIO_PIN_12 | GPIO_PIN_1;
}

void board_led_on(void)  { GPIOC->BSRR = GPIO_PIN_13; }
void board_led_off(void) { GPIOC->BRR  = GPIO_PIN_13; }
void board_led_toggle(void) { GPIOC->ODR ^= GPIO_PIN_13; }
int board_button_pressed(void) { return (GPIOA->IDR & GPIO_PIN_0) != 0; }

void board_gpio_set(uint16_t mask, uint16_t value)
{
    GPIOB->BSRR = (uint32_t)(mask & value) |
                  ((uint32_t)(mask & (uint16_t)~value) << 16u);
}

uint16_t board_gpio_get(uint16_t mask) { return (uint16_t)(GPIOB->IDR & mask); }

void board_trigger_pulse(uint16_t us)
{
    GPIOB->BRR = GPIO_PIN_12;
    delay_us(us);
    GPIOB->BSRR = GPIO_PIN_12;
}

void board_nrst_assert(uint16_t us)
{
    GPIOB->BRR = GPIO_PIN_1;
    delay_us(us);
    GPIOB->BSRR = GPIO_PIN_1;
}
