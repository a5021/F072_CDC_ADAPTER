#include "script.h"
#include "i2c_master.h"
#include "capture.h"
#include "board.h"
#include "timing.h"
#include "stats.h"
#include "string.h"

uint8_t script_blob[SCRIPT_MAX_BYTES];
uint16_t script_blob_len;
uint8_t script_result[SCRIPT_RESULT_MAX];
uint16_t script_result_len;
volatile uint8_t script_abort;

static uint8_t i2c_addr;
static uint16_t put_off;
static uint32_t t_start;

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }

static uint8_t put_result(uint8_t op_index, uint8_t status, uint32_t ts,
                          const uint8_t *data, uint16_t nbytes)
{
    uint16_t need = (uint16_t)(8u + nbytes);
    if ((uint16_t)(put_off + need) > SCRIPT_RESULT_MAX) return ST_BUFFER_FULL;
    uint8_t *p = script_result + put_off;
    p[0] = op_index; p[1] = status;
    p[2] = (uint8_t)(nbytes & 0xFFu); p[3] = (uint8_t)(nbytes >> 8);
    p[4] = (uint8_t)(ts & 0xFFu);
    p[5] = (uint8_t)((ts >> 8) & 0xFFu);
    p[6] = (uint8_t)((ts >> 16) & 0xFFu);
    p[7] = (uint8_t)((ts >> 24) & 0xFFu);
    if (nbytes) memcpy(p + 8, data, nbytes);
    put_off = (uint16_t)(put_off + need);
    script_result_len = put_off;
    return ST_OK;
}

uint8_t script_run(void)
{
    if (script_blob_len < 2u) return ST_BAD_PARAM;
    i2c_addr = script_blob[0];
    uint8_t declared = script_blob[1];
    if (declared == 0u || declared > SCRIPT_MAX_STEPS) return ST_BAD_PARAM;

    static uint8_t rx[256];
    put_off = 0;
    script_result_len = 0;
    t_start = timing_us();
    script_abort = 0;

    uint16_t pc = 2u;
    uint8_t steps = 0;
    uint16_t repeats_left = 0;
    uint16_t repeat_target = 0;
    uint8_t in_repeat = 0;
    uint8_t record = 0;

    while (pc < script_blob_len) {
        if (script_abort) return ST_ABORTED;
        if ((uint32_t)(timing_us() - t_start) > 5000000UL) return ST_TIMEOUT;

        if ((uint16_t)(pc + 1u) >= script_blob_len) break;
        uint8_t op = script_blob[pc++];
        uint8_t len = script_blob[pc++];
        if ((uint16_t)(pc + len) > script_blob_len) return ST_BAD_PARAM;
        const uint8_t *p = script_blob + pc;
        pc = (uint16_t)(pc + len);
        uint32_t ts = timing_us() - t_start;
        uint8_t status = ST_OK;
        uint16_t nbytes = 0;
        uint8_t store = in_repeat ? 0u : 1u;

        switch (op) {
        case OP_WRITE:
            if (len == 0u) return ST_BAD_PARAM;
            status = i2c_write(i2c_addr, p, len, I2C_FLAG_STOP);
            break;
        case OP_READ:
            if (len != 5u) return ST_BAD_PARAM;
            {
                uint16_t n = rd16(p);
                if (n == 0u || n > sizeof(rx)) return ST_BAD_PARAM;
                status = i2c_read(i2c_addr, rx, n, I2C_FLAG_STOP);
                nbytes = (status == ST_OK) ? n : 0u;
            }
            break;
        case OP_DELAY:
            if (len != 2u) return ST_BAD_PARAM;
            delay_us(rd16(p));
            break;
        case OP_GPIO:
            if (len != 4u) return ST_BAD_PARAM;
            board_gpio_set(rd16(p), rd16(p + 2u));
            break;
        case OP_TRIGGER:
            if (len != 2u) return ST_BAD_PARAM;
            board_trigger_pulse(rd16(p));
            break;
        case OP_CAPTURE_MARK:
            if (len != 1u) return ST_BAD_PARAM;
            capture_mark(p[0]);
            break;
        case OP_REPEAT:
            if (len != 4u || in_repeat) return ST_BAD_PARAM;
            repeats_left = rd16(p);
            repeat_target = rd16(p + 2u);
            if (repeats_left == 0u || repeat_target < 2u ||
                repeat_target >= script_blob_len) return ST_BAD_PARAM;
            store = 0u;
            break;
        default:
            return ST_BAD_PARAM;
        }

        if (store) {
            uint8_t st = put_result(record, status, ts, rx, nbytes);
            if (st != ST_OK) return st;
            record++;
            if (++steps > SCRIPT_MAX_STEPS) return ST_BUFFER_FULL;
        }
        if (op == OP_READ) {
            uint16_t gap = rd16(p + 2u);
            if (gap) delay_us(gap);
        }

        if (pc >= script_blob_len && repeats_left) {
            repeats_left--;
            pc = repeat_target;
            in_repeat = 1;
        }
    }
    return ST_OK;
}

uint16_t script_get(uint8_t *buf, uint16_t offset, uint16_t max_bytes)
{
    if (offset >= script_result_len) return 0u;
    uint16_t n = (uint16_t)(script_result_len - offset);
    if (n > max_bytes) n = max_bytes;
    memcpy(buf, script_result + offset, n);
    return n;
}
