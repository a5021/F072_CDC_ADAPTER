"""Host driver for the F072_CDC_ADAPTER USB-to-I2C adapter.

Drop-in replacement for owbridge_demo.Ch341I2C at the transport level.
No third-party dependency except pyserial.
"""
import struct
import time

import serial

SOF = 0xA6
FRAME_MAX = 512
PAYLOAD_MAX = FRAME_MAX - 7

CMD_PING = 0x01
CMD_SET_SPEED = 0x02
CMD_SET_TURNAROUND = 0x03
CMD_SET_STRETCH_TO = 0x04
CMD_WRITE = 0x10
CMD_READ = 0x11
CMD_WRITE_READ = 0x12
CMD_REP_START = 0x13
CMD_SCRIPT_RUN = 0x20
CMD_SCRIPT_ABORT = 0x21
CMD_SCRIPT_GET = 0x22
CMD_CAPTURE_START = 0x30
CMD_CAPTURE_GET = 0x31
CMD_GPIO = 0x32
CMD_ADC_SAMPLE = 0x33
CMD_STATS = 0x40
CMD_BUS_CLEAR = 0x41
CMD_RESET = 0x50

OK = 0x00
ERRORS = {
    0x01: "NACK_ADDR", 0x02: "NACK_DATA", 0x03: "ARB_LOST",
    0x04: "BUS_ERROR", 0x05: "TIMEOUT", 0x06: "BAD_PARAM",
    0x07: "BUFFER_FULL", 0x08: "BAD_FRAME", 0x09: "ABORTED",
}
STAT_NAMES = ("txn", "nack_addr", "nack_data", "arb_lost",
              "bus_error", "timeout", "overflow")


