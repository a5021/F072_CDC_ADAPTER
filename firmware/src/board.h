#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>
#include "hw_defs.h"

#define PIN_LED_PORT GPIOC
#define PIN_LED      GPIO_PIN_13   /* on-board LED - not remapped */
#define PIN_BUTTON   GPIO_PIN_0    /* on-board user button (PA0) - not remapped */

#define PIN_NRST_PORT GPIOB
#define PIN_NRST      GPIO_PIN_1   /* target reset (SHOULD) */
#define PIN_TRIG_PORT GPIOB
#define PIN_TRIG      GPIO_PIN_12  /* script trigger pulse (SHOULD) */

void board_init(void);
void board_led_on(void);
void board_led_off(void);
void board_led_toggle(void);
int  board_button_pressed(void);
void board_gpio_set(uint16_t mask, uint16_t value);
uint16_t board_gpio_get(uint16_t mask);
void board_trigger_pulse(uint16_t us);
void board_nrst_assert(uint16_t us);

#endif
