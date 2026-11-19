#include "i2c_master.h"
#include "timing.h"
#include "stats.h"
#include "hw_defs.h"
#include "stm32f103_md.h"

#define PCLK1_MHZ        36u // HCLK 72 MHz / PPRE1_DIV2
#define I2C_ERROR_MASK   (I2C_SR1_BERR | I2C_SR1_ARLO | I2C_SR1_AF)

static uint16_t current_khz = 100u;
static uint16_t stretch_timeout_ms = 100u;

static uint8_t check_error(uint32_t sr1, unsigned transferred)
{
    if (sr1 & I2C_SR1_ARLO) { stats_inc(STAT_ARB_LOST); return ST_ARB_LOST; }
    if (sr1 & I2C_SR1_BERR) { stats_inc(STAT_BUS_ERROR); return ST_BUS_ERROR; }
    if (sr1 & I2C_SR1_AF) {
        if (transferred == 0u) { stats_inc(STAT_NACK_ADDR); return ST_NACK_ADDR; }
        stats_inc(STAT_NACK_DATA); return ST_NACK_DATA;
    }
    return ST_OK;
}

/* F1 SR1 error bits are rc_w0: they must be cleared by writing 0, or every
 * later wait sees the stale flag and fails instantly. */
static void clear_errors(void) { I2C1.SR1 &= ~I2C_ERROR_MASK; }

static void i2c_stop(void);
static void i2c_recover(void);

static uint8_t abort_xfer(uint8_t st)
{
    I2C1.CR1 &= ~I2C_CR1_START; // a pending START would block STOP
    i2c_stop();
    i2c_recover();
    return st;
}

static uint8_t wait_flag(uint32_t flag, unsigned transferred, uint32_t *sr1_out)
{
    uint32_t start = timing_us();
    uint32_t limit = (uint32_t)stretch_timeout_ms * 1000u;
    for (;;) {
        uint32_t sr1 = I2C1.SR1;
        if (sr1 & I2C_ERROR_MASK) {
            if (sr1_out) *sr1_out = sr1;
            uint8_t st = check_error(sr1, transferred);
            clear_errors();
            return st;
        }
        if (sr1 & flag) {
            if (sr1_out) *sr1_out = sr1;
            return ST_OK;
        }
        if ((uint32_t)(timing_us() - start) > limit) {
            stats_inc(STAT_TIMEOUT);
            return ST_TIMEOUT;
        }
    }
}

static uint8_t i2c_start(uint8_t addr, uint8_t read, unsigned transferred)
{
    uint32_t dummy;
    I2C1.CR1 |= I2C_CR1_START; // generate START, then wait for SB
    uint8_t st = wait_flag(I2C_SR1_SB, transferred, &dummy);
    if (st != ST_OK) return st;
    I2C1.DR = (uint8_t)((addr << 1) | (read ? 1u : 0u));
    st = wait_flag(I2C_SR1_ADDR, transferred, &dummy);
    if (st != ST_OK) return st;
    (void)I2C1.SR1; // clear ADDR by reading SR1 then SR2
    (void)I2C1.SR2;
    return ST_OK;
}

static void i2c_stop(void)
{
    I2C1.CR1 |= I2C_CR1_STOP;
    uint32_t start = timing_us();
    /* Wait for the hardware to clear STOP as well as BUSY. If STOP stays
     * latched the next START is ignored (observed as ARB_LOST/TIMEOUT). */
    while (((I2C1.CR1 & I2C_CR1_STOP) != 0u || (I2C1.SR2 & I2C_SR2_BUSY) != 0u) &&
           (uint32_t)(timing_us() - start) < 20000u) {}
    if ((I2C1.CR1 & I2C_CR1_STOP) != 0u) {
        I2C1.CR1 = 0;                         // release the stuck state machine
        I2C1.CR1 = I2C_CR1_PE | I2C_CR1_ACK;
    }
}

void i2c_init(void)
{
    RCC.APB1ENR |= RCC_APB1ENR_I2C1EN;
    I2C1.CR1 = I2C_CR1_SWRST;
    I2C1.CR1 = 0;
    I2C1.CR2 = PCLK1_MHZ;
    I2C1.OAR1 = 0;
    I2C1.OAR2 = 0;
    i2c_set_speed_khz(100u, 0);
    I2C1.CR1 |= I2C_CR1_PE | I2C_CR1_ACK;
}

void i2c_set_speed_khz(uint16_t khz, uint16_t *actual)
{
    if (khz < 10u) khz = 10u;
    if (khz > 400u) khz = 400u;

    uint8_t fast = khz > 100u;
    uint32_t divisor = (fast ? 3u : 2u) * (uint32_t)khz * 1000u;
    uint32_t ccr = (uint32_t)(PCLK1_MHZ * 1000000UL / divisor);
    if (ccr == 0u) ccr = 1u;
    if (ccr > 0xFFFu) ccr = 0xFFFu;

    uint32_t cr1 = I2C1.CR1 & ~I2C_CR1_PE;
    I2C1.CR1 = cr1;
    I2C1.CCR = (uint16_t)(ccr | (fast ? I2C_CCR_FS : 0u));
    I2C1.TRISE = fast ? 13u : (PCLK1_MHZ + 1u);
    I2C1.CR1 = cr1 | I2C_CR1_PE;

    uint16_t act = (uint16_t)((uint32_t)(PCLK1_MHZ * 1000000UL) /
                              (ccr * (fast ? 3u : 2u)) / 1000u);
    current_khz = act;
    if (actual) *actual = act;
}

