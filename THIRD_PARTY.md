# Third-party code

The MIT license in `LICENSE` covers the code written for this project. Files
under `firmware/vendor/` come from other projects and keep their own terms.
They are vendored unmodified (except the CDC adaptation described in
`firmware/vendor/usb.c`), so upstream licenses apply to them.

| Files | Origin | License |
|---|---|---|
| `usb.c`, `usb.h`, `usart.c`, `usart.h`, `clock.c`, `clock.h`, `gpio2.c`, `gpio2.h`, `boot.c`, `vectors.c`, `printf.c`, `printf.h`, `stm32f103_md.h`, `stm32f103usb.h`, `stm32_flash_f103_128k.ld`, `Makefile`, `build.sh` | [daedaleanai/stm32f103_usb](https://github.com/daedaleanai/stm32f103_usb) | see the upstream repository |
| `core_cm3.c`, `core_cm3.h` | ARM CMSIS (`Copyright (C) 2009 ARM Limited`) | Apache-2.0 upstream |
| `stb_sprintf.h` | [nothings/stb](https://github.com/nothings/stb) | MIT or public domain, at your option (text is in the file) |

If the upstream of the USB stack publishes its license file, its terms take
precedence over anything written here.
