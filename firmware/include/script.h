#ifndef SCRIPT_H
#define SCRIPT_H

#include <stdint.h>
#include "protocol.h"

extern uint8_t script_blob[SCRIPT_MAX_BYTES];
extern uint16_t script_blob_len;
extern uint8_t script_result[SCRIPT_RESULT_MAX];
extern uint16_t script_result_len;
extern volatile uint8_t script_abort;

uint8_t script_run(void);
uint16_t script_get(uint8_t *buf, uint16_t offset, uint16_t max_bytes);

#endif
