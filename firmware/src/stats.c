#include "stats.h"

static uint32_t counters[STAT_COUNT];

void stats_init(void) { stats_reset(); }

void stats_reset(void)
{
    for (uint8_t i = 0; i < STAT_COUNT; i++) counters[i] = 0;
}

void stats_inc(uint8_t which)
{
    if (which < STAT_COUNT) counters[which]++;
}

void stats_get(uint32_t *out)
{
    for (uint8_t i = 0; i < STAT_COUNT; i++) out[i] = counters[i];
}
