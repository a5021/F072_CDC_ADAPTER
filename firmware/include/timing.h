#ifndef TIMING_H
#define TIMING_H

#include <stdint.h>

void timing_init(void);
uint32_t timing_us(void);           /* 1 MHz free-running TIM2 count */
void delay_us(uint16_t us);         /* 1..65535 us, +/- 1 us */
void delay_ms(uint16_t ms);

#endif
