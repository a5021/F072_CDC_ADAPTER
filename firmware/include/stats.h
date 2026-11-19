#ifndef STATS_H
#define STATS_H

#include <stdint.h>
#include "protocol.h"

void stats_init(void);
void stats_inc(uint8_t which);
void stats_reset(void);
void stats_get(uint32_t *out);   /* STAT_COUNT words */

#endif
