# R6581 TFT firmware: the reworked firmware (2026-10-30), `-O2`

Ready-to-flash build of commit `f8fad41` (branch `rework/stage7-structure`): Ian Johnston's `R6581_VS_Display` with this
fork's earlier changes (see the test builds next to this folder) and the rework in
[`docs/REWORK-PLAN.md`](../../docs/REWORK-PLAN.md). For an R6581T with the original VFD-to-TFT hardware
(STM32F103C8 "Blue Pill" + LT7680A-R + ST7701S panel). Built with Arm GNU Toolchain 14.2.Rel1, `-O2`, no build options.

| File | What |
| --- | --- |
| `R6581_VS_Display_rework_O2_f8fad41.hex` | firmware (Intel HEX) |
| `R6581_VS_Display_rework_O2_f8fad41.bin` | same image, raw binary for `0x08000000` |
| `SHA256SUMS.txt` | checksums |

## What is new since the 2026-10-17 test builds

- **Display memory no longer decays.** The LT7680's SDRAM auto-refresh was never switched on, so display memory lost
  bits within minutes: the likely cause of the ohm and left-column corruption after a long DISPLAY OFF.
- **Clocks inside spec.** Every LT7680 PLL now runs inside its rated range.
- **Start-up checks itself.** The LT7680 set-up waits for the chip instead of fixed delays, retries once, and shows a
  red fault line, or blinks the Blue Pill LED if the chip never answers.
- **No more stray `?`.** Only whole frames from the meter are decoded.
- **Faster and smoother.** Only the characters that changed are redrawn, about 1 ms after the meter changes its
  display, at up to the meter's own 111 frames a second. Ian's loop redrew everything, at most 28.6 times a second.
- **Up in 0.14 s.** The display is ready 0.14 s after power-up, so the meter's own power-on screen now shows. The
  boot screen (speed test and splash) is off.
- **Hardware identity.** The timing-adjust page (hold DCV at power-up) shows the Blue Pill's chip, the panel's ID and
  how the LT7680 set-up went, for comparing boards.

The details, the measurements, and the build options that bring back each old behaviour are in
[`docs/REWORK-PLAN.md`](../../docs/REWORK-PLAN.md) and [`docs/BUILD-AND-FLASH.md`](../../docs/BUILD-AND-FLASH.md).

## Flashing

Take a backup first, use SWD with openocd or STM32CubeProgrammer, verify, and **do not do a full chip erase** (the last
1 KB holds the TFT timing settings and the boot counter).

```
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg -c "init" -c "halt" -c "dump_image my_backup.bin 0x08000000 0x10000" -c "exit"
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg -c "init" -c "halt" -c "program R6581_VS_Display_rework_O2_f8fad41.hex verify" -c "reset run" -c "exit"
```

## Please check

- Live display: colours right, no flicker; readings, range changes and menus look as before.
- Cold power-up: the display shows the meter's `Ver.` / `GPIB Addr` screen, then the self-test, then readings.
- Holding DCV at power-up: timing-adjust mode starts (about 2.3 s after power-up) and steps through the 6 timing sets.
  Two cyan lines at the bottom of that page identify the hardware (MCU, panel, LT7680); a photo of them helps if your
  board behaves differently. See [`docs/BLUE-PILL-CHIPS.md`](../../docs/BLUE-PILL-CHIPS.md).
- DISPLAY OFF for an hour, then on: the ohm symbol and the left column are intact.
