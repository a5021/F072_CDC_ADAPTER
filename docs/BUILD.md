# Build and flash

Target: STM32F103CBT6 (Cortex-M3, 128 KB flash, 20 KB SRAM, HSE 8 MHz,
SYSCLK 72 MHz, USB 48 MHz via PLL/1.5).

USB stack: open-source register-level implementation from
https://github.com/daedaleanai/stm32f103_usb (no ST HAL or proprietary
middleware). Its register header `firmware/stm32f103_md.h` and `usb.c`
are used as-is with local additions for CDC ACM class requests.

Build:

```
bash build_firmware.sh
```

Flash:

```
st-flash --serial <SERIAL> --connect-under-reset --reset write build/f103_adapter.bin 0x08000000
```

Host driver `tools/f103_adapter.py` (pyserial). Tests:

```
python -m unittest discover -s tests -v
```
