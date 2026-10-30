# Rework plan

A staged plan for reworking the display firmware: what is wrong, what to keep, and the order to change things in. Each stage
is small enough to build, flash and check on the bench before the next one starts, and each can be turned off at build time
until it has proved itself. Register references are to the LT768x datasheet V4.1 in `reference/lt7680/`.

Status (2026-10-30):

- **Stage 0 done:** the register dump on the bench confirms the SDRAM findings ([results](#stage-0-results-2026-10-29)).
- **Stage 1 done:** registers verified, and the SDRAM retention soak shows no bit loss where the current build loses up to
  3% of bits in 10 minutes ([soak results](#sdram-retention-soak-2026-10-30)).
- **Stage 2 done:** clocks verified in range, soak clean at 100 MHz.
- **Stage 3 done:** the init sequence detects faults and recovers, and the live display starts 0.5 s sooner
  ([results](#stage-3-results-2026-10-30)).
- **Stage 4 done:** whole, stable frames only. In 5 minutes no `?` reaches a redraw, against 5 or 6 for the current
  capture ([results](#stage-4-results-2026-10-30)).
- **Stage 5 done:** the redraw takes 6.6 ms instead of 15.0, and the live display starts at 1.6 s
  ([results](#stage-5-results-2026-10-30)). Bursts of data bytes in one chip-select frame work.
- **Stage 6 done:** only changed cells are drawn, as soon as a stable frame arrives. A change reaches the screen in
  10.7 ms on average instead of 32.6, and the screen read back from display memory matches Ian's full redraw pixel for
  pixel ([results](#stage-6-results-2026-10-30)).
- **Stage 7 done:** one job per file, dead code gone, two small bugs fixed; same bench results as Stage 6. The boot path
  was trimmed too: the meter's first frame can be drawn 0.14 s after reset, against about 2.9 s for Ian's start-up
  ([results](#stage-7-results-2026-10-30)). GPIB tests then found and fixed a fault in the capture rule: the board now
  follows the meter's display at its full 111 frames a second, about 1 ms behind
  ([after Stage 7](#after-stage-7-boot-capture-rule-and-gpib-tests-2026-10-30)).
- **Visual checks:** the panel became visible after Stage 7. It comes up on a cold power-up and shows the meter's
  power-on screen; the other checks are still to do ([list](#deferred-visual-checks)).

## Contents

- [What the audit found](#what-the-audit-found)
- [What to keep, refactor and rework](#what-to-keep-refactor-and-rework)
- [Target structure](#target-structure)
- [Stages](#stages)
  - [Stage 0: baseline and instruments](#stage-0-baseline-and-instruments)
  - [Stage 1: SDRAM initialisation](#stage-1-sdram-initialisation)
  - [Stage 2: clocks](#stage-2-clocks)
  - [Stage 3: init robustness](#stage-3-init-robustness)
  - [Stage 4: capture pipeline](#stage-4-capture-pipeline)
  - [Stage 5: LT7680 bus layer](#stage-5-lt7680-bus-layer)
  - [Stage 6: redraw engine](#stage-6-redraw-engine)
  - [Stage 7: structure and clean-up](#stage-7-structure-and-clean-up)
- [Test protocol](#test-protocol)
- [Open questions to settle on the bench](#open-questions-to-settle-on-the-bench)

## What the audit found

### Display memory (the corruption)

`SDRAM_Init_LT()` writes `E4h = 0x04` first. Bit 2 of `E4h` (SDR_PARAMEN) switches `E0h-E3h` to the **SDRAM timing
parameter registers**, so the values meant for the normal registers go into the timing registers instead:

| Write | Meant as | Actually sets (0h-Fh = 1-16 clocks) |
| --- | --- | --- |
| `E0h = 0x29` | attributes: 4 banks, 4K rows, 512 columns | TMRD = 10 clocks |
| `E1h = 0x03` | CAS latency 3 | TRFC = **1 clock** (default 9), TXSR = 4 (default 8) |
| `E2h = 0x9A` | refresh interval, low byte | TRP = 10, TWR = 11 |
| `E3h = 0x00` | refresh interval, high byte | TRCD = **1 clock** (default 3), TRAS = **1 clock** (default 7) |

`E4h = 0x01` then starts initialisation with the normal registers at their reset values:

- **Refresh interval `E2h-E3h` = 0000h, which disables auto-refresh.** The panel scan reads the display buffer every frame,
  so it keeps that memory alive while the display is on. Nothing refreshes the glyph region at `0x200000`. Nothing
  refreshes anything during DISPLAY OFF. That matches the open item "ohm/left-column corruption after long DISPLAY OFF".
- **TRAS and TRCD of 1 clock** (17 ns at the 58 MHz memory clock, against roughly 42 ns and 15-20 ns for typical SDR SDRAM)
  can close a row before its cells are fully restored. This is a second likely mechanism for slow bit loss.
- **`E0h` stays at its reset value.** The table text suggests 256 columns instead of 512. That needs confirming by readback
  (Stage 0).

The refresh calculation is wrong as well:

- It assumes a 20 MHz memory clock, but the PLL sets 2 x PCLK, which is 58 MHz at the default timings.
- It uses 8,192 rows, but the datasheet gives 4,096 for this part.
- `1000 / SDRAM_MCLK` is integer division, so it goes badly wrong at real clock rates.

The correct value is `MCLK_Hz x 64 ms / 4096`: about 906 at 58 MHz, and 1,562 at 100 MHz (the datasheet's own example).

### Clocks

| Item | Now | Note |
| --- | --- | --- |
| PCLK | 29 MHz (490 x 993 x 60 Hz, rounded) | the panel refresh rate is 59.6 Hz |
| MCLK, CCLK | 58 MHz | limits 133 and 100 MHz; the rules are CCLK x 2 >= MCLK >= CCLK and CCLK >= 1.5 x PCLK |
| PCLK PLL VCO | 58 MHz | **below the 100-500 MHz VCO range**: OD = /2, R = 5, so the VCO equals Fout |
| Timing-adjust mode | re-runs the PLL setup | MCLK changes, but the refresh interval is not recalculated |

### Capture and the main loop

- `Packets_to_chars()` runs on every pass of the main loop and reads `rx_buffer` while DMA is writing it. A frame can be
  half old and half new: that is the rare `?`.
- The "pause SPI2 capture" around drawing doesn't work. EXTI15_10 is left enabled and restarts SPI2 and its DMA every
  9 ms, two or three times per draw. It also disables DMA1 channel 5, which SPI2 RX doesn't use. Drawing works from
  `G[]`, not `rx_buffer`, so the pause isn't needed at all.
- Every tick ends with a fixed 6 ms busy wait. `Delay_NonBlocking` blocks, despite its name, and `DrawText` already waits
  for the controller.
- Each main-loop pass also:
  - rebuilds debug strings with `snprintf` and `strlen`
  - toggles SPI1 `SPE` and DMA channel 3
  - calls `NVIC_EnableIRQ(SPI1_IRQn)` for an interrupt that has no handler
- `main_display_debug[19]` is written at index 29. At `-O0` that zeroes `LCD_buffer_packets[1]`, which is harmless by luck.
  At `-O2` the store is optimised away. It's still a latent bug.

### LT7680 driver

- **Bus:**
  - Each register write costs two chip-select cycles and four one-byte `HAL_SPI_Transmit` calls, at 9 MHz. The STM32F103
    can do 18 MHz on SPI1, and the LT768x accepts up to 50 MHz.
  - `WaitForLT7680Ready()` runs before every character. It ignores its own timeout.
- **Init:**
  - `Check_SDRAM_Ready_LT()` has no timeout, and polls `E4h` bit 0 instead of status register bit 2.
  - `Software_Reset_LT()` writes nothing. (Wrong: it writes `00h` = 01h. Stage 3 found that writes in the next few ms
    are lost.)
- **Drawing:**
  - `DrawLine()` doesn't wait for the drawing engine before the next call reprograms it.
  - Every tick redraws all of main, aux and the 18 annunciators, changed or not. Each annunciator costs about 14 register
    writes before its text.
  - Eight lines are wiped at the right edge every tick to hide "random pixels". The cause is unknown, and it should be
    re-checked once Stage 1 is in.
  - `ClearScreen()` writes about 1,500 space characters, each with a cursor move.
- **Minor:**
  - `DelayMicroseconds()`'s wrap check can never be true.
  - The TIM2 handler clears its interrupt flag with a read-modify-write.
  - `SetTimerDuration()` causes an immediate extra tick.
  - Timing-adjust mode erases the whole settings page, boot counter included.

## What to keep, refactor and rework

| Area | Verdict | Why |
| --- | --- | --- |
| EXTI resync + SPI2 slave DMA capture | **keep** (wrap in a snapshot) | sound idea, works on the bench |
| Packet unscrambling, `Reorder[]`, bitmap decode table | **keep** | verified against about 60 photographed states and the menu walk |
| `LineFmt` layout table, `DrawLineWithGlyphs` | **keep** | one place for geometry and glyphs |
| Glyph region at `0x200000` (`CGRAM_BASE_MAIN`) | **keep** | correct once refresh works |
| Settings page, boot counter, build options, bench tools | **keep** | |
| ST7701S init tables in `lcd.c` | **keep** | panel-specific, proven; only the delay helper needs fixing |
| `Main_Aux_R6581` | refactor | 47-branch if-chains become a straight copy; debug strings go behind a flag |
| `DisplayMain` 1 V mode | refactor | split out into its own function, built only with `ONE_VOLT_MODE` |
| `DisplayAnnunciators` | refactor | draw only the ones that changed |
| `ClearScreen` | refactor | one hardware fill |
| LT7680 SPI layer | **rework** | dominant cost; no error handling |
| LT7680 init (SDRAM, PLL, verification) | **rework** | the corruption bug; out-of-range PLL |
| Frame pipeline and tick model | **rework** | torn reads; full redraw every 35 ms whatever changed |
| Dead code | remove | `SendAllToLT7680_LT_2`, `DrawText_old`, `DrawTextChunks`, `Ohms*SymbolStoreUCG`, `FillScreen`, commented-out blocks |

## Target structure

Today's `main.c` (decode, flash, main loop, timing UI) and `lt7680.c` (bus, init, text, glyphs, logo) become small modules
with one job each:

| Module | Job | From |
| --- | --- | --- |
| `lt_bus.c` | chip select, register write/read, status, timeouts; direct SPI registers; optional state cache | `lt7680.c` core commands |
| `lt_init.c` | reset, PLL from panel timings, SDRAM from MCLK, panel, readback check | `lt7680.c` init |
| `lt_draw.c` | font setup, text, glyphs, fill, line, logo | `lt7680.c` drawing |
| `vfd_capture.c` | EXTI + DMA, frame-complete snapshot, frame counter | `stm32f1xx_it.c`, `main.c` |
| `vfd_decode.c` | unscramble, bitmap to char, annunciators: snapshot to a `Screen` struct | `main.c` |
| `screen.c` | the `Screen` model (main 18, aux 29, annunciators 18, display-off flag) and the diff against what was drawn | new |
| `render.c` | draws a diff using `LineFmt`; DISPLAY OFF handling; 1 V mode | `display.c` |
| `settings.c` | flash page, timing sets, boot counter | `main.c` |
| `main.c` | init, then: new stable frame? decode, diff, render | |

The main loop becomes:

```
for (;;) {
    if (!capture_take_frame(&raw)) { idle(); continue; }   // only complete frames, copied out of the DMA buffer
    decode(&raw, &now);
    if (!same_as(&now, &prev)) { prev = now; continue; }   // accept a screen only after two identical frames (~18 ms)
    render_diff(&shown, &now);                             // draws only changed cells; updates `shown`
}
```

The two-frame rule removes torn and transitional frames. Latency drops from up to 35 ms (tick) + 25 ms (draw) to about
18 ms + the draw of whatever changed.

(Later changed: the GPIB tests after Stage 7 showed the two-frame rule wasn't needed, because copying only whole frames
already removes the torn ones, and that it froze the display when the reading changed every frame. Every new frame is
now decoded at once; see [After Stage 7](#after-stage-7-boot-capture-rule-and-gpib-tests-2026-10-30).)

## Stages

Each stage lists what changes, how it's checked, and how to turn it off. Stages 1 and 2 are the correctness fixes and
should ship on their own before any performance work.

### Stage 0: baseline and instruments

Goal: measure before changing anything, so every later stage has a comparison.

- Add `-DLT_REGDUMP=1`: after init, read `E0h-E4h` in the normal bank, then set `E4h` bit 2 and read `E0h-E3h` in the
  timing bank, then clear it. Also read `05h-0Ah` and the status register, and store them all in a `volatile` array for
  `tools/swd.py`.
- **Built:** `LT_REGDUMP` in `lt7680.c`/`lt7680.h`, hooked in `main.c` after `SendAllToLT7680_LT()` and in the main loop
  (`lt_regdump_request`), and `tools/regdump.py`. With the option off, the `-O2` image is byte-identical to `no1v-O2`.
  To run it on the bench:

  ```bash
  OPT=2 EXTRA_CFLAGS="-DLT_REGDUMP=1" bash docs/tools/build.sh regdump-O2
  python docs/tools/regdump.py --elf docs/tools/images/regdump-O2/firmware.elf --save regdump-boot.json
  # after the DISPLAY OFF soak:
  python docs/tools/regdump.py --elf docs/tools/images/regdump-O2/firmware.elf --again --save regdump-soak.json
  ```

  The dump switches `E4h` to the timing bank and back, using the same two `E4h` writes `SDRAM_Init_LT()` already makes
  (bit 2 set, then cleared), and never writes bit 0 (start init) as 1.
- Record from the current `-O2` build:
  - the register dump
  - the `TIMING_PROBE` numbers
  - a DISPLAY OFF soak photo set (see [Test protocol](#test-protocol))
- **Done when** the dump confirms the table above (refresh `0000h`, TRCD/TRAS `0h`), and there is a reference photo of
  the corruption after a long DISPLAY OFF.

#### Stage 0 results (2026-10-29)

**Setup:**
- R6581T with the Blue Pill, ST-Link V2 (target 3.28 V)
- Board running the `no1v-O2` test build (5339ab5), identical to `Prebuilt/test-2026-10-17-O2-bootscreen`
- Settings page `17 14 2 50 30 10 60 AdaF`, boot count 10
- Flash backup taken twice and compared: `backups/board_flash_before-rework_2026-10-29.bin` (sha256 `4c42dd9f...dc0a`)
- Dumps saved in `captures/regdump/` (project folder)

| Register | Current build (`regdump-O2`) | Stage 1 (`stage1-regdump-O2`) | Expected |
| --- | --- | --- | --- |
| PCLK / MCLK / CCLK | 29 / 58 / 58 MHz | same | |
| PCLK PLL VCO | **58 MHz, outside 100-500** | same (Stage 2) | 100-500 MHz |
| `E0h` attributes | **`28h`: 4 banks, 4,096 rows, 256 columns** | `29h`: 512 columns | `29h` |
| `E1h` CAS latency | 3 | 3 | 3 |
| `E2h-E3h` refresh | **`0000h`, auto-refresh disabled** | 870 clocks = 15.0 us per row | <= 15.625 us |
| Timing bank `E0h-E3h` | **`29 03 9A 00`**: TRFC 1, TRCD 1, TRAS 1 clock | `02 87 20 26` (reset values): TRFC 9, TRCD 3, TRAS 7 | |
| `E4h` | `11h` | `11h` | |
| Status at the end of init | `54h`: SDRAM ready | `54h` | |
| SDRAM init waits timed out | | 0 | 0 |

**Answers to the open questions:**
- `E0h` resets to `28h`, so the current build runs the SDRAM as 8 MB with 256 columns. Everything in use is below 8 MB.
- `E4h` bit 0 does read back 1 once initialisation is done. Status bit 2 agrees.
- Bit 4 of `E4h` also reads 1 (not documented in the extracted table).
- The timing-bank reset values (`02 87 20 26`) match the datasheet defaults. Stage 1 leaves them alone.

On the Stage 1 build the meter decoded and displayed normally: `- 24.57779 mVDC`, `100mV Range`, annunciators
SMPL/AUTO/AZERO/FRONT, no unmatched characters.

### Stage 1: SDRAM initialisation

**Built** on `rework/stage1-sdram`:
- `SDRAM_Init_LT()` writes the normal bank with `E4h` bit 2 clear.
- `lt_mclk_mhz` is set by `LT7680_PLL_Initial_LT()`.
- `SDRAM_SetRefresh_LT()` calculates the refresh interval from MCLK (`SDRAM_REFRESH_NS` = 15 us). Timing-adjust mode
  calls it again after reprogramming the PLL.
- The ready-wait checks both `E4h` bit 0 and status bit 2, with a 100 ms timeout counted in `lt_sdram_init_timeouts`.
- `-DSDRAM_INIT_LEGACY=1` builds Ian's sequence.
- The register checks above pass, and so does the retention soak below. The visual checks are deferred.

#### SDRAM retention soak (2026-10-30)

There's no way to see the panel yet, so the corruption is measured directly instead (`-DSDRAM_SOAK=1`,
`tools/soak.py`):

- The firmware writes a fixed pseudo-random pattern of 1,024 bytes (8,192 bits) into each of 4 areas of SDRAM at
  `0x300000` + n x 64 KB. The panel never scans those areas, just like the glyph store.
- It reads each area back on its own schedule and counts wrong bits. Reading a row also refreshes it, which is why each
  area has a different interval.
- Each pattern is read straight back after writing. That gave 0 errors in every run, and the first bytes matched the
  expected pattern, so the read path itself is correct.

Each build ran for 11 minutes on the same board, with the meter on 100 mV DC. Figures are the most bits wrong at any
read:

| Build | read every 10 s | every 60 s | every 10 min | not read for 10-11 min |
| --- | --- | --- | --- | --- |
| `soak-legacy-O2`: Ian's SDRAM init (the current build) | 0 | **9** | **243** | **280** |
| `stage1-soak-regdump-O2`: Stage 1 | 0 | 0 | 0 | 0 |
| `stage2-soak-regdump-O2`: Stage 1 + Stage 2 clocks (MCLK = CCLK = 100 MHz, refresh 1,500) | 0 | 0 | 0 | 0 |

With refresh off, a row that isn't read loses bits within a minute, and about 3% of them within 10 minutes. That is
the corruption of the glyph store, and of the whole display buffer during DISPLAY OFF. With Stage 1 nothing is lost.
Logs and register dumps are in `captures/soak/` (project folder).


Goal: refresh on, timings sane, the correct memory layout. Small, isolated, and the most likely cure for the corruption.

- New `SDRAM_Init_LT(mclk_mhz)`:
  1. Optionally, with `E4h` bit 2 set, write the timing bank. Default: leave it at reset values. Write it only if Stage 0
     shows the reset values are wrong.
  2. Clear `E4h` bit 2.
  3. Write `E0h = 0x29`, `E1h = 0x03`, then `E2h/E3h = MCLK_Hz x 64 ms / 4096 - margin`. Integer maths in Hz, not
     `1000 / MCLK`. Use 4,096 rows, as the datasheet table says for the LT7680A-R.
  4. Write `E4h = 0x01`, then poll status register bit 2 (SDRAM ready), with a timeout.
- `SDRAM_ROWS` becomes 4096. Delete `SDRAM_MCLK` and `SDRAM_CLKFREQ`. The refresh value is calculated from the MCLK that
  the PLL code actually programs.
- Build option `SDRAM_INIT_LEGACY=1` restores Ian's sequence for A/B comparison.
- **Checks:**
  - the register dump shows the intended values in both banks
  - the menu walk is unchanged
  - a 60-minute DISPLAY OFF soak shows no corruption
  - the ohm glyph is intact after an hour on a non-ohm screen
- **Follow-up:** once this holds, try dropping the reload of the glyphs after DISPLAY OFF (`CheckDisplayStatus`). It
  should no longer be needed. Keep it until a soak test proves that.

### Stage 2: clocks

**Built** on `rework/stage2-clocks`:
- `SetPLL_LT()` programs each PLL with R = 5 and OD = /4, so the VCO is 4 x the output. Examples: PCLK 29 MHz gives a
  116 MHz VCO, and 100 MHz gives 400 MHz.
- PCLK follows the panel timings in 0.5 MHz steps.
- MCLK and CCLK are `LT_MCLK_MHZ` and `LT_CCLK_MHZ` (default 100 / 100), with the datasheet rules enforced.
- `lt_cclk_mhz` and `lt_pclk_khz` record the clocks actually programmed.
- The backlight PWM prescaler is calculated from CCLK so the PWM stays about 3.3 kHz.
- Timing-adjust mode keeps the boot count across its settings-page erase.
- `-DLT_CLOCKS_LEGACY=1` restores Ian's clocks.

**Bench (2026-10-30):**
- PCLK 29.0 MHz (VCO 116), MCLK 100.0 MHz (VCO 400), CCLK 100.0 MHz (VCO 400).
- Refresh 1,500 clocks = 15.0 us.
- Timing bank at its reset values.
- SDRAM retention soak clean (table above).
- The meter decodes normally.
- The visual checks (flicker, colours, backlight at the new PWM prescaler, all 6 timing sets) are deferred.

#### Deferred visual checks

To run now the panel can be seen. Build: `Prebuilt/rework-2026-10-30-O2/`, or the same source with `-DLT_REGDUMP=1` to also read
the register dump.

1. Panel is stable, with no flicker or colour faults. In particular, check the pixel clock: its frequency is unchanged,
   but its PLL now runs inside the VCO range.
2. Backlight brightness and no visible PWM beat. The prescaler went from 16 to 28 to keep about 3.3 kHz at the 100 MHz
   core clock.
3. Timing-adjust mode (hold DCV at power-up): step through all 6 sets, and check the boot count survives.
4. DISPLAY OFF for 60 minutes, then on: the whole panel is clean, including the left edge and the gaps between rows.
5. Ohms screen after an hour on another screen: the ohm glyph is intact.
6. Right edge (Stage 6): the wipe is off by default. Display memory there reads black in every check, so anything
   still seen at the edge would not come from memory content. Look for it anyway; `-DEDGE_WIPE=1` brings the wipe back
   on full repaints.
7. Glyph self-test pages and a menu walk, compared with the photos of the current build.
8. Boot (Stage 3): the panel comes up cleanly with the shorter reset and without the fixed gaps; no flash of garbage.
9. Menu walk with `menu_logger.py` (Stage 5 asked for it): it needs someone at the front panel, but not the panel
   itself. Compare the screens and characters logged with the earlier walk.
10. ~~Cold power-up (Stage 7)~~: done. The first try showed the PA12 timing-mode problem, which is now fixed (Stage 7
   results). If a power-up ever leaves the display blank again, read `timingModsOnBoot`, `lt_fault` and
   `lt_init_attempts` over SWD; `-DBOOT_SETTLE_MS=1000` restores the old wait.
11. Fault line (Stage 3): the red "LT7680 init fault" line is readable. To force it, build with a short
   `SDRAM_INIT_TIMEOUT_MS`; the fault test build (`-DLT_FAULT_TEST=3`) only exercises the no-reply path.

Goal: every PLL inside its range, and memory and core clocks chosen deliberately instead of as 2 x PCLK.

- **PLL setup:**
  - Choose OD/R/N per clock so that VCO = 10 MHz x N / R is in 100-500 MHz.
  - Keep R = 5 (2 MHz input). With OD = /4, N = 2 x Fout and the VCO = 4 x Fout, which suits PCLK 25-65 MHz.
  - Fix MCLK and CCLK at a safe value, about 100 MHz, independent of PCLK, while honouring the datasheet rules above.
  - Start at 80 MHz if the bench shows any doubt.
- **Refresh:** Stage 1's refresh value follows MCLK automatically.
- **Timing-adjust mode:**
  - Re-run the SDRAM refresh write after any PLL change.
  - Rewrite the boot-counter words after the page erase, or move the boot counter to its own page.
- **Brightness prescaler:** it assumes a 54 MHz core clock, so recalculate it for the new CCLK.
- Build options `LT_MCLK_MHZ` and `LT_CCLK_MHZ`.
- **Checks:**
  - register dump
  - no flicker or colour faults at each of the 6 timing sets
  - `TIMING_PROBE` (a faster CCLK should shorten the text engine's busy time)
  - a long soak at the new clocks

### Stage 3: init robustness

Goal: no silent hangs, a real reset, and a visible fault instead of a blank panel.

- **Timeouts:** every poll loop gets one. A failed init retries once with hardware reset, then shows a fault code in
  the boot-screen font, or blinks the test LED if the panel isn't usable.
- **Reset:** `Software_Reset_LT()` writes `00h` bit 0, or is deleted and the hardware reset relied on.
- **Wait for PLL lock** via `01h` bit 7 / `00h` bit 7 instead of fixed delays.
- **Delays:** trim the twenty-odd `HAL_Delay(5)` and `HAL_Delay(100)` calls in `SendAllToLT7680_LT()` to what the
  datasheet requires (PLL lock up to 500 us, SDRAM ready by polling). That gives a faster boot.
- **Ready-wait:** `WaitForLT7680Ready()` returns a status, and callers count timeouts in a `volatile` fault counter.
- **Checks:** pull the LT7680 reset line during boot (no hang); boot time measured before and after.

#### Stage 3 results (2026-10-30)

What was built:

- `LT_Init_LT()` resets the controller and runs `SendAllToLT7680_LT()`, which now returns fault bits, and retries once
  on any fault. A fault where the controller doesn't answer at all (`LT_FAULT_NOREPLY`) blinks the test LED (PC13) and
  retries every 1.5 s, forever. Any other fault shows a red line for 3 s, and boot carries on.
- The reset pulse is 2 ms instead of 100 ms. The wait for status bit 1 (inhibit) to clear replaces the 100 ms after it.
- The PLL switch polls `00h` bit 7 and then reads `05h-0Ah` back. The SPI clock runs at 1.1 MHz until the PLLs are
  running.
- The twenty fixed 5 to 100 ms gaps between register groups are gone. `-DLT_INIT_LEGACY=1` puts them back.
- `WaitForLT7680Ready()` returns 0 on a timeout and counts it in `lt_busy_timeouts`.
- `regdump.py` prints the init results.

Found on the bench:

- **Software reset:** after a write of 01h to `00h`, register writes for a while are lost, even though the status
  register already reads idle. Ian's 10 ms wait was needed and stays (`LT_SWRESET_MS`).
- **PLL registers:** `05h-0Ah` read back the settings in use (reset values 14 3C 14 85 14 64), not what was just
  written, until the switch. The read-back check therefore comes after the switch.
- **Status bit 1:** it was clear on the first poll every time, so in practice it doesn't hold anything up.

Measurements on the board (no boot screen; the boot count was past 10):

| Build | Init (reset to the end of `ClearScreen`) | Live loop starts |
|---|---|---|
| `LT_INIT_LEGACY=1` | 944 ms | 2,888 ms |
| Stage 3 | 416 ms | 2,364 ms |
| Stage 3, `LT_FAULT_TEST=3` (controller held in reset for 3 resets) | 834 ms (last call) | 5,125 ms, recovered on reset 4, fault 00 |

Most of the 416 ms is `ClearScreen()` writing about 1,500 characters; that is Stage 6's job. The 1 s `BOOT_SETTLE_MS`
before init is still there, because nothing says what it is for. Try it shorter once the panel can be watched.

The fault path where the controller answers but its set-up fails (the red line) is built but not yet seen; see the
deferred visual checks.

### Stage 4: capture pipeline

Goal: only whole, stable frames reach the screen; the CPU is idle when nothing changes.

- **Snapshot:**
  - In `HAL_SPI_RxCpltCallback` for SPI2, copy the 235 bytes to a snapshot buffer and bump a frame counter.
  - The main loop reads the snapshot with the counter checked before and after (seqlock), or with the copy done under a
    short `__disable_irq()`.
- **Delete:**
  - the SPI2/DMA4/DMA5 "pause" and resume around drawing
  - the SPI1 `SPE`/DMA3/NVIC toggling in the main loop
- **Decode:** only when a new frame arrives. Accept a screen after two identical consecutive frames.
- **Main_Aux_R6581:** becomes a loop. The debug strings go behind `-DDECODE_DEBUG=1`, with correctly sized buffers. That
  removes the out-of-bounds write.
- **EXTI handler:** keep the resync. Count how often a frame is cut short (DMA not complete at the next EXTI) as a health
  counter.
- **Checks:**
  - menu walk, decoded-frame log: zero `?` over 10,000 frames at `-O0` and `-O2`
  - the 240 fps comparison for latency

#### Stage 4 results (2026-10-30)

What was built:

- **Snapshot:** the EXTI handler copies the frame the DMA has just finished into `cap_frame[]` before it restarts the
  DMA, and only if all 235 bytes arrived. Otherwise the frame is counted as short. This uses the DMA counter, not the
  HAL completion callback.
- **Stable frames only:** the main loop takes the snapshot with interrupts off for the 235-byte copy. It decodes only
  when the same bytes have come twice in a row and differ from what is decoded already.
- **Deleted:** the SPI2/DMA "pause" around drawing, and the SPI1 `SPE`/DMA3/NVIC toggling.
- **Decoder:** `Main_Aux_R6581()` is a loop over the 47 characters. The out-of-bounds `main_display_debug` and the unused
  `LCD_buffer_*` and `main_display_line` are gone. The bitmap text is behind `-DDECODE_DEBUG=1`, in a correctly sized
  buffer.
- **Options and tools:** `-DCAPTURE_LEGACY=1` restores the old capture for comparison. `tools/capture.py` reads the
  counters.

Bench, 5 minutes per build, meter on 100 mV DC, reading changing about 6 times a second:

| Build | Frames/s | Frames cut short | Decodes/s | Decodes with `?` | Redraws with `?` | Main-loop passes/s |
|---|---|---|---|---|---|---|
| `CAPTURE_LEGACY=1`, -O0 | 83.4 | 8,478 (25%) | 157 | 18 | 5 of 8,822 | 157 |
| `CAPTURE_LEGACY=1`, -O2 | 83.5 | 8,482 (25%) | 428 | 88 | 6 of 8,833 | 429 |
| Stage 4, -O0 | 110.8 | 0 | 5.8 | 0 | 0 of 8,817 | 222,558 |
| Stage 4, -O2 | 110.8 | 0 | 5.8 | 0 | 0 of 8,829 | 1,218,685 |

Each Stage 4 run decoded about 34,000 frames, over the 10,000 the check asked for.

- **The pause was worse than useless.** It stopped SPI2 in the middle of a frame, so a quarter of all frames were cut
  short. The `?` came from decoding those torn buffers.
- **Rejected frames:** every real change costs one rejected frame (its first sighting) before it is decoded. Stage 4
  rejected 425 more than that in 311 s (2,214 rejected against 1,789 decodes), about 1.4 a second. These are frames
  caught while the meter was updating its display, and the old path would have decoded them.
- **Idle time:** with nothing to decode, the main loop now spins about 1.2 million times a second at -O2. That is the
  time Stages 5 and 6 can use.

Still to check: the 240 fps latency comparison needs the panel (see the deferred visual checks). The two-frame rule
adds 9 ms at most.

### Stage 5: LT7680 bus layer

Goal: make each register access cheap.

- **Direct registers:**
  - Write `SPI1->DR`, wait on `TXE`/`BSY`, and toggle CS with `GPIOA->BSRR` (no HAL).
  - Reads use `RXNE`.
  - Keep `HAL_SPI_Init` for set-up only.
- **SPI clock:** 9 MHz to 18 MHz (prescaler 4). Check signal integrity with a scope, and with a register write/readback
  loop of 100,000 patterns.
- **Bench experiment:** can several data bytes follow one `0x80` control byte inside one CS frame? The datasheet's 4-wire
  diagrams show one byte per frame. If bursts work, glyph loading and memory writes get much faster. If not, keep one byte
  per frame.
- **State cache in `lt_draw.c`:** remember the last value written to each of these and skip writes that don't change
  them. `SetCGRAMStart_LT` already does this for its one register.
  - colours `D2h-D7h`
  - `CCh`, `CDh`, `D0h`, `D1h`
  - text mode `03h`
- **Build option** `LT_BUS_HAL=1` keeps the old path for A/B comparison.
- **Checks:** `TIMING_PROBE` before and after; a full menu walk.

#### Stage 5 results (2026-10-30)

What was built:

- **Direct registers:** `WriteRegister`, `WriteData`, `ReadStatus` and `ReadData` drive `SPI1->DR` and the CS pin
  directly, one CS frame per control byte and data byte as before.
- **SPI clock:** 18 MHz after the PLLs run (`LT_SPI_PRESC`).
- **Address skipping:** `WriteRegister` only records the register. The address frame goes out with the next data write
  or read, and only if it differs from the last address sent.
- **Register cache:** registers only the firmware changes (03h, CCh, CDh, D0h-D7h) are shadowed, and a write of an
  unchanged value is skipped. This went into the bus layer rather than a separate `lt_draw.c`, so every caller gets it.
  A reset forgets it all.
- **Build options:** `-DLT_BUS_HAL=1` restores Ian's functions at 9 MHz, `-DLT_REG_CACHE=0` turns the cache off,
  `-DLT_SPI_STATS=1` counts frames, and `-DLT_BUS_TEST=1` runs the bench test below. In `LT_REGDUMP` builds the cache
  is compared with the chip once a second.

Bench:

- **Bus test at 18 MHz:**
  - 100,000 register write/read-back pairs on D2h, D5h and 63h: 0 errors.
  - 256 bytes written to SDRAM one byte per frame, and read back: 0 errors.
  - 256 bytes written as one CS frame (`0x80` followed by all 256 data bytes), and read back: 0 errors.
  - **So bursts work.** That's the tool for Stage 6 and for glyph loading.
- **Register cache:** 759 checks against the chip over a minute of live running: 0 mismatches. About 10% of frames are
  skipped by the cache and address skipping together.
- **SDRAM soak at 18 MHz:** read straight back after writing, 0 errors; the 10 s and 60 s regions after 3 minutes, 0 bit
  errors.
- **Capture:** still 0 short frames and no `?`.
- **Timing** (`TIMING_PROBE`, 72 MHz CPU, about 40 s of live running each; the block includes the fixed 6 ms wait):

  | Build | Redraw (ms) | Whole timed block, max (ms) |
  |---|---|---|
  | `LT_BUS_HAL=1` (9 MHz) | 15.0 | 21.0 |
  | direct registers, 9 MHz | 7.1 | 13.0 |
  | direct, 18 MHz, `LT_REG_CACHE=0` | 7.4 | 13.4 |
  | direct, 18 MHz, cache (Stage 5) | 6.6 | 12.5 |

  The HAL calls were half the redraw. The SPI clock and the cache matter little now: what's left is waiting for the
  text engine between characters, and the full redraw itself, which is Stage 6's job.
- **Boot:** init takes 174 ms instead of 416, and the live loop starts at 1.64 s instead of 2.36 s. That's the faster
  `ClearScreen()`.

### Stage 6: redraw engine

Goal: draw only what changed, as soon as a stable frame arrives.

- **`render_diff(shown, now)`:**
  - **Main and aux lines:** compare cell by cell. For each run of changed cells, set the cursor at
    `y0 + i x pitch` (from `LineFmt`) and draw just that run. Glyph cells go through the glyph path as now.
  - **Annunciators:** draw only those whose state changed.
  - **Colour change** (for example ERROR): forces a redraw of that item.
  - **DISPLAY OFF, display ON, or a layout change:** forces a full redraw.
- **Remove:**
  - the 35 ms TIM2 tick (or keep it as a watchdog-style full redraw every few seconds, behind `-DFULL_REDRAW_MS`)
  - the 6 ms post-draw wait
  - the right-edge wipe, if Stage 1 has removed the "random pixels". Check this with the wipe off on a test build first.
- **Primitives:**
  - `DrawLine()` waits for the engine before it returns.
  - `ClearScreen()` becomes one filled rectangle (`76h` drawing engine) or a BTE solid fill (`90h-91h`).
- **1 V mode:** stays a build option, as its own function.
- **Build option** `FULL_REDRAW=1` restores redraw-everything for A/B comparison.
- **Checks:**
  - glyph self-test pages
  - menu walk, reading the screen back over SWD against the decode
  - 240 fps video for latency and tearing
  - an overnight run on a changing reading (autorange stepping, statistics screen)

#### Stage 6 results (2026-10-30)

What was built:

- **`Render_Update()`** (`display.c`): builds the main and aux lines exactly as `DisplayMain()` and `DisplayAux()` do,
  including the 1 V mode and the ohm and sigma glyphs. It compares them cell by cell with what it last drew, and draws
  each run of changed cells at `y0 + i x pitch`. Only the annunciators that changed are drawn.
  - **Full redraw** the first time, after DISPLAY OFF, when a colour changes, and every `FULL_REDRAW_MS` (10 s).
  - **When it runs:** as soon as `Capture_Poll()` decodes a new stable frame. The 35 ms tick still reads the DCV button
    and calls it too, so a 1 V mode toggle is drawn without a new frame. An unchanged screen costs no SPI traffic.
- **Removed from the live loop:** the 6 ms wait, and the redraw of everything every tick.
- **Right-edge wipe:** off. `-DEDGE_WIPE=1` brings it back before each full repaint. It can't stay on every draw: rows
  952-955 are part of the last aux cell, which a partial draw doesn't repaint.
- **Primitives:**
  - `DrawLine()` waits for the drawing engine before it returns.
  - `ClearScreen()` is one filled rectangle from the drawing engine (`76h`); `-DLT_CLEAR_TEXT=1` restores the spaces.
- **Refactors on the way:**
  - The glyph line drawer takes a cell range (`DrawCells`).
  - The 1 V conversion is its own function (`MainToOneVolt`).
  - Each annunciator draws through `DrawAnnunciator()`.
  - Ian's `DisplayMain/Aux/Annunciators` still exist: the glyph self-test, `FULL_REDRAW=1` and the check below use them.
- **Build options:** `FULL_REDRAW=1` (Ian's loop, for A/B), `FULL_REDRAW_MS`, `EDGE_WIPE`, `LT_CLEAR_TEXT`, and
  `RENDER_VERIFY` (bench).

Bench, no panel needed. `-DRENDER_VERIFY=1` reads display memory back over SPI (`LT_ReadPixels`):

- **The engine draws the same pixels as Ian's full redraw.**
  - **Method:** hash the screen in three bands (annunciators, main, aux; memory X 128-351, all 960 rows), redraw
    everything with Ian's functions, and hash again.
  - **Self-test:** 60 random synthetic screens before the live loop. The changes mix digits, letters, the ohm and sigma
    placeholders, the micro sign, whole new lines and annunciator toggles. 0 mismatches, 654 cells drawn.
  - **Live:** 12 checks against the meter's own changing reading. 0 mismatches.
- **Right edge:** with the wipe off, memory rows 952-959 had no lit pixel outside the text cells that reach them, in 72
  checks.
- **`ClearScreen()`:** after the rectangle fill, all 384,000 pixels of the page read black.
- **Drawing engine:** `DrawLine` and `ClearScreen` waits had 0 timeouts, and `WaitForLT7680Ready` 0 timeouts.
- **Capture (5 minutes each, `-O0` and `-O2`):** 0 short frames, 0 unmatched decodes, 0 redraws with a `?`.
  - Redraws: 5.9 per second, one per decoded change, against about 28 for the tick-driven loop.
  - Main-loop passes at `-O2`: 1.66 M per second (Stage 4: 1.22 M); at `-O0` 0.63 M (Stage 4: 0.22 M).
- **Timing** (`TIMING_PROBE`, -O2, 72 MHz, about 45 s live each). Latency runs from the end of the first frame that
  shows a change to the end of its draw; the stable-frame rule alone accounts for one frame (about 9 ms).

  | | `FULL_REDRAW=1` (Ian's loop) | Stage 6 |
  |---|---|---|
  | Redraw, min / last / max (ms) | 2.9 / 6.8 / 6.9 | 0.17 / 1.1 / 6.2 (the max is the 10 s full repaint) |
  | Whole timed block, max (ms) | 12.9 | (no block) |
  | Change-to-screen latency, min / avg / max (ms) | 16.8 / 32.6 / 50.7 | 10.3 / 10.7 / 14.8 |
  | Redraws per second | 29 | 6 |

  A typical change (the last digits of the reading and a blinking annunciator) is 3 or 4 cells and about 1 ms of drawing.

Still to do with the panel visible: the glyph self-test pages, the menu walk, 240 fps video of latency and tearing, and
an overnight run on a changing reading. These are in the deferred checks.

### Stage 7: structure and clean-up

- Split the files into the modules in [Target structure](#target-structure).
- Delete the dead code listed above.
- Fix `DelayMicroseconds()` (use DWT `CYCCNT`) and the TIM2 flag clear.
- Update `CHANGES-AND-FINDINGS.md`, `BUILD-AND-FLASH.md` and the build script for the new files and options.
- This stage is mechanical. It goes last so that every earlier stage diffs cleanly against Ian's code.

#### Stage 7 results (2026-10-30)

What was done:

- **Files split by job.** The table in [Target structure](#target-structure) became:

  | File | From | What |
  |---|---|---|
  | `lt_bus.c` | `lt7680.c` | bus layer, register cache, busy wait, bus test and cache check |
  | `lt_init.c` | `lt7680.c` | reset, PLLs, SDRAM, panel timing, windows, backlight |
  | `lt_draw.c` | `lt7680.c` | font set-up, text, colours, line, fill, glyphs, boot logo |
  | `lt_bench.c` | `lt7680.c` | register dump, SDRAM soak, display memory read-back |
  | `vfd_capture.c` | `main.c` | DMA buffer, frame-end copy, stable-frame rule |
  | `vfd_decode.c` | `main.c` | unscramble, bitmap table, annunciators |
  | `settings.c` | `main.c` | flash page, timing sets, boot counter, `Settings_Load()`, `TimingAdjust_Poll()` |
  | `render.c` | `display.c` | `LineFmt`, Ian's full-redraw functions, the redraw engine and its check |
  | `display.c` | | boot screen, fault line, splash, glyph self-test |
  | `main.c` | | start-up and the main loop |

  The moved code is unchanged apart from what joins the pieces: headers `vfd.h` and `settings.h`, `lcd.h` for the panel
  set-up, and the latency probe's hand-off from capture to `main.c`. There is no separate `screen.c`: `G[]` and
  `Annunc[]` stay the screen model, and the comparison with what was drawn is in `render.c`. The VisualGDB project
  lists the new files.
- **Dead code deleted:** `SendAllToLT7680_LT_2`, `Ohms16x32/12x24SymbolStoreUCG`, `DrawTextChunks`, `DrawText_old`,
  `SetFontTypeSize`, `CGRAM_Start_address`, `Font_Select_UserDefine_Mode`, `SoftwareReset`, `Software_ResetPLL_LT`,
  `SetBacklightFull`, `FillScreen`, the commented-out blocks between them, `HAL_SPI_TxCpltCallback`,
  `RunBluePillSpeedTestOnline`, `DisplayCloneDeterminationMain/Aux`, and unused globals.
- **Fixes:**
  - `DelayMicroseconds()` counts CPU cycles (DWT). The old SysTick version's wrap check could never be true, so a
    delay could end early.
  - The TIM2 handler clears its flag with a plain write (`SR = ~UIF`) instead of a read-modify-write that could
    lose another flag.
  - `SetTimerDuration()` no longer causes an extra tick.
  - The implicit declaration GCC 14 complained about is gone.
- **Boot path** (asked for during this stage):
  - The boot screen is off by default (`BOOT_DIAG_SHOWS=0`).
  - The 1 s wait before the LT7680 set-up is 20 ms (`BOOT_SETTLE_MS`).
  - Two unexplained waits (10 ms and 5 ms) and a duplicate `ClearScreen()` are gone.
  - Capture starts as soon as the peripherals are up, not at the end of start-up.
  - New timestamps for SWD: `boot_lt_ms`, `boot_panel_ms`, `boot_ready_ms` and `first_frame_ms`.

Bench, with the same checks as Stage 6:

- **Builds:** every option combination builds at `-O0` and `-O2` with no new warnings.
- **Size:** `-O2` text is 29,252 bytes, against 29,304 for Stage 6.
- **Screen read-back:** 60 synthetic screens and 8 live checks, 0 mismatches. The synthetic screens drew the same 654
  cells as Stage 6, because the random sequence is fixed.
- **Right edge, cleared page, register cache:** right edge clean, cleared page black, register cache 440 checks with 0
  mismatches.
- **Timing (`-O2`):** change-to-screen latency 10.2 / 10.7 / 14.8 ms (min / avg / max), against 16.8 / 32.4 / 50.9 ms
  for `FULL_REDRAW=1`. The same as Stage 6.
- **Capture:** 5 minutes each at `-O0` and `-O2`, 0 short frames and 0 `?`.
- **Main-loop passes:** 1.09 M per second at `-O2` (Stage 6: 1.66 M). `Capture_Poll()` now lives in another file, so
  it is no longer inlined into the loop. This is idle time, not work.
- **Tick:** the 35 ms tick still runs. Render calls are about 32 per second: 28.6 ticks plus about 6 new frames.

Boot, from reset (10 SWD resets per build, all within a few ms, no retries or faults):

| Build | LT7680 set-up done | Panel set-up done | Live loop | First meter frame drawn |
|---|---|---|---|---|
| Ian's start-up (Stage 3 measurement, `LT_INIT_LEGACY`, text `ClearScreen`, no boot screen) | | | 2,888 ms | about 2.9 s |
| Stage 7 code with Ian's 1 s wait | 1,031 ms | 1,181 ms | 1,182 ms | 1,193 ms |
| **Stage 7, `-O2`** | **51 ms** | **201 ms** | **202 ms** | **214 ms** (210-216) |
| Stage 7, `-O0` | 52 ms | 204 ms | 205 ms | 217 ms |

With the boot screen built in (the pre-rework default for the first 10 boots), another 5 s comes first: 1 s of speed
test and 4 s of screen.

What's left in the 214 ms:

- 20 ms settle.
- 30 ms LT7680 set-up.
- 150 ms panel set-up. 120 ms of that is the ST7701S's mandatory wait after Sleep Out; the rest is the bit-banged SPI.
- About 12 ms for the first frame. Capture has been running, but the stable-frame rule still needs one more matching
  frame once the loop starts, and then the full draw.

The next saving, running the LT7680 set-up inside the panel's 120 ms wait, was done afterwards (below).

**Cold power-up:** the first one failed. The display stayed blank because the board was in timing-adjust mode. That
mode is chosen at start-up when PA12 reads high (DCV held), and on a cold power-up PA12 still reads high for a while,
before the meter drives its key lines. Ian's start-up read it after about 2.3 s; the trimmed boot read it at about
50 ms. SWD showed the LT7680 set up cleanly and frames decoding, but `timingModsOnBoot` was 1 and nothing had been
drawn. The settings page was untouched.

Fix: the live display starts at once, and the main loop reads PA12 at `TIMING_MODE_CHECK_MS` (2,300 ms), where Ian's
code did. If it's high, the screen clears and timing-adjust mode starts. Confirmed on the bench by power-cycling the
meter: the display comes up. Holding DCV through power-up to enter timing-adjust mode is still to be checked (deferred
item 3).

#### After Stage 7: boot, capture rule and GPIB tests (2026-10-30)

With the panel now visible on the bench, and the meter reachable over GPIB (NI-VISA `GPIB0::27::INSTR`, which answers
`ADVANTEST,R6581T,0,A01`; commands from `scpi_references/Advantest_R6581T_DMM_SCPI.md`).

**Boot, further trimmed:**

- **LT7680 set-up inside the panel's wait.** The ST7701S set-up is split around its mandatory 120 ms after Sleep Out
  (`AdaFruit_Init_Start/Finish`, `BuyDisplay_Init_Start/Finish`, `LCD_SLEEPOUT_MS`), and the LT7680 set-up runs in
  that wait. `AdaFruit_Init()` and `BuyDisplay_Init()` still do the whole thing, for the timing-adjust mode.
- **Settings now load first.** The panel type picks the panel set-up, so `Settings_Load()` moved ahead of the LT7680
  set-up. The side effect is intended: the LT7680 now starts with the stored TFT timings. Ian's start-up set it up
  before loading them, so it always started on the built-in defaults and only the panel type came from flash. Nothing
  changes on a unit whose stored set is the default set.
- **Faster panel SPI.** The bit-banged SPI to the ST7701S runs at a 1 us half-period instead of Ian's 5 us, with 1 us
  instead of 10 us around chip select (`LCD_BIT_US`, `LCD_CS_US`). The ST7701S's own limit is tens of nanoseconds.

Boot timeline (10 SWD resets each, all within a few ms of each other, no retries or faults):

| Build | Panel at Sleep Out | LT7680 set up | Panel set up | First meter frame drawable |
|---|---|---|---|---|
| Stage 7 as committed | | 51 ms | 201 ms | 214 ms |
| + LT7680 inside the panel wait | 28 ms | 58 ms | 148 ms | 162 ms |
| **+ 1 us panel SPI (`-O2`)** | **10 ms** | **50 ms** | **130 ms** | **140 ms** (138-143) |
| same, `-O0` | 12 ms | 51 ms | 132 ms | 145 ms |

The floor is now the panel: 10 ms of set-up plus its 120 ms after Sleep Out. The LT7680 starts at 20 ms
(`BOOT_SETTLE_MS`) and is done at 50 ms. On a cold power-up the meter sends its first frame later than that (below),
so the board waits for the meter.

**The meter's own start-up** (cold power-up, `-DBOOT_TRACE=1`, read with `tools/boottrace.py`):

| Time after power-on | Meter's display | Drawn at |
|---|---|---|
| 0.20 s | (nothing sent yet) | blank screen |
| 0.34 s | `R6581T` / `Ver.A01  GPIB Addr:27` | 0.341 s |
| 2.51 s | `RUNNING SELF TEST` | 2.513 s |
| 3.01 s | blank (the self-test text blinks) | 3.008 s |
| 3.22 s | `RUNNING SELF TEST` | 3.226 s |
| 3.59 s | `mVDC` / `100mV Range`, no digits yet | 3.588 s |
| 3.98 s | autorange to `1000mV Range` | 3.982 s |
| 4.42 s | first reading | 4.426 s |

PA12 read high only on the first tick (0.20 s) and low from 0.24 s on. The power-on screen used to be missed
entirely, because the display came up at about 2.9 s.

**Capture rule fixed.** An integration-time sweep over GPIB showed that Stage 4's rule (decode a frame only after
seeing the same bytes twice) freezes the display when the reading changes in every 9 ms frame, which happens at 0.1 PLC
and faster. The rule also capped updates at half the VFD's frame rate, and dropped changes that lasted only one frame.
The whole-frame copy (`Capture_FrameEnd`) is what removed the torn frames; the second-frame rule wasn't needed.

Now every new frame is decoded and drawn at once. Only a frame holding a pattern not in the font table waits to be seen
twice (`cap_heldback` counts those). `CAPTURE_CONFIRM=1` restores the old rule. `Decode_Text()` skips the table
search for cells whose bitmap hasn't changed.

Sweep (`tools/sweep.py`, 1 V range fixed, 30 s per step, `TIMING_PROBE` at `-O2`, meter input about -0.3 V):

| Meter setting | Frames/s | Changes drawn/s, two-frame rule | Changes drawn/s, now | Latency avg, now |
|---|---|---|---|---|
| 10 PLC, 7 digits | 110.8 | 5.8 | 8.0 | 0.89 ms |
| 1 PLC, 7 digits | 110.8 | 26.9 | 44.2 | 0.86 ms |
| 0.06 PLC (0.1 asked), 6 digits | 110.6 | **0 (frozen)** | 110.6 | 1.25 ms |
| 0.006 PLC, 5 digits | 110.6 | **0** | 110.5 | 1.15 ms |
| 0.00006 PLC (MIN), 4 digits | 110.7 | **0** | 110.7 | 1.02 ms |
| MIN, 4 digits, auto-zero off | 110.7 | 0.07 | 110.6 | 0.99 ms |

- **Frame rate:** the meter refreshes its display every 9.0 ms (110.5-110.9 frames a second) whatever the reading
  rate. That is the most often its display can change, and the board now follows it frame for frame.
- **Latency:** from frame end to draw end, about 1 ms. The worst case, 6.7 ms, is when the 10 s full repaint coincides.
- **Torn frames:** none. In every step, and in 3 minutes at the fastest setting (19,921 frames, 9,340 changes), 0
  frames were held back, 0 had an unknown pattern, and 0 short frames arrived. The meter has never handed over a
  half-written character since the whole-frame copy went in.
- **For comparison, Ian's loop** redrew at most 28.6 times a second (one per 35 ms tick), about 20 on ohm screens, and
  could draw a frame the DMA was still writing.

**DISPLAY OFF from GPIB:** `DISP OFF` makes the meter show `DISPLAY OFF`, and the board turned the panel and backlight
off (`displayBlank` 1). `DISP ON` brought them back, with the glyphs reloaded and a full repaint; the reading was back
within 3 s.

The meter's settings were saved before the tests and restored after (DCV, 100 mV autorange, 7 digits, 10 PLC,
auto-zero on), then the meter was returned to local.

## Test protocol

Run for every stage that ships an image:

1. **Register dump** (Stage 0 tool) over SWD, saved next to the image.
2. **Menu walk** with `tools/menu_logger.py`: the 95 known screens decode and draw the same as the baseline.
3. **Glyph self-test** (`-DGLYPH_SELFTEST=1`): every page photographed.
4. **Timing:** `TIMING_PROBE` at `-O0` and `-O2`: redraw time, period, worst case.
5. **DISPLAY OFF soak:**
   - 60 minutes off, then on; photograph the whole panel, including the overscan edge and the gaps between rows
   - repeat with the meter on an ohms screen
   - one overnight run on a fixed non-ohm screen, then switch to ohms
6. **Timing sets:** step through all 6 in timing-adjust mode.
7. **240 fps video** of a range change and a fast-changing reading (`tools/events240.py`).

## Open questions to settle on the bench

| Question | Settled by | Affects |
| --- | --- | --- |
| ~~Reset value of `E0h` (256 or 512 columns)~~ | **Answered:** `28h`, 256 columns | |
| ~~Does `E4h` bit 0 read back as "init done"?~~ | **Answered:** yes; status bit 2 agrees | |
| Are the reset timing-bank values right for this SDRAM at 100 MHz? | dump + soak at the Stage 2 clocks | whether Stage 1 writes the timing bank |
| ~~Multi-byte data after one control byte in a CS frame~~ | **Answered (Stage 5):** works; 256-byte burst read back with 0 errors | glyph and memory-write speed |
| ~~Highest reliable SPI1 clock on this wiring~~ | **Answered (Stage 5):** 18 MHz, the STM32F103's SPI maximum; 0 errors in 100,000 read-backs | bus speed |
| ~~Cause of the right-edge "random pixels"~~ | **Answered in memory:** with the wipe off, rows 952-959 read black in 72 checks; the panel itself is still to be seen | wipe off by default (`EDGE_WIPE`) |
| Is the per-character `WaitForLT7680Ready()` needed, or is the write-FIFO-full bit (status bit 7) enough? | Stage 5 | text speed |
