#!/bin/bash
set -e
INC="-ICore/Inc -IUSB_DEVICE/App -IUSB_DEVICE/Target -IDrivers/STM32F1xx_HAL_Driver/Inc -IDrivers/STM32F1xx_HAL_Driver/Inc/Legacy -IDrivers/CMSIS/Device/ST/STM32F1xx/Include -IDrivers/CMSIS/Include -IMiddlewares/ST/STM32_USB_Device_Library/Core/Inc -IMiddlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc"
FLAGS="-mcpu=cortex-m3 -mthumb -std=gnu11 -Os -ffunction-sections -fdata-sections -DSTM32F103xB -DHSE_VALUE=8000000UL -DUSE_HAL_DRIVER"
SRC=$(find Core/Src USB_DEVICE Drivers/STM32F1xx_HAL_Driver/Src Middlewares/ST/STM32_USB_Device_Library -name '*.c' | tr '\n' ' ')
arm-none-eabi-gcc $FLAGS $INC -c Core/Startup/startup_stm32f103c8tx.s -o startup.o
for f in $SRC; do
  o=$(echo $f | tr '/' '_' | sed 's/\.c$/.o/')
  arm-none-eabi-gcc $FLAGS $INC -c "$f" -o "$o" || exit 1
done
arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -Wl,--gc-sections --specs=nano.specs --specs=nosys.specs -TSTM32F103C8TX_FLASH.ld startup.o $(ls *.o | grep -v "^startup.o$") -o test.elf
arm-none-eabi-objcopy -O binary test.elf test.bin
arm-none-eabi-size test.elf
