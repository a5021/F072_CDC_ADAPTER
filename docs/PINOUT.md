# Pinout

| Signal | Pin | Notes |
|---|---|---|
| I2C1_SCL | PB6 | AF1, open-drain, no internal pull-up |
| I2C1_SDA | PB7 | AF1, open-drain, no internal pull-up |
| I2C1_SCL/SDA (reserve) | PB8/PB9 | not used by default |
| USB D-/D+ | PA11/PA12 | CDC/VCOM, external 1.5 kOhm pull-up on D+ required |
| SWDIO/SWCLK | PA13/PA14 | ST-Link |
| nRST (target) | PB1 | push-pull, active low |
| TRIG | PB12 | push-pull, active low pulse |
| Status LED | PC13 | left in board role |
| User button | PA0 | left in board role |
| HSE 8 MHz | PF0/PF1 | occupied by quartz |
| LSE 32.768 kHz | PC14/PC15 | occupied, unused by firmware |

GPIO mask bits for `GPIO`/`OP_GPIO` map directly to GPIOB pins: bit0 -> PB1
(nRST), bit1 -> PB12 (TRIG), bits 2..15 -> PB2..PB15. Avoid bits 6 and 7
(I2C) and bits 13/14 (SWD).

Pull-ups are external: 2.2 kOhm at 100 kHz, 4.7 kOhm at 20 kHz. Common ground
with the target is mandatory; logic levels are 3.3 V.
