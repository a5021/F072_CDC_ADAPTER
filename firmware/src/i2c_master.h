#ifndef I2C_MASTER_H
#define I2C_MASTER_H

#include <stdint.h>
#include "protocol.h"

void i2c_init(void);
void i2c_set_speed_khz(uint16_t khz, uint16_t *actual);
uint16_t i2c_get_speed_khz(void);

#define I2C_FLAG_STOP   0x01u   /* generate STOP at end of transfer */
#define I2C_FLAG_RESTART 0x02u  /* repeated START: kept for API symmetry */

/* Returns ST_OK or one of ST_* errors. */
uint8_t i2c_write(uint8_t addr, const uint8_t *data, uint16_t len, uint8_t flags);
uint8_t i2c_read(uint8_t addr, uint8_t *data, uint16_t len, uint8_t flags);

/* Bit-bang bus recovery: 9x SCL + STOP. */
void i2c_bus_clear(void);
uint8_t i2c_is_busy(void);

void i2c_set_stretch_timeout_ms(uint16_t ms);
uint16_t i2c_get_stretch_timeout_ms(void);

#endif
