#!/bin/bash
# Build firmware using the vendored public STM32Cube USB CDC stack.
set -e
cd "$(dirname "$0")"
F=firmware
INC="-I$F/include -I$F/usb_stack/Core/Inc -I$F/usb_stack/USB_DEVICE/App -I$F/usb_stack/USB_DEVICE/Target -I$F/usb_stack/Drivers/STM32F1xx_HAL_Driver/Inc -I$F/usb_stack/Drivers/STM32F1xx_HAL_Driver/Inc/Legacy -I$F/usb_stack/Drivers/CMSIS/Device/ST/STM32F1xx/Include -I$F/usb_stack/Drivers/CMSIS/Include -I$F/usb_stack/Middlewares/ST/STM32_USB_Device_Library/Core/Inc -I$F/usb_stack/Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc"
FLAGS="-mcpu=cortex-m3 -mthumb -std=gnu11 -Os -ffunction-sections -fdata-sections -DSTM32F103xB -DHSE_VALUE=8000000UL -DUSE_HAL_DRIVER"
mkdir -p build
rm -f build/*.o
arm-none-eabi-gcc $FLAGS -c $F/src/startup_stm32f103c8tx.s -o build/startup.o
SRCS=$(find $F/src $F/usb_stack/Drivers/STM32F1xx_HAL_Driver/Src $F/usb_stack/Middlewares/ST/STM32_USB_Device_Library -name '*.c')
for f in $SRCS; do
  o="build/$(echo "$f" | tr '/' '_' | sed 's/\.c$/.o/')"
  arm-none-eabi-gcc $FLAGS $INC -c "$f" -o "$o"
done
arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -Wl,--gc-sections --specs=nano.specs --specs=nosys.specs -Tlinker/STM32F103C8TX_FLASH.ld build/*.o -o build/f072_adapter.elf
arm-none-eabi-objcopy -O binary build/f072_adapter.elf build/f072_adapter.bin
arm-none-eabi-size build/f072_adapter.elf
