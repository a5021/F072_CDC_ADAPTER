# Build and flash

Target: STM32F103CBT6 (Cortex-M3, 128 KB flash, 20 KB SRAM, HSE 8 MHz,
SYSCLK 72 MHz, USB 48 MHz via PLL/1.5).

The firmware uses the public STM32Cube HAL + ST USB Device Library (CDC ACM)
vendored under `firmware/usb_stack/` from https://github.com/philrawlings/bluepill-usb-cdc-test
(BSD-3-Clause / ST license). Build with arm-none-eabi-gcc:

```
bash build_firmware.sh
```

Outputs: `build/f072_adapter.elf`, `build/f072_adapter.bin`.

Flash:

```
st-flash --serial <SERIAL> --connect-under-reset --reset write build/f072_adapter.bin 0x08000000
```

Host driver: `tools/f072_adapter.py` (`F072I2C`). Run host tests with:

```
python -m unittest discover -s tests -v
```

Note: after flashing, Windows may keep a stale COM port entry; replug USB once.