uint16_t i2c_get_speed_khz(void) { return current_khz; }

/* The F1 I2C state machine can wedge after an aborted transfer (BUSY set,
 * SCL held low, SB pending). A full SWRST cycle is the only reliable way out. */
static void i2c_recover(void)
{
    I2C1.CR1 = I2C_CR1_SWRST;
    I2C1.CR1 = 0;
    I2C1.CR2 = PCLK1_MHZ;
    I2C1.OAR1 = 0;
    i2c_set_speed_khz(current_khz, 0);
    I2C1.CR1 |= I2C_CR1_PE | I2C_CR1_ACK;
    I2C1.SR1 = 0;
}

void i2c_set_stretch_timeout_ms(uint16_t ms) { stretch_timeout_ms = ms ? ms : 1u; }
uint16_t i2c_get_stretch_timeout_ms(void) { return stretch_timeout_ms; }

uint8_t i2c_write(uint8_t addr, const uint8_t *data, uint16_t len, uint8_t flags)
{
    if ((I2C1.SR2 & I2C_SR2_BUSY) != 0u) i2c_recover();
    uint8_t st = i2c_start(addr, 0, 0);
    if (st != ST_OK) return abort_xfer(st);

    for (uint16_t i = 0; i < len; i++) {
        st = wait_flag(I2C_SR1_TXE, i + 1u, 0);
        if (st != ST_OK) return abort_xfer(st);
        I2C1.DR = data[i];
    }
    st = wait_flag(I2C_SR1_BTF, len, 0);
    if (st != ST_OK) return abort_xfer(st);
    if (flags & I2C_FLAG_STOP) i2c_stop();
    stats_inc(STAT_TXN);
    return ST_OK;
}

uint8_t i2c_read(uint8_t addr, uint8_t *data, uint16_t len, uint8_t flags)
{
    if ((I2C1.SR2 & I2C_SR2_BUSY) != 0u && !(flags & I2C_FLAG_RESTART)) i2c_recover();

    I2C1.CR1 |= I2C_CR1_ACK;
    uint8_t st = i2c_start(addr, 1, 0);
    if (st != ST_OK) return abort_xfer(st);

    for (uint16_t i = 0; i < len; i++) {
        if (i + 1u == len) {
            I2C1.CR1 &= ~I2C_CR1_ACK;
            if (flags & I2C_FLAG_STOP) I2C1.CR1 |= I2C_CR1_STOP;
        }
        st = wait_flag(I2C_SR1_RXNE, i, 0);
        if (st != ST_OK) return abort_xfer(st);
        data[i] = (uint8_t)I2C1.DR;
    }
    /* STOP was already requested before the last byte; with RESTART the bus
     * stays owned until the next i2c_start(). */
    stats_inc(STAT_TXN);
    return ST_OK;
}

void i2c_bus_clear(void)
{
    uint8_t was_enabled = (I2C1.CR1 & I2C_CR1_PE) != 0u;
    if (was_enabled) I2C1.CR1 &= ~I2C_CR1_PE;

    /* PB6/PB7: general-purpose open-drain output. */
    uint32_t crl = GPIOB.CRL;
    GPIOB.CRL = (crl & ~((0xFu << 24u) | (0xFu << 28u))) |
                 (0x5u << 24u) | (0x5u << 28u);
    GPIOB.BSRR = GPIO_PIN_6 | GPIO_PIN_7;
    delay_us(10u);

    for (uint8_t i = 0; i < 9u; i++) {
        if ((GPIOB.IDR & GPIO_PIN_7) == 0u) {
            GPIOB.BRR = GPIO_PIN_6; delay_us(5u);
            GPIOB.BSRR = GPIO_PIN_6; delay_us(5u);
        }
    }
    GPIOB.BRR = GPIO_PIN_7; delay_us(5u);
    GPIOB.BSRR = GPIO_PIN_6; delay_us(5u);
    GPIOB.BSRR = GPIO_PIN_7; delay_us(5u);

    GPIOB.CRL = (GPIOB.CRL & ~((0xFu << 24u) | (0xFu << 28u))) |
                 (0xDu << 24u) | (0xDu << 28u);
    GPIOB.BSRR = GPIO_PIN_6 | GPIO_PIN_7;
    if (was_enabled) I2C1.CR1 |= I2C_CR1_PE;
}

uint8_t i2c_is_busy(void) { return (I2C1.SR2 & I2C_SR2_BUSY) != 0u; }
