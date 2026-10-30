# Reviewing the rework

A guide for reviewing the reworked firmware before it is published: the commits in order, what each one changes and where
to look, the spots that deserve the closest reading, how it was built and tested, and what is still unverified.
The reasons and the measurements behind each change are in [REWORK-PLAN.md](REWORK-PLAN.md); this file points into it.

## Contents

- [What is being reviewed](#what-is-being-reviewed)
- [How to read the diff](#how-to-read-the-diff)
- [The commits](#the-commits)
- [Where the code is now](#where-the-code-is-now)
- [Where to look hardest](#where-to-look-hardest)
- [Behaviour changes a user will notice](#behaviour-changes-a-user-will-notice)
- [Build matrix](#build-matrix)
- [Bench evidence](#bench-evidence)
- [Not yet verified](#not-yet-verified)
- [Left as Ian wrote it](#left-as-ian-wrote-it)
- [Publishing](#publishing)

## What is being reviewed

- **Branch:** `rework/stage7-structure`. It is `master` (`9990d7c`, the same as `origin/master`) plus the commits below,
  in a straight line.
- **Size:** about 5,400 lines added and 3,100 removed across 36 files. Most of the Stage 7 commit's 3,700 added lines
  are code moved between files, unchanged.
- **The other `rework/*` branches** each point at one commit inside this line (the state at the end of that stage).
  They need no separate review. `rework/sdram-soak` is checked out in a second worktree on the NAS.
- **Ready-to-flash image:** [`Prebuilt/rework-2026-10-30-O2/`](../Prebuilt/rework-2026-10-30-O2/).

## How to read the diff

Commit by commit is easiest: each stage is small, builds on its own, and keeps the old behaviour behind a build option.

```bash
git log --reverse --format='%h %s' master..rework/stage7-structure
```

The Stage 7 commit (`c26b634`) moves most of `lt7680.c`, `main.c` and `display.c` into new files. Show moved lines
dimmed, so only the real changes stand out:

```bash
git show c26b634 --color-moved=dimmed-zebra --color-moved-ws=allow-indentation-change
```

To see the end result against Ian's code file by file, compare `master` with the branch tip. The old `lt7680.c` maps to
`lt_bus.c`, `lt_init.c`, `lt_draw.c` and `lt_bench.c`; see [Where the code is now](#where-the-code-is-now).

## The commits

| Commit | What | Code to read | Undo at build time | Evidence |
|---|---|---|---|---|
| `100bbd0` | Rework plan (docs only) | | | |
| `baaf894` | **Stage 0:** read the LT7680's PLL, panel and SDRAM registers back over SWD | `LT_RegDump()` (now `lt_bench.c`), `docs/tools/regdump.py` | off unless `LT_REGDUMP=1` | [Stage 0](REWORK-PLAN.md#stage-0-results-2026-10-29) |
| `c54973f` | **Stage 1:** SDRAM set-up writes the normal register bank; auto-refresh on, interval from the memory clock; ready-wait with a timeout | `SDRAM_Init_LT`, `SDRAM_RefreshInterval`, `SDRAM_WaitReady_LT` in `lt_init.c` | `SDRAM_INIT_LEGACY=1` | [Stage 1](REWORK-PLAN.md#stage-1-sdram-initialisation), [soak](REWORK-PLAN.md#sdram-retention-soak-2026-10-30) |
| `8665d46` | SDRAM retention soak (bench instrument) | `SdramSoak_*` in `lt_bench.c`, `docs/tools/soak.py` | off unless `SDRAM_SOAK=1` | as above |
| `5dbc4c5` | **Stage 2:** every PLL inside its VCO range; memory and core clocks 100 MHz; backlight PWM prescaler from the core clock; timing-adjust mode keeps the boot counter | `SetPLL_LT`, `LT7680_PLL_Initial_LT`, `ConfigurePWMAndSetBrightness` in `lt_init.c`; `TimingAdjust_Poll` in `settings.c` | `LT_CLOCKS_LEGACY=1` | [Stage 2](REWORK-PLAN.md#stage-2-clocks) |
| `9ac1f63` | **Stage 3:** reset, PLL and SDRAM waits polled with timeouts; PLL read-back; one retry with a hardware reset; fault line, or LED blink if the chip never answers. (`SDRAM_INIT_LEGACY=1` keeps Ian's SDRAM wait, which has no timeout.) | `LT_Init_LT`, `LT_WaitIdle_LT`, `PLL_Switch_LT` in `lt_init.c`; the init loop in `main()` | `LT_INIT_LEGACY=1`; `LT_FAULT_TEST=n` to exercise it | [Stage 3](REWORK-PLAN.md#stage-3-results-2026-10-30) |
| `153334d` | **Stage 4:** the EXTI handler copies only whole DMA frames; the main loop decodes the copy. Capture pause and SPI1 toggling removed. Fixes an out-of-bounds write in the debug strings | `Capture_FrameEnd`, `Capture_Poll` in `vfd_capture.c`; `stm32f1xx_it.c` | `CAPTURE_LEGACY=1` | [Stage 4](REWORK-PLAN.md#stage-4-results-2026-10-30) |
| `bc6c094` | **Stage 5:** LT7680 bus on the SPI1 registers at 18 MHz; address frames skipped when unchanged; cache of the registers only the firmware changes | all of `lt_bus.c` | `LT_BUS_HAL=1`, `LT_REG_CACHE=0`, `LT_SPI_PRESC` | [Stage 5](REWORK-PLAN.md#stage-5-results-2026-10-30) |
| `2a1d2d7` | **Stage 6:** draw only the cells and annunciators that changed, as soon as a new frame is decoded; full repaint every 10 s; `ClearScreen()` as one filled rectangle | `Render_Update`, `DrawChanged`, `BuildMain`, `BuildAux`, `DrawCells` in `render.c`; `ClearScreen`, `LT_WaitDraw` in `lt_draw.c` | `FULL_REDRAW=1`, `FULL_REDRAW_MS`, `EDGE_WIPE=1`, `LT_CLEAR_TEXT=1` | [Stage 6](REWORK-PLAN.md#stage-6-results-2026-10-30) |
| `c26b634` | **Stage 7:** files split by job; dead code removed; `DelayMicroseconds()` on the cycle counter; TIM2 flag clear and `SetTimerDuration()` fixed; boot trimmed (boot screen off by default, 1 s settle wait to 20 ms) | mostly moves (see above); the fixes in `lcd.c`, `timer.c`, `stm32f1xx_it.c`; `main()` | `BOOT_DIAG_SHOWS=10`, `BOOT_SETTLE_MS=1000` | [Stage 7](REWORK-PLAN.md#stage-7-results-2026-10-30) |
| `82baa04` | Timing-adjust mode chosen at 2.3 s in the main loop, not at start-up (a cold power-up left the display blank) | the `TIMING_MODE_CHECK_MS` block in the main loop | `TIMING_MODE_CHECK_MS` | Stage 7 results, "Cold power-up" |
| `fdb2383` | Boot in 0.14 s: settings first, LT7680 set-up inside the panel's 120 ms Sleep Out wait, panel SPI at 1 µs. Capture decodes every new whole frame at once | `main()`; `*_Init_Start/_Finish` in `lcd.c`; `Capture_Poll` in `vfd_capture.c`; `Decode_Text`, `Main_Aux_Commit` in `vfd_decode.c` | `LCD_BIT_US=5 LCD_CS_US=10`, `CAPTURE_CONFIRM=1` | [After Stage 7](REWORK-PLAN.md#after-stage-7-boot-capture-rule-and-gpib-tests-2026-10-30) |
| `08473bd` | Review guide, rework image in `Prebuilt/`, docs brought up to date, two stale comments in `vfd_capture.c` | comments only | | |
| `ee3f4c6` | Hardware identity on the timing-adjust page: MCU CPUID, device ID, flash size, cycle counter; panel ID; LT7680 set-up result. Notes on clone chips | `DrawChipId` in `settings.c`; `LCD_ReadID` in `lcd.c`; its call in `main()` | | [BLUE-PILL-CHIPS.md](BLUE-PILL-CHIPS.md) |
| `0e75e51` | The panel read also takes the power-mode byte (`0Ah`), which proves the panel answers; ID bytes are unprogrammed (`FFFFFF`) on the ER-TFT4.58-1 | `LCD_ReadID`, `LCD_ReadByte` in `lcd.c` | | [BLUE-PILL-CHIPS.md](BLUE-PILL-CHIPS.md#hardware-identity-on-the-timing-adjust-page) |
| `21f4363` | Timing-adjust page inside the meter's window, every row in the 32 px font; title carries the instructions; `New/Saved:` | `TA_X_*`, `DrawIdLine` and the page in `settings.c` | | [LAYOUT.md](LAYOUT.md#timing-adjust-page) |
| `f8fad41` | From an adversarial review: on a power-on reset only, the panel's first command waits until 10 ms and its Sleep Out until 150 ms after reset (ST7701S reset timing); a SysTick fallback when the DWT cycle counter is missing; every stored setting range-checked; DISPLAY ON detected on every frame (it was missed on ohms ranges); timing-adjust re-sets up the selected set's panel type; the panel read uses no pull | `main()` start-up; `LCD_SleepOut`, `DelayMicroseconds`, `LCD_ReadByte` in `lcd.c`; `Settings_Load`, `TimingAdjust_Poll` in `settings.c`; `Render_Update` | `LCD_POWERON_CMD_MS`, `LCD_POWERON_SLPOUT_MS`, `DELAY_FORCE_SYSTICK=1`, `LCD_READ_US` | [BLUE-PILL-CHIPS.md](BLUE-PILL-CHIPS.md) |
| (release commit) | Image rebuilt from `f8fad41` (byte-identical to the image tested on the bench) | none | | |

## Where the code is now

| File | Job | Came from |
|---|---|---|
| `main.c` | start-up order and the main loop | `main.c` |
| `vfd_capture.c` | SPI2 DMA buffer, whole-frame copy in the EXTI handler, which frames get decoded | `main.c`, `stm32f1xx_it.c` |
| `vfd_decode.c` | unscramble the packets, bitmap to character, annunciators, into `G[]` and `Annunc[]` | `main.c` |
| `render.c` | the layout table (`LineFmt`), Ian's full-redraw functions, the redraw engine, the read-back check | `display.c` |
| `display.c` | boot screen, fault line, splash, glyph self-test | `display.c` |
| `settings.c` | flash settings page, timing-adjust mode, boot counter | `main.c` |
| `lt_bus.c` | SPI1 bus to the LT7680, register cache, busy wait | `lt7680.c` |
| `lt_init.c` | reset, PLLs, SDRAM, panel timing, windows, backlight | `lt7680.c` |
| `lt_draw.c` | fonts, text, colours, lines, fills, glyphs, `ClearScreen()` | `lt7680.c` |
| `lt_bench.c` | register dump, SDRAM soak, display memory read-back (all bench-only) | `lt7680.c` |
| `lcd.c` | ST7701S panel set-up over bit-banged SPI | unchanged apart from the split around Sleep Out and the delay |
| `vfd.h`, `settings.h` | new headers for the modules above | |

## Where to look hardest

These are the places where a mistake would matter most, or where the reasoning isn't obvious from the code.

1. **Capture hand-off between interrupt and main loop** (`vfd_capture.c`). `Capture_FrameEnd()` runs in the EXTI
   handler and copies `rx_buffer` only when the DMA counter is 0. `Capture_Poll()` copies `cap_frame` with interrupts
   off. Check that no path reads `rx_buffer` or `cap_frame` outside those two places, and that `cap_armed` drops the
   first, partial frame.
2. **The capture rule** (`Capture_Poll`, since `fdb2383`). Every new frame is decoded at once. A frame whose decode has
   an unknown pattern (`?`) is held back until the next frame is identical. `Packets_to_chars()` and `Decode_Text()`
   still run on a held-back frame; check they leave nothing behind that `Main_Aux_Commit()` would later pick up wrongly.
3. **The redraw engine** (`Render_Update` in `render.c`). It builds the strings that Ian's `DisplayMain()` and
   `DisplayAux()` would draw, compares them with what was drawn last, and redraws only runs of changed cells. Worth
   checking: glyph cells (`$` for ohm and sigma), the 1 V mode path, DISPLAY OFF and back on (`CheckDisplayStatus()`
   returns `turnedOn`, which forces a full repaint), and that a forced full repaint resets every "was" buffer.
   `RENDER_VERIFY=1` checks this pixel for pixel against the full redraw; it found nothing.
4. **Register cache and address skipping** (`lt_bus.c`). A cached register written somewhere that bypasses
   `WriteDataToRegister()`, or an LT7680 reset that doesn't call `LT_BusForget()`, would leave the cache stale.
   `LT_REGDUMP=1` compares the cache with the chip once a second: 759 checks in Stage 5 and 440 in Stage 7, no mismatch.
5. **PLL and refresh arithmetic** (`SetPLL_LT`, `SDRAM_RefreshInterval` in `lt_init.c`), against the LT768x datasheet
   V4.1 sections cited in the comments. Verified by register read-back, not only by calculation.
6. **Start-up order** (`main()`). On a power-on reset (`RCC_FLAG_PORRST`) the panel has just come out of its own
   reset: its first command waits until `LCD_POWERON_CMD_MS` and its Sleep Out until `LCD_POWERON_SLPOUT_MS` after
   reset, and the LT7680 is set up in between. A warm reset keeps the faster order: settings load first, then the
   panel's first half of set-up and Sleep Out; the LT7680 is set up during the panel's 120 ms after Sleep Out, then the
   panel's second half. Check that nothing touches the panel
   inside that 120 ms. Two waits are spun on `HAL_GetTick()`, which needs SysTick running (it is, from `HAL_Init()`).
7. **`DelayMicroseconds()` uses the Cortex-M3 cycle counter (DWT), with a fallback.** The ST7701S set-up depends on
   it. Every ST STM32F103 has the counter, but it is optional in a Cortex-M3, and a clone without it would have hung
   at boot. The first call now checks that the counter runs; if not, the delay counts SysTick instead, adding up its
   down-counts across reloads. Check the wrap arithmetic. `-DDELAY_FORCE_SYSTICK=1` forces the fallback, for testing
   on a genuine chip. See [BLUE-PILL-CHIPS.md](BLUE-PILL-CHIPS.md).
8. **Timing-adjust mode at 2.3 s** (main loop, `TIMING_MODE_CHECK_MS`). The live display has been running for 2.3 s by
   then; the block clears the screen and switches mode. Checked on the bench: holding DCV at power-up starts the
   mode and each press steps to the next set.
9. **The panel ID read at boot** (`LCD_ReadID` in `lcd.c`). It switches the panel's data pin (PB5) to a plain input
   for each 8-clock reply and back (four reads). Check that the pin is always restored to an output, and that chip
   select frames each read. The panel's reply is weak on this hardware; the reasons for no pull and 5 µs are in
   [BLUE-PILL-CHIPS.md](BLUE-PILL-CHIPS.md#hardware-identity-on-the-timing-adjust-page).
10. **The settings page** (`settings.c`). It is the last 1 KB page of flash, `0x0800FC00`. Any erase there must write
   the timing set back; the boot-counter words share the page. Never do a full-chip erase when flashing.

## Behaviour changes a user will notice

- **No boot screen.** The speed test and splash are off (`BOOT_DIAG_SHOWS=0`); `BOOT_DIAG_SHOWS=10` restores the
  pre-rework behaviour. See [BOOT-AND-SPLASH.md](BOOT-AND-SPLASH.md).
- **The display is up 0.14 s after reset** instead of about 2.9 s, so the meter's own power-on screen (`Ver.` and
  `GPIB Addr`) now shows.
- **Updates follow the meter's display** frame by frame (up to 111 a second, about 1 ms behind), where Ian's loop redrew
  at most 28.6 times a second.
- **The LT7680 starts on the stored timing set** instead of the built-in defaults. No difference unless the stored set
  differs from the defaults, and then the stored set is what timing-adjust mode chose.
- **Holding DCV at power-up** is read at 2.3 s, as Ian's start-up effectively did.

## Build matrix

During the rework every build option was compile-checked on its own, as each stage added it. The final code is built
at `-O0` and `-O2` with no new warnings; the reference sizes are in [BUILD-AND-FLASH.md](BUILD-AND-FLASH.md). The
remaining warnings are all in Ian's code: `-Wformat` on the `%d` used for `uint32_t` timing values (`display.c`,
`settings.c`), `-Wunused-value` on `EEPROM_ErasePage(...) == HAL_OK;` (`settings.c`), `-Wstringop-truncation` in the
1 V mode's `strncpy` (`render.c`), and `-Wmultichar` / `-Woverflow` for the UTF-8 `'°'` and `'½'` in the font table
(`vfd_decode.c`; the low byte, 0xB0 or 0xBD, is what the code uses). Only the default `-O2` image was flashed and run.

## Bench evidence

All on an R6581T with the original VFD-to-TFT hardware, an ST-Link on SWD, and the meter on GPIB. The panel could not be
seen until after Stage 7, so the earlier checks are by measurement (register read-back, display memory read-back,
counters over SWD), not by eye.

| What | Result | Where |
|---|---|---|
| SDRAM retention | Ian's set-up loses up to 3% of bits in 10 minutes; the new one none, also at 100 MHz and 18 MHz SPI | Stages 1, 2, 5 |
| PLLs | every VCO inside 100-500 MHz, read back from the chip | Stage 2 |
| Init faults | a held reset and a no-reply chip both recover or report | Stage 3 |
| Torn frames | about 1 `?` per 1,000 frames before; none in any run since Stage 4, including 19,921 frames at the meter's fastest rate | Stage 4, after Stage 7 |
| Redraw correctness | display memory read back: 60 synthetic screens and live checks match Ian's full redraw pixel for pixel; cleared page reads black; right edge clean | Stages 6, 7 |
| Change-to-screen latency | 32.6 ms (Ian's loop) to 10.7 ms (Stage 6) to about 1 ms (after Stage 7) | Stage 6, after Stage 7 |
| Updates per second | up to 110.7, the meter's own VFD frame rate | after Stage 7 |
| Boot | first meter frame drawable 0.14 s after reset, against about 2.9 s; 10 resets each, no retries | after Stage 7 |
| Cold power-up | display comes up; the meter's own start-up sequence traced from 0.34 s | after Stage 7 |
| DISPLAY OFF / ON over GPIB | panel and backlight off, then back with a full repaint | after Stage 7 |

## Not yet verified

Checked by eye since the panel became visible: no flicker or colour faults, and timing-adjust mode starts when DCV is
held at power-up and steps through the sets on each press.

Still to do, from the [deferred visual checks](REWORK-PLAN.md#deferred-visual-checks):

- **60 minutes of DISPLAY OFF**, then on: ohm glyph and left column intact. This is the original corruption report;
  Stage 1's soak says it is fixed, but it hasn't been seen fixed. With Hydron.
- **Glyphs** (ohm, sigma, ERROR) after long running, with Hydron; the glyph self-test pages and a menu walk compared
  with the earlier photos and log.
- **Fault line** readable when forced.
- **Clone chips without DWT** (point 7 above): no such board here; the SysTick fallback was forced on the genuine chip
  instead.
- **The identity lines on the timing-adjust page** with no debugger attached (the device ID should read 0 on ST's
  chip), and a panel ID from a panel whose maker programmed it (the ER-TFT4.58-1 reads `FFFFFF`, unprogrammed).

## Left as Ian wrote it

Deliberately not changed, to keep the diff about behaviour: his naming and formatting, the global variables shared
between files, the commented-out demo code in the main loop, the decode tables, the panel register sequences in
`lcd.c`, the flash helper functions, and the files under `VisualGDB/Debug/` (his original image, kept as the revert
target).

## Publishing

Nothing has been pushed. `origin` is `Cyclotronic/R6581_VS_Display` (this fork); `upstream` is
`Ian-Johnston/R6581_VS_Display`.

Suggested order once the review is done:

1. Fix anything the review finds, on `rework/stage7-structure`, and rebuild the image in `Prebuilt/` if code changed.
2. Push the branch to `origin` and open a pull request into `origin/master`, or merge it into `master` locally and push.
3. Decide which of the `rework/stage*` branches to publish. They are only useful as bookmarks; the commits are all on
   the main branch.
4. Offering the work to Ian (`upstream`) is a separate step, and his call.
