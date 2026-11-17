#include "protocol.h"
#include "usb_cdc.h"
#include "i2c_master.h"
#include "capture.h"
#include "board.h"
#include "script.h"
#include "stats.h"
#include "timing.h"
#include "string.h"
#include "stm32f0xx.h"

#define FW_VERSION 0x0100u
#define CAPS       0x00000007u   /* capture, script, clock-stretch */

static uint8_t frame[FRAME_MAX];
static uint16_t frame_len;
static uint8_t expected_seq;
static uint16_t turnaround_us = 15u;

uint16_t crc16_ccitt(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFu;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (uint8_t b = 0; b < 8u; b++)
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                  : (uint16_t)(crc << 1);
    }
    return crc;
}

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }

static void send_frame(uint8_t seq, uint8_t status, const uint8_t *payload, uint16_t len)
{
    if (len > PAYLOAD_MAX) len = PAYLOAD_MAX;
    uint8_t out[FRAME_MAX];
    out[0] = FRAME_SOF;
    out[1] = seq;
    out[2] = status;
    put16(out + 3, len);
    if (len) memcpy(out + 5, payload, len);
    uint16_t crc = crc16_ccitt(out + 1, (uint16_t)(5u + len));
    put16(out + 5u + len, crc);
    usb_cdc_write(out, (uint16_t)(7u + len));
}

