# Build and flash

CMSIS is fetched from GitHub at configure time (pinned revisions in
`CMakeLists.txt`); nothing is vendored.

```
cmake -S . -B build -G "Unix Makefiles" \
  -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi-gcc.cmake
cmake --build build
```

Outputs: `build/f072_adapter.elf`, `.bin`, `.hex`, `.map` and a size report.

Flash with ST-Link:

```
st-flash write build/f072_adapter.bin 0x08000000
# or
openocd -f interface/stlink.cfg -f target/stm32f0x.cfg \
  -c "program build/f072_adapter.elf verify reset exit"
```

Use the `.bin` file with `st-flash`; `--format ihex` with a `.hex` file writes
ASCII unless specified.

Host driver and tests:

```
python -m unittest discover -s tests -v
```

The driver is `tools/f072_adapter.py` (`F072I2C`). Point the owbridge tools
at it with `--adapter f072 --port COMx` (or set `F072_PORT`).
