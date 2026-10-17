# R6581 TFT firmware — test build for Hydron (2026-10-16)

A ready-to-flash build of this repository's `master` (commit `3902c78`), built from Ian Johnston's
`R6581_VS_Display` with the changes below. It has been run on an R6581T with the original VFD-to-TFT hardware
(STM32F103C8 "Blue Pill" + LT7680A-R). Please try it on yours and report what you see.

Files here:

| File | What |
| --- | --- |
| `R6581_VS_Display_hydron-test_3902c78.hex` | the firmware (Intel HEX) |
| `R6581_VS_Display_hydron-test_3902c78.bin` | the same image as a raw binary, for `0x08000000` |
| `SHA256SUMS.txt` | checksums of the two files |

**To go back:** flash your own backup (take it first, see below), or Ian's original image, which is in this repository at
`VisualGDB/Debug/R6581_VS_Display.hex`.

## What changed (all in `Core/`; full diff: `compare/4241405...master`)

1. **Sigma on the statistics screen.** The meter sends a lowercase sigma as the label of the standard-deviation row
   (CONFIGURE > MATH > STATISTICS > READ, then the up/down keys). The 5x7 pattern `07 08 0E 11 11 12 0C` was not in the
   decode table, so it showed `?` (the missing glyph you reported). It is now drawn as a user-defined glyph.
2. **Ohm and sigma are loaded once.** They live in their own SDRAM regions (16x32 set at `0x200000`, 12x24 set at
   `0x201000`, chosen with the CGRAM start register REG[DBh-DEh]) instead of at address 0, which is the top-left of the display
   buffer. They are loaded at boot and again after DISPLAY OFF, so drawing can no longer overwrite them. This removes the
   per-frame reload and the fixed `Delay_NonBlocking(5)` waits around it. Redraw time on 2W/4W screens went from 45 ms to
   26 ms (non-ohm screens are unchanged at 25 ms), which puts the update back inside the 35 ms tick.
3. **One routine draws any line containing ohm or sigma** (`DrawLineWithGlyphs`), replacing the duplicated `$`-splitting code in
   `DisplayMain` and `DisplayAux`. Adding another custom glyph is a table entry plus the glyph data.
4. **ERR is shown as ERROR, in red.**
5. **Larger annunciators** in the 24-dot font, laid out one after another as in your `display.c` (INFO and SLOT blank; row at
   `Xpos_ANNUNC` 126; first label at 2 px).
6. **The splash screen's grey timing numbers moved** to below the credit line. With the taller annunciators they otherwise
   overlapped the annunciator row for the first 5 seconds.

Everything else is Ian's code. The glyph self-test (`-DGLYPH_SELFTEST=1`) and a timing probe (`-DTIMING_PROBE=1`) are in the
source but off in this build.

**Compared with your own build:** this uses Ian's `main.c` (so the 1V mode and the boot speed test are as Ian has them), Ian's main-line layout
(`Xpos_MAIN` 182) and the stock colours. It does not include your layout changes other than the annunciators. If you want these
changes in your tree, the `main.c` parts are three small things: the sigma entry in `bitmap_characters`
(`{{0x07,0x08,0x0E,0x11,0x11,0x12,0x0C}, '\x7F'}`), one call to `LoadCustomGlyphs_LT()` after the boot `ClearScreen()`, and the
reload call in `CheckDisplayStatus()` when the display wakes (in `display.c`).

## Flashing (ST-Link on the SWD header: SWDIO, SWCLK, GND)

Take a backup first. The last 1 KB of flash holds the TFT timing settings, so please do **not** do a full chip erase.

```
# backup (64 KB)
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg -c "init" -c "halt" -c "dump_image my_backup.bin 0x08000000 0x10000" -c "exit"

# flash and verify, then run
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg -c "init" -c "halt" -c "program R6581_VS_Display_hydron-test_3902c78.hex verify" -c "reset run" -c "exit"
```

STM32CubeProgrammer works too: open the `.hex`, connect over SWD, program with *Verify* and without *Erase full chip*.
The image is about 39 KB, well below the settings page, so existing settings are kept.

## Please check and tell me

- **Statistics:** step to the SIGMA row. Do you see a sigma where it used to say `?`? Does its size and height look right next to the other labels?
- **Ohm** on 2W and 4W, on both lines (e.g. `100 kΩ Range`, `kΩ2W`): same look as before? Any flicker?
- **Red ERROR** when the meter has an error (`ERR?` key shows it). Does it clear completely?
- **Annunciator row:** fits your panel? Anything cut off at either end (SRQ at the far right)? INFO and SLOT are intentionally blank.
- **Display OFF and back on** (including after a long time off): is the ohm symbol intact and is the left column clean?
- **Boot splash:** grey numbers near the bottom, annunciator row clean for the first 5 seconds.
- Does the display feel any quicker on the ohm screens?

If something looks wrong, a photo plus the screen it was on is the most useful thing.

## Notes

- Needs the LT7680A-R's SDRAM to be more than 2 MB (it is) because the glyph regions sit at `0x200000`.
- The sigma glyph is an oval ring with a flag at the top right, drawn to sit on the same baseline as the ohm; if you would rather have a different shape it is 64 + 48 bytes in `lt7680.c`.
- Built with the Arm GNU Toolchain 14.2.Rel1 at `-O0` (the same optimisation level as Ian's Debug build).