static uint8_t dispatch(uint8_t cmd, const uint8_t *p, uint16_t len,
                        uint8_t *resp, uint16_t *resp_len)
{
    uint16_t n = 0;
    switch (cmd) {
    case CMD_PING:
        put16(resp, FW_VERSION); put32(resp + 2, CAPS);
        memcpy(resp + 6, "F072I2C", 7); n = 13;
        break;

    case CMD_SET_SPEED:
        if (len != 2u) return ST_BAD_PARAM;
        { uint16_t actual; i2c_set_speed_khz(get16(p), &actual); put16(resp, actual); n = 2; }
        break;

    case CMD_SET_TURNAROUND:
        if (len != 2u) return ST_BAD_PARAM;
        { uint16_t v = get16(p); if (v < 2u || v > 250u) return ST_BAD_PARAM;
          turnaround_us = v; put16(resp, v); n = 2; }
        break;

    case CMD_SET_STRETCH_TO:
        if (len != 2u) return ST_BAD_PARAM;
        i2c_set_stretch_timeout_ms(get16(p));
        put16(resp, i2c_get_stretch_timeout_ms()); n = 2;
        break;

    case CMD_WRITE:
        if (len < 2u) return ST_BAD_PARAM;
        return i2c_write(p[0], p + 1, (uint16_t)(len - 1u), I2C_FLAG_STOP);

    case CMD_READ:
        if (len != 3u) return ST_BAD_PARAM;
        {
            uint16_t count = get16(p + 1);
            if (count == 0u || count > PAYLOAD_MAX - 4u) return ST_BAD_PARAM;
            uint32_t t0 = timing_us();
            uint8_t st = i2c_read(p[0], resp, count, I2C_FLAG_STOP);
            uint32_t t1 = timing_us();
            if (st != ST_OK) return st;
            put32(resp + count, t1 - t0);
            n = (uint16_t)(count + 4u);
        }
        break;

    case CMD_WRITE_READ:
    case CMD_REP_START:
        if (len < 6u) return ST_BAD_PARAM;
        {
            uint8_t addr = p[0];
            uint8_t wlen = p[1];
            if ((uint16_t)(2u + wlen + 4u) != len) return ST_BAD_PARAM;
            uint16_t rlen = get16(p + 2u + wlen);
            uint16_t gap = (cmd == CMD_WRITE_READ) ? get16(p + 4u + wlen) : 0u;
            if (rlen == 0u || rlen > PAYLOAD_MAX - 8u) return ST_BAD_PARAM;
            if (gap == 0u && cmd == CMD_WRITE_READ) gap = turnaround_us;
            uint32_t t0 = timing_us();
            uint8_t st = i2c_write(addr, p + 2u, wlen, I2C_FLAG_STOP);
            if (st != ST_OK) return st;
            if (gap) delay_us(gap);
            st = i2c_read(addr, resp, rlen, I2C_FLAG_STOP);
            uint32_t t1 = timing_us();
            if (st != ST_OK) return st;
            put32(resp + rlen, t0);
            put32(resp + rlen + 4u, t1);
            n = (uint16_t)(rlen + 8u);
        }
        break;

    case CMD_SCRIPT_RUN:
        if (len > SCRIPT_MAX_BYTES) return ST_BAD_PARAM;
        memcpy(script_blob, p, len);
        script_blob_len = len;
        { uint8_t st = script_run(); if (st != ST_OK) return st; }
        break;

    case CMD_SCRIPT_ABORT:
        script_abort = 1u;
        break;

    case CMD_SCRIPT_GET:
        if (len != 2u) return ST_BAD_PARAM;
        n = script_get(resp, get16(p), PAYLOAD_MAX);
        break;

    case CMD_CAPTURE_START:
        if (len != 3u) return ST_BAD_PARAM;
        capture_start(p[0], get16(p + 1));
        break;

    case CMD_CAPTURE_GET:
        if (len != 2u) return ST_BAD_PARAM;
        {
            uint16_t total;
            put16(resp, (uint16_t)(PAYLOAD_MAX - 4u));
            put16(resp + 2, 0u);
            n = capture_get(resp + 4, get16(p), (uint16_t)(PAYLOAD_MAX - 4u), &total);
            put16(resp + 2, total);
            n = (uint16_t)(n + 4u);
        }
        break;

    case CMD_GPIO:
        if (len != 4u) return ST_BAD_PARAM;
        board_gpio_set(get16(p), get16(p + 2u));
        put16(resp, board_gpio_get(get16(p))); n = 2;
        break;

    case CMD_ADC_SAMPLE:
        return ST_BAD_PARAM;   /* v1: ADC out of scope */

    case CMD_STATS:
        if (len != 1u) return ST_BAD_PARAM;
        if (p[0] == 1u) { stats_reset(); break; }
        if (p[0] != 0u) return ST_BAD_PARAM;
        { uint32_t counters[STAT_COUNT]; stats_get(counters);
          for (uint8_t i = 0; i < STAT_COUNT; i++) put32(resp + i * 4u, counters[i]);
          n = (uint16_t)(STAT_COUNT * 4u); }
        break;

    case CMD_BUS_CLEAR:
        i2c_bus_clear();
        break;

    case CMD_RESET:
        SCB->AIRCR = 0x05FA0004u;   /* VECTKEY | SYSRESETREQ */
        break;

    default:
        return ST_BAD_PARAM;
    }
    *resp_len = n;
    return ST_OK;
}

void protocol_poll(void)
{
    static uint8_t byte;
    while (usb_cdc_available()) {
        usb_cdc_read(&byte, 1);

        if (frame_len == 0u) {
            if (byte != FRAME_SOF) continue;
            frame[frame_len++] = byte;
            continue;
        }
        frame[frame_len++] = byte;

        if (frame_len >= 5u) {
            uint16_t len = (uint16_t)(frame[3] | ((uint16_t)frame[4] << 8));
            if (len > PAYLOAD_MAX) { frame_len = 0; continue; }
            uint16_t want = (uint16_t)(5u + len + 2u);
            if (frame_len < want) continue;

            uint16_t crc = (uint16_t)(frame[want - 2u] | ((uint16_t)frame[want - 1u] << 8));
            uint16_t calc = crc16_ccitt(frame + 1, (uint16_t)(5u + len));
            uint8_t seq = frame[1];
            uint8_t cmd = frame[2];
            frame_len = 0;

            if (crc != calc || seq != expected_seq) continue;
            expected_seq = (uint8_t)(seq + 1u);

            static uint8_t resp[PAYLOAD_MAX];
            uint16_t resp_len = 0;
            uint8_t status = dispatch(cmd, frame + 5, len, resp, &resp_len);
            send_frame(seq, status, resp, resp_len);
        }
    }
}
