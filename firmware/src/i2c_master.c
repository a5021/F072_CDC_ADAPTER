#include "i2c_master.h"
#include "timing.h"
#include "stats.h"
#include "hw_defs.h"
#include "stm32f0xx.h"

#define I2C_TIMEOUT_MS_DEFAULT 100u

static uint16_t current_khz = 100u;
static uint16_t stretch_timeout_ms = I2C_TIMEOUT_MS_DEFAULT;

static uint32_t err_flags(void)
{
    return I2C_ISR_BERR | I2C_ISR_ARLO | I2C_ISR_OVR | I2C_ISR_NACKF;
}

static uint8_t map_error(uint32_t isr, unsigned transferred)
{
    if (isr & I2C_ISR_ARLO) { stats_inc(STAT_ARB_LOST); return ST_ARB_LOST; }
    if (isr & I2C_ISR_BERR) { stats_inc(STAT_BUS_ERROR); return ST_BUS_ERROR; }
    if (isr & I2C_ISR_NACKF) {
        if (transferred == 0u) { stats_inc(STAT_NACK_ADDR); return ST_NACK_ADDR; }
        stats_inc(STAT_NACK_DATA); return ST_NACK_DATA;
    }
    if (isr & I2C_ISR_OVR) { stats_inc(STAT_OVERFLOW); return ST_BUS_ERROR; }
    return ST_OK;
}

static uint8_t wait_for(uint32_t flag, unsigned transferred, uint32_t *isr_out)
{
    uint32_t start = timing_us();
    uint32_t limit = (uint32_t)stretch_timeout_ms * 1000u;
    for (;;) {
        uint32_t isr = I2C1->ISR;
        if (isr & err_flags()) {
            if (isr_out) *isr_out = isr;
            return map_error(isr, transferred);
        }
        if (isr & flag) {
            if (isr_out) *isr_out = isr;
            return ST_OK;
        }
        if ((uint32_t)(timing_us() - start) > limit) {
            stats_inc(STAT_TIMEOUT);
            return ST_TIMEOUT;
        }
    }
}

static void force_stop(void)
{
    I2C1->ICR = I2C_ICR_NACKCF | I2C_ICR_STOPCF | I2C_ICR_BERRCF |
               I2C_ICR_ARLOCF | I2C_ICR_OVRCF;
    if (I2C1->ISR & I2C_ISR_BUSY) {
        I2C1->CR2 |= I2C_CR2_STOP;
        uint32_t start = timing_us();
        while ((I2C1->ISR & I2C_ISR_STOPF) == 0u) {
            if ((uint32_t)(timing_us() - start) > 20000u) break;
        }
        I2C1->ICR = I2C_ICR_STOPCF;
    }
}

void i2c_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;
    I2C1->CR1 = I2C_CR1_SWRST;
    I2C1->CR1 = 0;
    I2C1->TIMINGR = 0x00301C3Eu;    /* 400 kHz placeholder, overwritten below */
    I2C1->CR1 = I2C_CR1_PE;
    i2c_set_speed_khz(100u, 0);
}

void i2c_set_speed_khz(uint16_t khz, uint16_t *actual)
{
    if (khz < 10u) khz = 10u;
    if (khz > 400u) khz = 400u;

    uint64_t half_x1000 = 500000000ULL / khz;   /* half period, ns*1000 */
    uint64_t tick_x1000 = 20833ULL;             /* 1/48 MHz, ns*1000 */
    uint32_t presc;
    for (presc = 0; presc < 16u; presc++) {
        if (half_x1000 / tick_x1000 <= 256u) break;
        tick_x1000 *= 2u;
    }
    if (presc > 15u) { presc = 15u; tick_x1000 = 20833ULL * 16384ULL; }

    uint32_t ticks = (uint32_t)((half_x1000 + tick_x1000 / 2u) / tick_x1000);
    if (ticks == 0u) ticks = 1u;
    if (ticks > 256u) ticks = 256u;

    uint32_t sdadel = (khz <= 100u) ? 2u : 0u;
    uint32_t scldel = 2u;
    uint32_t timing = ((uint32_t)presc << 28) | (scldel << 20) |
                      (sdadel << 16) | ((ticks - 1u) << 8) | (ticks - 1u);

    uint8_t was_enabled = (I2C1->CR1 & I2C_CR1_PE) != 0u;
    if (was_enabled) I2C1->CR1 &= ~I2C_CR1_PE;
    I2C1->TIMINGR = timing;
    if (was_enabled) I2C1->CR1 |= I2C_CR1_PE;

    uint64_t period_x1000 = 2u * ticks * tick_x1000;
    uint16_t act = (uint16_t)(1000000000ULL / period_x1000);
    current_khz = act;
    if (actual) *actual = act;
}

uint16_t i2c_get_speed_khz(void) { return current_khz; }

void i2c_set_stretch_timeout_ms(uint16_t ms)
{
    stretch_timeout_ms = ms ? ms : 1u;
}

uint16_t i2c_get_stretch_timeout_ms(void) { return stretch_timeout_ms; }

