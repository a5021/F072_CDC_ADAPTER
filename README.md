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

## Status

Verified on bench (COM6, `0483:5740`): PING, SET_SPEED, SET_TURNAROUND,
SET_STRETCH_TIMEOUT, STATS, GPIO. I2C transactions require a slave with
external 2.2 kOhm pull-ups on PB6/PB7.
