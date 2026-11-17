# Adapter protocol

## Framing

```
Host -> adapter: [SOF=0xA6][seq][cmd][len(u16 LE)][payload][crc16 LE]
Adapter -> host: [SOF=0xA6][seq][status][len(u16 LE)][payload][crc16 LE]
```

`crc16` is CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no final XOR,
computed over `seq .. end of payload` (everything except SOF and CRC).
Maximum frame size is 512 bytes, so payload is at most 505 bytes.

The host increments `seq` for each request. The adapter echoes it in the
response and expects the next request to carry `seq + 1`. A frame with a bad
CRC or an unexpected `seq` is dropped without a response; the host retries the
same `seq`.

Multi-byte fields are little-endian.

## Commands

| Code | Name | Request payload | Response payload |
|---|---|---|---|
| 0x01 | PING | — | version u16, caps u32, "F072I2C" |
| 0x02 | SET_SPEED | kHz u16 | actual kHz u16 |
| 0x03 | SET_TURNAROUND | us u16 (2..250) | actual us u16 |
| 0x04 | SET_STRETCH_TO | ms u16 | actual ms u16 |
| 0x10 | WRITE | addr u8, data[n] | — |
| 0x11 | READ | addr u8, count u16 | data[n], duration_us u32 |
| 0x12 | WRITE_READ | addr, wlen u8, wdata, rlen u16, gap_us u16 | data, t0 u32, t1 u32 |
| 0x13 | REP_START | addr, wlen u8, wdata, rlen u16 | data, t0 u32, t1 u32 |
| 0x20 | SCRIPT_RUN | script blob | — (drain with SCRIPT_GET) |
| 0x21 | SCRIPT_ABORT | — | — |
| 0x22 | SCRIPT_GET | offset u16 | result bytes |
| 0x30 | CAPTURE_START | channel mask u8, max_events u16 | — |
| 0x31 | CAPTURE_GET | offset u16 | bytes u16, total_events u16, events |
| 0x32 | GPIO | mask u16, value u16 | value u16 |
| 0x33 | ADC_SAMPLE | channel u8 | BAD_PARAM in v1 |
| 0x40 | STATS | subcmd u8 (0 get, 1 reset) | 7 x u32 |
| 0x41 | BUS_CLEAR | — | — |
| 0x50 | RESET | — | no response |

`WRITE_READ` performs write -> STOP -> `gap_us` -> START -> read. If `gap_us`
is zero, the value set with `SET_TURNAROUND` is used. `REP_START` uses a
repeated START with no gap.

Status codes: 0x00 OK, 0x01 NACK_ADDR, 0x02 NACK_DATA, 0x03 ARB_LOST,
0x04 BUS_ERROR, 0x05 TIMEOUT, 0x06 BAD_PARAM, 0x07 BUFFER_FULL, 0x08 BAD_FRAME,
0x09 ABORTED.

Stats counters: transactions, NACK address, NACK data, ARB_LOST, BUS_ERROR,
TIMEOUT, OVERFLOW.

## Scripts

Blob format:

```
[addr u8][nops u8][records...]
record = [opcode u8][len u8][payload]
```

| Opcode | Name | Payload |
|---|---|---|
| 0x01 | OP_WRITE | data bytes |
| 0x02 | OP_READ | count u16, next_gap_us u16 |
| 0x03 | OP_DELAY | us u16 |
| 0x04 | OP_GPIO | mask u16, value u16 |
| 0x05 | OP_TRIGGER | pulse_us u16 |
| 0x06 | OP_CAPTURE_MARK | label u8 |
| 0x07 | OP_REPEAT | count u16, block_start u16 |

`OP_REPEAT` repeats from `block_start` (a byte offset into the records area)
to the end of the script. No nesting. Total script time is limited to 5 s.

`SCRIPT_GET` returns result records:

```
[op_index u8][status u8][nbytes u16][timestamp_us u32][data[nbytes]]
```

Only the first pass through a repeated block produces result records; later
iterations still execute, so a 1000-iteration poll loop fits in the 1 KB
result buffer.

## Capture

Events are 5 bytes: `timestamp_us u32 LE`, `flags u8`.
Flags: bit0 SCL, bit1 SDA, bit2 rising edge (0 = falling), bit3 capture mark
(low 3 bits = label). Capture runs at 1 MHz resolution and stores up to 1024
events with a dropped-event counter.
