PREFIX=arm-none-eabi-
CC          := $(PREFIX)gcc
OBJCOPY     := $(PREFIX)objcopy
SIZE        := $(PREFIX)size
REVISION := $(shell git log -1 --format="%h" || echo "(NONE)")
ARCH_FLAGS	 = -mthumb -mcpu=cortex-m3
LTO_FLAGS	 = -O2 -flto -fuse-linker-plugin -ffunction-sections -fdata-sections -fverbose-asm -ffat-lto-objects
WARN_FLAGS   	 = -Wall -Wextra -Wno-enum-conversion
DEBUG_FLAGS	 = -ggdb3 -DNDEBUG -D__REVISION__='"$(REVISION)"'
CFLAGS 		 = -std=gnu99 $(ARCH_FLAGS) $(LTO_FLAGS) $(WARN_FLAGS) $(DEBUG_FLAGS)
LDFLAGS		 = -nostartfiles -lnosys -static $(ARCH_FLAGS) $(LTO_FLAGS) $(WARN_FLAGS) $(DEBUG_FLAGS) -Wl,-gc-sections -Wl,--cref
.DEFAULT_GOAL := main.hex
LD_SCRIPT = stm32_flash_f103_128k.ld
OBJS = vectors.o boot.o clock.o gpio2.o usart.o printf.o usb.o main.o \
       board.o timing.o stats.o i2c_master.o capture.o protocol.o script.o usb_cdc.o
$(OBJS): Makefile
%.o: %.c
	$(CC) -c -o $@ $(CFLAGS) $<
%.elf: $(LD_SCRIPT)
%.elf: $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS) -T$(LD_SCRIPT)
%.hex: %.elf
	$(SIZE) $<
	$(OBJCOPY) -O ihex --set-start 0x08000000 $< $@
clean:
	rm -f *.o *.elf *.hex *.bin *.map
