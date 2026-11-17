# F072_CDC_ADAPTER

USB CDC (VCOM) to I2C master adapter for STM32F103CBT6 (Blue Pill), built to
replace the CH341A in the owbridge bench. Repository name is historical; the
original STM32G0 board had no USB peripheral.

## Layout

- `firmware/` — STM32Cube HAL + ST USB Device Library (CDC ACM) + adapter
  protocol, I2C master, script engine, capture. Vendored public stack in
  `firmware/usb_stack/`.
- `tools/f072_adapter.py` — host driver (`F072I2C`).
- `tests/` — host-side protocol tests.

## Build

```
bash build_firmware.sh
```

Outputs `build/f072_adapter.elf` / `.bin`. Flash with ST-Link:

```
st-flash --serial <SERIAL> --connect-under-reset --reset write build/f072_adapter.bin 0x08000000
```

## Status

USB enumerates as `0483:5740` (STM32 Virtual COM Port). Protocol verified:
PING, SET_SPEED, SET_TURNAROUND, SET_STRETCH_TIMEOUT, STATS, GPIO. I2C
transactions need a slave with external 2.2 kOhm pull-ups on PB6/PB7.
