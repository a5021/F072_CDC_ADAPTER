#include "timing.h"
#include "stm32f103_md.h"

static volatile uint32_t us_hi; // TIM2 update count, extended by the IRQ

void TIM2_IRQ_Handler(void)
{
    if (TIM2.SR & TIM_SR_UIF) {
        TIM2.SR = 0;
        ++us_hi;
    }
}

void timing_init(void)
{
    RCC.APB1ENR |= RCC_APB1ENR_TIM2EN;
    /* APB1 = 36 MHz, timer clock = x2 = 72 MHz; PSC 71 gives 1 MHz counts.
     * TIM2 is 16-bit, so the counter wraps every ~65.5 ms; the update IRQ
     * extends the count to 32 bits. */
    TIM2.PSC = 72u - 1u;
    TIM2.ARR = 0xFFFFu;
    TIM2.EGR = TIM_EGR_UG;
    TIM2.SR  = 0;
    TIM2.DIER = TIM_DIER_UIE;
    NVIC_EnableIRQ(TIM2_IRQn);
    TIM2.CR1 = TIM_CR1_CEN;
}

uint32_t timing_us(void)
{
    /* The update IRQ would otherwise be able to bump us_hi between the two
     * reads, turning the result into a 65.5 ms jump backwards. The PRIMASK
     * access is inline: the vendored core_cm3.c helper breaks LTO links. */
    uint32_t primask;
    __asm volatile("mrs %0, primask" : "=r"(primask));
    __asm volatile("cpsid i" ::: "memory");
    uint32_t hi = us_hi;
    uint16_t lo = TIM2.CNT;
    if (TIM2.SR & TIM_SR_UIF) ++hi; // update pending, IRQ not serviced yet
    if ((primask & 1u) == 0u) __asm volatile("cpsie i" ::: "memory");
    return (hi << 16) | lo;
}

void delay_us(uint16_t us)
{
    uint32_t start = timing_us();
    while ((uint32_t)(timing_us() - start) < us) { __NOP(); }
}

void delay_ms(uint16_t ms)
{
    while (ms--) delay_us(1000u);
}
