# F103_CDC_I2C

USB CDC (VCOM) to I2C master adapter for STM32F103CBT6 (Blue Pill). The
project was renamed from F072_CDC_ADAPTER; PING now reports `F103I2C`.

The firmware uses a compact register-level USB stack from the open-source
`daedaleanai/stm32f103_usb` project. **No ST HAL / STM32Cube / proprietary
middleware.** CMSIS-style register header is also from that project.

Validated bus rate: 10-400 kHz for the adapter itself; with the polled
owbridge slave the working rate is 100 kHz (400 kHz is not supported by that
slave, and repeated START is only usable up to 50 kHz).

## Layout

- `firmware/src/` — adapter application (protocol, I2C, capture, scripts)
- `firmware/include/` — application headers
- `firmware/vendor/` — open-source register-level USB stack + register header
- `firmware/Makefile` — build
- `tools/f103_adapter.py` — host driver (`F103I2C`)
- `tests/` — host-side protocol tests

## Build

```
bash build_firmware.sh
```

Outputs `build/f103_adapter.elf`, `.bin`, `.hex`. Flash:

```
st-flash --serial <SERIAL> --connect-under-reset --reset write build/f103_adapter.bin 0x08000000
```

After flashing, replug USB once so Windows drops the stale COM node.

## USB identity

The device enumerates as a class-compliant CDC/ACM serial port with
`VID:PID = 1209:0001`. `0x1209` is the pid.codes open-source vendor ID and PID
`0x0001` is the test PID it reserves for unregistered projects: this adapter is
bench equipment for the owbridge project rather than a product, so it keeps the
test PID on purpose. A dedicated PID is only worth requesting if the adapter
ever ships.

It reports an `iSerialNumber` built from the STM32 96-bit unique ID, so several
adapters on one host stay distinct and keep their COM assignment. The function
is class-compliant, so no vendor driver or INF file is required on Windows,
Linux or macOS; the host driver simply opens the COM port and identifies the
firmware through `PING`.

## Status

Verified on bench (`1209:0001`, enumerated as COM8 on the test host): PING, SET_SPEED, SET_TURNAROUND,
SET_STRETCH_TIMEOUT, STATS, GPIO. I2C transactions require a slave with
external 2.2 kOhm pull-ups on PB6/PB7.
