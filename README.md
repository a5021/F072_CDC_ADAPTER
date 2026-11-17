# F072_CDC_ADAPTER

USB CDC (VCOM) to I2C master adapter for STM32F103CBT6 (Blue Pill). Repository
name is historical; the original STM32G0 board had no USB peripheral.

The firmware uses a compact register-level USB stack from the open-source
`daedaleanai/stm32f103_usb` project. **No ST HAL / STM32Cube / proprietary
middleware.** CMSIS-style register header is also from that project.

## Layout

- `firmware/` — flat firmware sources + Makefile
- `tools/f072_adapter.py` — host driver (`F072I2C`)
- `tests/` — host-side protocol tests

## Build

```
bash build_firmware.sh
```

Outputs `build/f072_adapter.elf`, `.bin`, `.hex`. Flash:

```
st-flash --serial <SERIAL> --connect-under-reset --reset write build/f072_adapter.bin 0x08000000
```

After flashing, replug USB once so Windows drops the stale COM node.

## Status

Verified on bench (COM6, `0483:5740`): PING, SET_SPEED, SET_TURNAROUND,
SET_STRETCH_TIMEOUT, STATS, GPIO. I2C transactions require a slave with
external 2.2 kOhm pull-ups on PB6/PB7.
