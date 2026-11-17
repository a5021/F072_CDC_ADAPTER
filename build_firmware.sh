#!/bin/bash
# Build firmware with the open-source register-level USB stack (no ST HAL/middleware).
set -e
cd "$(dirname "$0")/firmware"
make clean >/dev/null 2>&1 || true
make main.elf
arm-none-eabi-objcopy -O binary main.elf ../build/f072_adapter.bin
arm-none-eabi-objcopy -O ihex main.elf ../build/f072_adapter.hex
cp main.elf ../build/f072_adapter.elf
arm-none-eabi-size ../build/f072_adapter.elf
