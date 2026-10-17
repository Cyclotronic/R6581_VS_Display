# R6581 TFT firmware — second test build for Hydron (2026-10-17)

Ready-to-flash build of branch `hydron-layout` (commit `b358880`), on top of the first test build (`Prebuilt/hydron-test-2026-10-16/`,
whose notes still apply: sigma, ohm/sigma loaded once, red ERROR, larger annunciators). Run on an R6581T with the
STM32F103C8 "Blue Pill" + LT7680A-R. Built with Arm GNU Toolchain 14.2.Rel1 at `-O0`.

| File | What |
| --- | --- |
| `R6581_VS_Display_hydron-test2_b358880.hex` | firmware (Intel HEX) |
| `R6581_VS_Display_hydron-test2_b358880.bin` | same image, raw binary for `0x08000000` |
| `SHA256SUMS.txt` | checksums |

## What is new since the first test build

1. **Live layout now uses your sizes, with balanced spacing.** Annunciators 12x48, main line 48x96, aux line 32x64 (32-dot font
   drawn x2, pitch 33). Gaps between the three rows are 8 px; the main line is at `Xpos_MAIN` 184 (yours: 176). One layout table
   (`LineFmt` in `display.c`) drives every line. The aux line now uses the same 16x32 glyph set as the main line, so the 12x24 set is gone.
2. **Boot diagnostics only for the first 10 boots.** The Blue Pill speed test and the splash (credits, TFT timing numbers, `boot n/10`)
   are shown on their own boot screen for 4 s before the live display starts, so they no longer share the screen. From boot 11 on
   the board goes straight to the live display. **Hold DCV while powering up** to reset the count (this also enters the TFT timing
   mode as before and leaves the timing settings as loaded).
3. The boot count lives in spare words of the settings page (offset 64..127). It does not touch the 8 timing words. A normal boot does
   no flash erase.

## Flashing

Same as the first build: take a backup, SWD with openocd or STM32CubeProgrammer, verify, **no full chip erase** (the last 1 KB holds
the TFT timing settings).

```
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg -c "init" -c "halt" -c "dump_image my_backup.bin 0x08000000 0x10000" -c "exit"
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg -c "init" -c "halt" -c "program R6581_VS_Display_hydron-test2_b358880.hex verify" -c "reset run" -c "exit"
```

## Please check

- Do the three rows sit well on your panel and under your lens? (The first draft with a taller main line caused lens interference on
  one panel, so the main line is the same size and position as the first test build.)
- Boot screen for the first 10 boots: readable, nothing left behind when the live display starts?
- Holding DCV at power-up: does the boot screen come back and count `boot 1/10` again?
- Anything cut off at the left or right edge?