uint8_t i2c_write(uint8_t addr, const uint8_t *data, uint16_t len, uint8_t flags)
{
    if (I2C1->ISR & I2C_ISR_BUSY) return ST_BUS_ERROR;
    I2C1->ICR = I2C_ICR_NACKCF | I2C_ICR_STOPCF | I2C_ICR_BERRCF |
               I2C_ICR_ARLOCF | I2C_ICR_OVRCF;

    uint16_t off = 0u;
    uint8_t first = 1u;
    while (off < len) {
        uint16_t remaining = (uint16_t)(len - off);
        uint16_t chunk = remaining > 255u ? 255u : remaining;
        uint32_t cr2 = ((uint32_t)addr << 1) | ((uint32_t)chunk << 16);
        if (remaining <= 255u) {
            if (flags & I2C_FLAG_STOP) cr2 |= I2C_CR2_AUTOEND;
        } else {
            cr2 |= I2C_CR2_RELOAD;
        }
        if (first) { cr2 |= I2C_CR2_START; first = 0u; }
        I2C1->CR2 = cr2;

        for (uint16_t i = 0; i < chunk; i++) {
            uint32_t dummy;
            uint8_t st = wait_for(I2C_ISR_TXE, off + i, &dummy);
            if (st != ST_OK) { force_stop(); return st; }
            I2C1->TXDR = data[off + i];
        }
        off = (uint16_t)(off + chunk);
        uint32_t dummy;
        if (remaining > 255u) {
            uint8_t st = wait_for(I2C_ISR_TCR, off, &dummy);
            if (st != ST_OK) { force_stop(); return st; }
        } else if (flags & I2C_FLAG_STOP) {
            uint8_t st = wait_for(I2C_ISR_STOPF, off, &dummy);
            if (st != ST_OK) { force_stop(); return st; }
            I2C1->ICR = I2C_ICR_STOPCF;
        } else {
            uint8_t st = wait_for(I2C_ISR_TC, off, &dummy);
            if (st != ST_OK) { force_stop(); return st; }
        }
    }
    stats_inc(STAT_TXN);
    return ST_OK;
}

uint8_t i2c_read(uint8_t addr, uint8_t *data, uint16_t len, uint8_t flags)
{
    if (I2C1->ISR & I2C_ISR_BUSY) {
        /* Allow a pending write (repeated START) to continue. */
        if (!(flags & I2C_FLAG_RESTART)) return ST_BUS_ERROR;
    }
    I2C1->ICR = I2C_ICR_NACKCF | I2C_ICR_STOPCF | I2C_ICR_BERRCF |
               I2C_ICR_ARLOCF | I2C_ICR_OVRCF;

    uint16_t off = 0u;
    uint8_t first = 1u;
    while (off < len) {
        uint16_t remaining = (uint16_t)(len - off);
        uint16_t chunk = remaining > 255u ? 255u : remaining;
        uint32_t cr2 = ((uint32_t)addr << 1) | I2C_CR2_RD_WRN |
                       ((uint32_t)chunk << 16);
        if (remaining <= 255u) {
            if (flags & I2C_FLAG_STOP) cr2 |= I2C_CR2_AUTOEND;
        } else {
            cr2 |= I2C_CR2_RELOAD;
        }
        if (first) { cr2 |= I2C_CR2_START; first = 0u; }
        I2C1->CR2 = cr2;

        for (uint16_t i = 0; i < chunk; i++) {
            uint32_t dummy;
            uint8_t st = wait_for(I2C_ISR_RXNE, off + i, &dummy);
            if (st != ST_OK) { force_stop(); return st; }
            data[off + i] = (uint8_t)I2C1->RXDR;
        }
        off = (uint16_t)(off + chunk);
        uint32_t dummy;
        if (remaining > 255u) {
            uint8_t st = wait_for(I2C_ISR_TCR, off, &dummy);
            if (st != ST_OK) { force_stop(); return st; }
        } else if (flags & I2C_FLAG_STOP) {
            uint8_t st = wait_for(I2C_ISR_STOPF, off, &dummy);
            if (st != ST_OK) { force_stop(); return st; }
            I2C1->ICR = I2C_ICR_STOPCF;
        }
    }
    stats_inc(STAT_TXN);
    return ST_OK;
}

void i2c_bus_clear(void)
{
    uint8_t was_enabled = (I2C1->CR1 & I2C_CR1_PE) != 0u;
    if (was_enabled) I2C1->CR1 &= ~I2C_CR1_PE;

    /* Bit-bang PB6 (SCL) / PB7 (SDA) as open-drain. */
    GPIOB->MODER &= ~(GPIO_MODER_MODER6 | GPIO_MODER_MODER7);
    GPIOB->MODER |= GPIO_MODER_MODER6_0 | GPIO_MODER_MODER7_0;
    GPIOB->OTYPER |= GPIO_OTYPER_OT_6 | GPIO_OTYPER_OT_7;
    GPIOB->BSRR = GPIO_PIN_6 | GPIO_PIN_7;
    delay_us(10u);

    for (uint8_t i = 0; i < 9u; i++) {
        if ((GPIOB->IDR & GPIO_PIN_7) == 0u) {
            GPIOB->BRR = GPIO_PIN_6; delay_us(5u);
            GPIOB->BSRR = GPIO_PIN_6; delay_us(5u);
        }
    }
    /* STOP: SCL high, SDA 0 -> 1 */
    GPIOB->BRR = GPIO_PIN_7; delay_us(5u);
    GPIOB->BSRR = GPIO_PIN_6; delay_us(5u);
    GPIOB->BSRR = GPIO_PIN_7; delay_us(5u);

    GPIOB->MODER &= ~(GPIO_MODER_MODER6 | GPIO_MODER_MODER7);
    GPIOB->MODER |= GPIO_MODER_MODER6_1 | GPIO_MODER_MODER7_1;
    if (was_enabled) I2C1->CR1 |= I2C_CR1_PE;
    I2C1->ICR = I2C_ICR_NACKCF | I2C_ICR_STOPCF | I2C_ICR_BERRCF |
               I2C_ICR_ARLOCF | I2C_ICR_OVRCF;
}

uint8_t i2c_is_busy(void) { return (I2C1->ISR & I2C_ISR_BUSY) != 0u; }
