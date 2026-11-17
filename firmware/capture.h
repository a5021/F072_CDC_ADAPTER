#ifndef CAPTURE_H
#define CAPTURE_H

#include <stdint.h>

#define CAP_FLAG_SCL  0x01u
#define CAP_FLAG_SDA  0x02u
#define CAP_FLAG_EDGE 0x04u   /* 1 = rising, 0 = falling */

/* Events stored as: ts u32 LE, flags u8. */
void capture_init(void);
void capture_start(uint8_t channel_mask, uint16_t max_events);
void capture_stop(void);
void capture_mark(uint8_t label);
uint16_t capture_get(uint8_t *buf, uint16_t offset, uint16_t max_bytes,
                     uint16_t *total_events);
uint16_t capture_count(void);
uint16_t capture_dropped(void);

#endif
