#include "timing.h"
#include "stm32f1xx.h"

void timing_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
    TIM2->PSC = 48u - 1u;               /* TIM2 clock = 72 MHz (APB1 x2) */
    TIM2->ARR = 0xFFFFFFFFu;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR  = 0;
    TIM2->CR1 = TIM_CR1_CEN;
}

uint32_t timing_us(void) { return TIM2->CNT; }

void delay_us(uint16_t us)
{
    uint32_t start = timing_us();
    while ((uint32_t)(timing_us() - start) < us) { __NOP(); }
}

void delay_ms(uint16_t ms)
{
    while (ms--) delay_us(1000u);
}
