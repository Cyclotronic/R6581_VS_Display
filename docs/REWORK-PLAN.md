# Rework plan

A staged plan for reworking the display firmware: what is wrong, what to keep, and the order to change things in. Each stage
is small enough to build, flash and check on the bench before the next one starts, and each can be turned off at build time
until it has proved itself. Register references are to the LT768x datasheet V4.1 in `reference/lt7680/`.

Status (2026-10-29):

- **Stage 0 done:** the register dump on the bench confirms the SDRAM findings ([results](#stage-0-results-2026-10-29)).
- **Stage 1 built and flashed:** registers verified on the bench. The DISPLAY OFF soak hasn't been run yet. Branch
  `rework/stage1-sdram`.
- Later stages not started.

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
  - `Software_Reset_LT()` writes nothing.
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
- The register checks above pass. The soak and menu-walk checks below are still to do.


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

### Stage 7: structure and clean-up

- Split the files into the modules in [Target structure](#target-structure).
- Delete the dead code listed above.
- Fix `DelayMicroseconds()` (use DWT `CYCCNT`) and the TIM2 flag clear.
- Update `CHANGES-AND-FINDINGS.md`, `BUILD-AND-FLASH.md` and the build script for the new files and options.
- This stage is mechanical. It goes last so that every earlier stage diffs cleanly against Ian's code.

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
| Multi-byte data after one control byte in a CS frame | Stage 5 experiment | glyph and memory-write speed |
| Highest reliable SPI1 clock on this wiring | Stage 5 scope + readback loop | bus speed |
| Cause of the right-edge "random pixels" | Stage 6 with the wipe off | whether the wipe stays |
| Is the per-character `WaitForLT7680Ready()` needed, or is the write-FIFO-full bit (status bit 7) enough? | Stage 5 | text speed |
