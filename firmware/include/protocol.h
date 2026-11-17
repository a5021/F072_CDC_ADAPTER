#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#define FRAME_SOF        0xA6u
#define FRAME_MAX        512u
#define FRAME_OVERHEAD   7u        /* SOF,seq,cmd/len,len lo/hi,crc lo/hi */
#define PAYLOAD_MAX      (FRAME_MAX - FRAME_OVERHEAD)

/* Commands */
#define CMD_PING             0x01u
#define CMD_SET_SPEED        0x02u
#define CMD_SET_TURNAROUND   0x03u
#define CMD_SET_STRETCH_TO   0x04u
#define CMD_WRITE            0x10u
#define CMD_READ             0x11u
#define CMD_WRITE_READ       0x12u
#define CMD_REP_START        0x13u
#define CMD_SCRIPT_RUN       0x20u
#define CMD_SCRIPT_ABORT     0x21u
#define CMD_SCRIPT_GET       0x22u
#define CMD_CAPTURE_START    0x30u
#define CMD_CAPTURE_GET      0x31u
#define CMD_GPIO             0x32u
#define CMD_ADC_SAMPLE       0x33u
#define CMD_STATS            0x40u
#define CMD_BUS_CLEAR        0x41u
#define CMD_RESET            0x50u

/* Status codes */
#define ST_OK           0x00u
#define ST_NACK_ADDR    0x01u
#define ST_NACK_DATA    0x02u
#define ST_ARB_LOST     0x03u
#define ST_BUS_ERROR    0x04u
#define ST_TIMEOUT      0x05u
#define ST_BAD_PARAM    0x06u
#define ST_BUFFER_FULL  0x07u
#define ST_BAD_FRAME    0x08u
#define ST_ABORTED      0x09u

/* Script opcodes */
#define OP_WRITE         0x01u
#define OP_READ          0x02u
#define OP_DELAY         0x03u
#define OP_GPIO          0x04u
#define OP_TRIGGER       0x05u
#define OP_CAPTURE_MARK  0x06u
#define OP_REPEAT        0x07u

#define SCRIPT_MAX_STEPS  64u
#define SCRIPT_MAX_BYTES  512u
#define SCRIPT_RESULT_MAX 1024u
#define CAPTURE_MAX_EVENTS 1024u
#define CAPTURE_EVT_SIZE  5u

/* Stats indices */
enum {
    STAT_TXN = 0, STAT_NACK_ADDR, STAT_NACK_DATA, STAT_ARB_LOST,
    STAT_BUS_ERROR, STAT_TIMEOUT, STAT_OVERFLOW, STAT_COUNT
};

uint16_t crc16_ccitt(const uint8_t *data, uint16_t len);
void protocol_poll(void);

#endif
