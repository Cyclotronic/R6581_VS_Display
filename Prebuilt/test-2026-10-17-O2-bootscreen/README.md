# R6581 TFT firmware — third test build (2026-10-17): `-O2` and the new boot screen

Ready-to-flash build of commit `5339ab5` (`master`), built from Ian Johnston's `R6581_VS_Display` with the changes in the two earlier
test builds in `Prebuilt/` (sigma and ohm as loaded-once glyphs, red ERROR, larger annunciators, Hydron-size layout with 8 px
gaps, boot diagnostics for the first 10 boots) plus the three changes below. Run on an R6581T with the original VFD-to-TFT hardware
(STM32F103C8 "Blue Pill" + LT7680A-R).

| File | What |
| --- | --- |
| `R6581_VS_Display_test3_O2_5339ab5.hex` | firmware (Intel HEX) |
| `R6581_VS_Display_test3_O2_5339ab5.bin` | same image, raw binary for `0x08000000` |
| `SHA256SUMS.txt` | checksums |

## What is new

1. **Built with `-O2`** (Arm GNU Toolchain 14.2.Rel1). The earlier builds are `-O0`, like Ian's Debug build. At `-O2` the text redraw
   measured 18.4 ms against 25 ms, which leaves real margin inside the 35 ms tick, and the decoded display was identical in a 42-step run.
   **The boot "BluePill speed" number is higher with this build** (about 2.3 million instead of about 1.5 million); it is only displayed,
   not compared against a limit, and still separates a genuine STM32F103 from a slower clone only if you compare like with like.
2. **New boot screen** (first 10 boots only, as before): centred, inside the same vertical band as the live display, with two large
   white notes, `Splash + timing test: first 10 boots only` and `After that, hold DCV at power-up to reset`, the timing numbers and
   `boot n/10`, and the credits with Ian's character (from his PCB silkscreen artwork) in the same green. See
   [`docs/BOOT-AND-SPLASH.md`](../../docs/BOOT-AND-SPLASH.md) for the mock-up, how the counter works and how to get the old behaviour back.

3. **Ian's 1 V display mode is switched off.** (On the 1000 mV range a DCV press showed the reading in volts, e.g. `+ 0.99999709 VDC` with
   aux `1 V Range`.) The DCV press no longer toggles it, so the code is inert; the rest of Ian's DCV handling (hold at power-up for
   the TFT timing mode) is unchanged. Build with `-DONE_VOLT_MODE=1` to get it back; with that flag the image behaves exactly as the
   previous test build did. See [`docs/ONE-VOLT-MODE.md`](../../docs/ONE-VOLT-MODE.md).

## Flashing

Take a backup first, use SWD with openocd or STM32CubeProgrammer, verify, and **do not do a full chip erase** (the last 1 KB holds
the TFT timing settings and the boot counter).

```
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg -c "init" -c "halt" -c "dump_image my_backup.bin 0x08000000 0x10000" -c "exit"
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg -c "init" -c "halt" -c "program R6581_VS_Display_test3_O2_5339ab5.hex verify" -c "reset run" -c "exit"
```

## Please check

- Boot screen: readable, nothing clipped, Ian's character the right way round and in the same green as the credits?
- Live display: any change in how it looks or feels compared with the `-O0` builds (flicker, tearing, ohm screens)?
- Holding DCV at power-up: the screen comes back and reads `boot 1/10`.
- `BluePill speed` value on your board.
