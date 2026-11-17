# F072_CDC_ADAPTER

USB (CDC/VCOM) to I2C master adapter firmware, built to replace the CH341A in
the `owbridge` test bench. The bench board was identified as STM32G0 (no USB),
so the firmware now targets **STM32F103CBT6** (Blue Pill class: 128 KB flash,
20 KB SRAM, USB from PLL/1.5). The repository name is historical. All test scripts run on the host; the
adapter adds programmable STOP-to-START turnaround, microsecond timestamps,
in-firmware transaction scripts and an SCL/SDA capture buffer.

See [docs/PROTOCOL.md](docs/PROTOCOL.md) for the wire protocol,
[docs/PINOUT.md](docs/PINOUT.md) for the board wiring and
[docs/BUILD.md](docs/BUILD.md) for build/flash instructions.

## Layout

- `firmware/` — CMSIS-only firmware (no HAL, no dynamic memory)
- `tools/f072_adapter.py` — host driver (`F072I2C`)
- `tests/` — host-side protocol tests

## Status

Firmware builds to 7.7 KB text / 9.8 KB BSS. Host protocol unit tests pass.
Hardware validation is pending: loopback, frequency/turnaround sweeps, capture
comparison with an external logic analyzer and the 1-hour soak run.