def crc16_ccitt(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def build_frame(seq, cmd, payload=b""):
    body = bytes([seq, cmd]) + struct.pack("<H", len(payload)) + payload
    return bytes([SOF]) + body + struct.pack("<H", crc16_ccitt(body))


def parse_frame(raw):
    if len(raw) < 7 or raw[0] != SOF:
        raise ValueError("bad SOF")
    seq, status, length = raw[1], raw[2], struct.unpack_from("<H", raw, 3)[0]
    if len(raw) != 7 + length:
        raise ValueError("bad length")
    crc = struct.unpack_from("<H", raw, 5 + length)[0]
    if crc != crc16_ccitt(raw[1:5 + length]):
        raise ValueError("bad CRC")
    return seq, status, raw[5:5 + length]


OP_WRITE = 0x01
OP_READ = 0x02
OP_DELAY = 0x03
OP_GPIO = 0x04
OP_TRIGGER = 0x05
OP_CAPTURE_MARK = 0x06
OP_REPEAT = 0x07


def build_script(addr, ops):
    """Encode an operation list into a SCRIPT_RUN blob.

    ops: list of tuples
      ("write", data_bytes)
      ("read", byte_count, next_gap_us)
      ("delay", us)
      ("gpio", mask, value)
      ("trigger", us)
      ("mark", label)
      ("repeat", count, step_index)   -- repeat from step_index to end
    """
    records = []
    for op in ops:
        kind = op[0]
        if kind == "write":
            payload = bytes(op[1])
            records.append(bytes([OP_WRITE, len(payload)]) + payload)
        elif kind == "read":
            records.append(bytes([OP_READ, 5]) +
                           struct.pack("<HH", op[1], op[2]))
        elif kind == "delay":
            records.append(bytes([OP_DELAY, 2]) + struct.pack("<H", op[1]))
        elif kind == "gpio":
            records.append(bytes([OP_GPIO, 4]) + struct.pack("<HH", op[1], op[2]))
        elif kind == "trigger":
            records.append(bytes([OP_TRIGGER, 2]) + struct.pack("<H", op[1]))
        elif kind == "mark":
            records.append(bytes([OP_CAPTURE_MARK, 1, op[1]]))
        elif kind == "repeat":
            records.append(bytes([OP_REPEAT, 4]) + struct.pack("<HH", op[1], 0))
        else:
            raise ValueError("unknown op %r" % kind)

    offsets = []
    off = 2
    for rec in records:
        offsets.append(off)
        off += len(rec)
    for i, op in enumerate(ops):
        if op[0] == "repeat":
            target = offsets[op[2]]
            payload = struct.pack("<HH", op[1], target)
            records[i] = bytes([OP_REPEAT, 4]) + payload

    blob = bytes([addr, len(ops)]) + b"".join(records)
    if len(blob) > 512:
        raise ValueError("script too long: %d bytes" % len(blob))
    return blob


def parse_script_result(blob):
    """Decode SCRIPT_RUN results into a list of dicts."""
    out = []
    off = 0
    while off + 8 <= len(blob):
        op_index, status, nbytes = struct.unpack_from("<BBH", blob, off)
        ts = struct.unpack_from("<I", blob, off + 4)[0]
        data = blob[off + 8:off + 8 + nbytes]
        out.append({"op": op_index, "status": status, "ts_us": ts, "data": data})
        off += 8 + nbytes
    return out


class F072Error(RuntimeError):
    pass


class F072I2C:
    """Adapter transport. `address` is the 7-bit I2C slave address."""

    def __init__(self, port=None, address=0x30, speed_khz=100, turnaround_us=15,
                 baudrate=921600, timeout=3.0, trace=None, retries=4):
        self.address = address
        self.turnaround_us = turnaround_us
        self.repeated_start = False
        self.trace = trace
        self.timeout = timeout
        self.retries = retries
        self.seq = 0
        self._opened = False
        self.ser = None
        if port:
            self.open(port, baudrate)

    def open(self, port, baudrate=921600):
        self.ser = serial.Serial(port=port, baudrate=baudrate, timeout=0.2,
                                 write_timeout=self.timeout)
        self._opened = True
        self.ser.reset_input_buffer()
        if speed_khz := getattr(self, "speed_khz", None):
            self.set_speed(speed_khz)
        if self.turnaround_us:
            self.set_turnaround(self.turnaround_us)

    def close(self):
        if self._opened and self.ser:
            self.ser.close()
        self._opened = False

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    # -- framing ---------------------------------------------------------
    def _read_exact(self, n):
        out = bytearray()
        deadline = time.monotonic() + self.timeout
        while len(out) < n:
            if time.monotonic() > deadline:
                raise F072Error("serial timeout")
            chunk = self.ser.read(n - len(out))
            if chunk:
                out += chunk
            else:
                time.sleep(0.001)
        return bytes(out)

    def _exchange(self, cmd, payload=b""):
        if not self._opened:
            raise F072Error("adapter not open")
        frame = build_frame(self.seq, cmd, payload)
        for attempt in range(self.retries):
            self.ser.write(frame)
            status = None
            try:
                raw = self._read_exact(5)
                length = struct.unpack_from("<H", raw, 3)[0]
                raw += self._read_exact(length + 2)
                seq, status, data = parse_frame(raw)
                if seq != self.seq:
                    raise ValueError("seq mismatch")
                self.seq = (self.seq + 1) & 0xFF
                if status:
                    raise F072Error("%s (0x%02X)" % (ERRORS.get(status, "ERROR"), status))
                if self.trace:
                    self.trace(payload, data, cmd)
                return data
            except F072Error:
                raise
            except (ValueError, serial.SerialException) as exc:
                if attempt == self.retries - 1:
                    raise F072Error("no valid response: %s" % exc)
                self.ser.reset_input_buffer()
                time.sleep(0.005)
        raise F072Error("unreachable")

    # -- Ch341I2C-compatible surface -------------------------------------
    def _stream(self, wbytes, rlen):
        wbytes = bytes(wbytes)
        if not wbytes:
            raise F072Error("missing I2C address byte")
        addr = wbytes[0] >> 1
        data = wbytes[1:]
        if rlen == 0:
            self._exchange(CMD_WRITE, bytes([addr]) + data)
            return b""
        if self.repeated_start:
            payload = bytes([addr, len(data)]) + data + struct.pack("<H", rlen)
            return self._exchange(CMD_REP_START, payload)[:rlen]
        payload = (bytes([addr, len(data)]) + data +
                   struct.pack("<HH", rlen, self.turnaround_us))
        return self._exchange(CMD_WRITE_READ, payload)[:rlen]

    def stream(self, wbytes, rlen=0):
        return self._stream(wbytes, rlen)

    def set_stream(self, speed_code):
        codes = {0: 20, 1: 100, 2: 400, 3: 400}
        return self.set_speed(codes.get(speed_code, 100))

    def write(self, payload):
        self._stream(payload, 0)

    def read(self, length):
        return self._stream(bytes([(self.address << 1) | 1]), length)

    # -- adapter-specific -------------------------------------------------
    def ping(self):
        data = self._exchange(CMD_PING)
        version, caps = struct.unpack_from("<HI", data, 0)
        return version, caps, data[6:].decode("ascii", "replace")

    def set_speed(self, khz):
        data = self._exchange(CMD_SET_SPEED, struct.pack("<H", int(khz)))
        self.speed_khz = struct.unpack("<H", data)[0]
        return self.speed_khz

    def set_turnaround(self, us):
        data = self._exchange(CMD_SET_TURNAROUND, struct.pack("<H", int(us)))
        self.turnaround_us = struct.unpack("<H", data)[0]
        return self.turnaround_us

    def set_stretch_timeout(self, ms):
        return struct.unpack("<H", self._exchange(
            CMD_SET_STRETCH_TO, struct.pack("<H", int(ms))))[0]

    def gpio(self, mask, value):
        return struct.unpack("<H", self._exchange(
            CMD_GPIO, struct.pack("<HH", mask, value)))[0]

    def adc(self, channel=0):
        raise NotImplementedError("ADC_SAMPLE is not implemented in firmware v1")

    def bus_clear(self):
        self._exchange(CMD_BUS_CLEAR)

    def stats(self, reset=False):
        data = self._exchange(CMD_STATS, bytes([1 if reset else 0]))
        vals = struct.unpack("<7I", data[:28])
        return dict(zip(STAT_NAMES, vals))

    def script(self, blob, chunk=256):
        self._exchange(CMD_SCRIPT_RUN, bytes(blob))
        out = bytearray()
        offset = 0
        while True:
            part = self._exchange(CMD_SCRIPT_GET, struct.pack("<H", offset))
            if not part:
                break
            out += part
            offset += len(part)
        return bytes(out)

    def script_abort(self):
        self._exchange(CMD_SCRIPT_ABORT)

    def capture_start(self, channels=0x03, max_events=1024):
        self._exchange(CMD_CAPTURE_START, bytes([channels]) + struct.pack("<H", max_events))

    def capture_get(self):
        out = bytearray()
        offset = 0
        while True:
            data = self._exchange(CMD_CAPTURE_GET, struct.pack("<H", offset))
            total = struct.unpack_from("<H", data, 2)[0]
            chunk = data[4:]
            if not chunk:
                break
            out += chunk
            offset += len(chunk)
        events = []
        for i in range(0, len(out) - 4, 5):
            ts = struct.unpack_from("<I", out, i)[0]
            events.append((ts, out[i + 4]))
        return total, events

    def capture_stop(self):
        # The firmware stops capture implicitly when the event limit is hit.
        pass
