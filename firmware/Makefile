PREFIX=arm-none-eabi-
CC      := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
SIZE    := $(PREFIX)size
REVISION := $(shell git log -1 --format="%h" || echo "(NONE)")

ARCH_FLAGS = -mthumb -mcpu=cortex-m3
OPT_FLAGS  = -O2 -flto -fuse-linker-plugin -ffunction-sections -fdata-sections -ffat-lto-objects
WARN_FLAGS = -Wall -Wextra -Wno-enum-conversion
CFLAGS = -std=gnu99 $(ARCH_FLAGS) $(OPT_FLAGS) $(WARN_FLAGS) \
         -DNDEBUG -D__REVISION__='"$(REVISION)"' -Iinclude -Ivendor
LDFLAGS = -nostartfiles -lnosys -static $(ARCH_FLAGS) $(OPT_FLAGS) $(WARN_FLAGS) \
          -Wl,-gc-sections

LD = vendor/stm32_flash_f103_128k.ld
VPATH = src:vendor
OBJS = vectors.o boot.o clock.o gpio2.o usart.o printf.o usb.o \
       main.o board.o timing.o stats.o i2c_master.o capture.o protocol.o script.o usb_cdc.o

.DEFAULT_GOAL := main.elf
$(OBJS): Makefile
%.o: %.c
	$(CC) -c -o $@ $(CFLAGS) $<
main.elf: $(LD) $(OBJS)
	$(CC) -o $@ $(filter %.o,$^) $(LDFLAGS) -T$(LD)
	$(SIZE) $@
clean:
	rm -f *.o *.elf *.hex *.bin *.map
