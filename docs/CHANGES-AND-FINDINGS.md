# Changes and findings, in one place

A summary of everything done to Ian Johnston's firmware in this fork, what was found, what was checked, and what is still open.
Details and how-to are in the other notes in this folder; the per-build notes are in each `Prebuilt/` folder. Everything below was
tested on an R6581T with the original VFD-to-TFT hardware (STM32F103C8 "Blue Pill" + LT7680A-R), at `-O0` unless stated.

## Changes (all in `Core/`)

| # | Change | Why | Notes |
| --- | --- | --- | --- |
| 1 | Sigma (lowercase, the SIGMA row of the statistics screen) is decoded and drawn | The meter sends the 5x7 pattern `07 08 0E 11 11 12 0C`, which was not in the decode table, so the TFT showed `?` | Placeholder code `0x7F` in `bitmap_characters`; the glyph is a user-defined character |
| 2 | Ohm and sigma are user-defined glyphs **loaded once** into their own SDRAM region (`0x200000`) and reloaded after DISPLAY OFF | Ian's code never set the CGRAM start register (REG[DBh-DEh]), so the ohm glyph lived at address 0, the top-left of the display buffer, where drawing could overwrite it; that is why it was stored again every frame | Removes the per-frame reload and its fixed `Delay_NonBlocking` waits |
| 3 | One routine (`DrawLineWithGlyphs`) draws any line containing a glyph | Replaces duplicated `$`-splitting code in `DisplayMain` and `DisplayAux` | Adding a glyph is a table entry plus its bitmap |
| 4 | `ERR` is shown as `ERROR`, in red | Readability | Clears cleanly when the error is read |
| 5 | Larger annunciators (12x48, 24-dot font), INFO and SLOT blank | Closer to Hydron's layout; the originals were small | All 18 fit; SRQ is at the right end |
| 6 | Live layout table (`LineFmt`): annunciators 12x48, main 48x96, aux 32x64, 8 px gaps | One place to change sizes; sizes match Hydron's `display.c`, only positions differ | [LAYOUT.md](LAYOUT.md) |
| 7 | Speed test and splash moved to a boot screen shown for the first 10 boots only; hold DCV at power-up to reset the count; centred layout with Ian's character | The splash overlapped the live display; moving it frees the whole panel | [BOOT-AND-SPLASH.md](BOOT-AND-SPLASH.md) |
| 8 | Ian's 1 V display mode is a build option, off by default | Hydron did not want it | [ONE-VOLT-MODE.md](ONE-VOLT-MODE.md) |
| 9 | Prebuilt images of 2026-10-17 are built at `-O2` | Redraw 25 ms to 18.4 ms, identical decode | [TIMING-EXPERIMENTS.md](TIMING-EXPERIMENTS.md) |
| 10 | Build options: `BOOT_DIAG_SHOWS`, `BOOT_SCREEN_MS`, `SPLASH_IN_LOOP`, `ONE_VOLT_MODE`, `GLYPH_SELFTEST`, `TIMING_PROBE` | Every behaviour change can be undone at build time | All default to the behaviour described above |
| 11 | Build script for the Arm GNU Toolchain (no VisualGDB) | GCC 14 treats Ian's one implicit declaration as an error; the script downgrades it instead of editing his source | [BENCH-TOOLS.md](BENCH-TOOLS.md) |

### The rework (2026-10-29 and 30)

Stages 0 to 7 of [REWORK-PLAN.md](REWORK-PLAN.md), each with an A/B build option and checked on the bench, mostly without
a visible panel (it became visible after Stage 7). The results sections of that file have the numbers; the visual checks
still to do are listed there. A guide for reviewing the code is in [REVIEW-GUIDE.md](REVIEW-GUIDE.md), and the image is
[`Prebuilt/rework-2026-10-30-O2/`](../Prebuilt/rework-2026-10-30-O2/).

| Stage | Change | Why | Result |
| --- | --- | --- | --- |
| 0 | Register dump over SWD (`LT_REGDUMP`), timing probe extended | See what the LT7680 is really set to | SDRAM auto-refresh found disabled and its timing registers wrong |
| 1 | SDRAM set-up per the datasheet: refresh on, interval from the memory clock, init polled with a timeout | Display memory decayed: the likely cause of the ohm and left-column corruption after DISPLAY OFF | Retention soak: Ian's set-up loses up to 3% of bits in 10 minutes, the new one none |
| 2 | Every LT7680 PLL inside its VCO range; memory and core clocks 100 MHz, independent of the pixel clock | Ian's pixel-clock PLL ran below its minimum | Registers verified, soak clean |
| 3 | Start-up waits polled with timeouts, PLL read-back, retry with a hardware reset, fault line or LED blink | Fixed delays, and no way to tell a dead controller | Live display 0.5 s sooner; a held reset recovers |
| 4 | Capture keeps whole frames only (since the GPIB tests each is decoded at once; at first a frame had to be seen twice) | The rare `?` came from decoding while DMA wrote the buffer | No `?` in any run since; the board follows the meter's 111 frames a second |
| 5 | LT7680 bus on SPI1 registers at 18 MHz, address skipping, register cache | HAL was half of every redraw | Redraw 15.0 to 6.6 ms; burst writes proven |
| 6 | Draw only the cells and annunciators that changed, as soon as a frame is stable; rectangle `ClearScreen()` | Everything was redrawn every 35 ms, then a 6 ms wait | Change-to-screen 32.6 to 10.7 ms on average; read-back matches a full redraw pixel for pixel |
| 7 | Files split by job (see the repository map in [BUILD-AND-FLASH.md](BUILD-AND-FLASH.md)); dead code removed; `DelayMicroseconds()` on the cycle counter; TIM2 flag clear fixed | Readability, and two latent bugs | Same results on the bench as Stage 6 |
| after 7 | Boot: LT7680 set-up inside the panel's 120 ms wait, faster panel SPI. Capture: every whole frame decoded at once | Fastest start-up; the two-frame rule froze the display at 0.1 PLC and faster | First frame 0.14 s after reset; latency about 1 ms at up to 111 updates a second |
| after 7 | Timing-adjust page shows the hardware: MCU CPUID, device ID, flash size, cycle counter; panel ID; LT7680 set-up result | Clone Blue Pills and other panels or controllers; the DWT cycle counter is new since Stage 7 | Bench board: genuine STM32F103 (rev X), counter OK, panel answers (power byte `9C`) with its ID bytes unprogrammed (`FFFFFF`), LT7680 OK. See [BLUE-PILL-CHIPS.md](BLUE-PILL-CHIPS.md) |

## Test editions (ready-to-flash builds)

| Folder | Built from | Optimisation | What it adds | Notes |
| --- | --- | --- | --- | --- |
| Ian's original | `VisualGDB/Debug/R6581_VS_Display.hex` | `-O0` | the baseline | the revert target |
| [`Prebuilt/hydron-test-2026-10-16/`](../Prebuilt/hydron-test-2026-10-16/) | `3902c78` | `-O0` | changes 1 to 5 (sigma, loaded-once glyphs, central routine, red ERROR, larger annunciators); splash numbers moved off the annunciator row | first build for Hydron; its README lists the checks asked of him |
| [`Prebuilt/hydron-test-2026-10-17/`](../Prebuilt/hydron-test-2026-10-17/) | `b358880` | `-O0` | change 6 (layout table, Hydron sizes, 8 px gaps) and change 7 in its first form (boot diagnostics for the first 10 boots) | |
| [`Prebuilt/test-2026-10-17-O2-bootscreen/`](../Prebuilt/test-2026-10-17-O2-bootscreen/) | `5339ab5` | `-O2` | the centred boot screen with Ian's character (7), 1 V mode off (8), `-O2` (9) | the current test build |

Each folder has its own README with the flash and restore commands, checksums, and what to check. Take a backup of the 64 KB flash first and never do a full chip erase (the last 1 KB holds the TFT timing settings and the boot counter).

## Experiments and where their results are

| Experiment | Outcome | Where |
| --- | --- | --- |
| Timing probe (`-DTIMING_PROBE=1`) at `-O0`, `-O2`, `-Os` | redraw 25 / 18.4 / 19.5 ms; `-O2` adopted for the latest build | [TIMING-EXPERIMENTS.md](TIMING-EXPERIMENTS.md), `tools/measure.py` |
| Ohm redraw before and after loading glyphs once | 45.3 ms to 26.1 ms | [TIMING-EXPERIMENTS.md](TIMING-EXPERIMENTS.md) |
| LT7680 page flip (VSYNC, `REG[20h-23h]`) | works, no visible benefit; not merged | [TIMING-EXPERIMENTS.md](TIMING-EXPERIMENTS.md), `tools/events240.py` |
| 240 fps slow-motion analysis of display updates | in-place redraw already updates within about one frame; no visible tearing | [SLOW-MO-ANALYSIS.md](SLOW-MO-ANALYSIS.md), `tools/events240.py` |
| Layout options and the taller main line | shipped layout in [LAYOUT.md](LAYOUT.md); taller line rejected (tick limit, lens interference) | [LAYOUT.md](LAYOUT.md), `images/layout_*.png`, `tools/layout_diagram.py` |
| Boot screen design | centred, inside X 128 to 352 | [BOOT-AND-SPLASH.md](BOOT-AND-SPLASH.md), `images/boot_screen.png`, `tools/boot_screen_mockup.py`, `tools/make_logo.py` |
| Ian's 1 V mode | works, off by default | [ONE-VOLT-MODE.md](ONE-VOLT-MODE.md) |
| Menu walk for decode gaps | no persistent unmatched glyph; sigma found | [BENCH-TOOLS.md](BENCH-TOOLS.md), `tools/menu_logger.py`, `tools/swd.py` |
| Sigma bitmap | generated, drawn as a user-defined glyph | `tools/make_sigma.py` |
| Building without VisualGDB | GCC 14 note, build options | `tools/build.sh`, [BENCH-TOOLS.md](BENCH-TOOLS.md) |
| LT7680 register dump, SDRAM retention soak, capture counters, screen read-back | the non-visual checks behind the rework | [REWORK-PLAN.md](REWORK-PLAN.md), `tools/regdump.py`, `tools/soak.py`, `tools/capture.py` |

The raw captures (photos, videos, logs) are not in this repository; the numbers and conclusions above are.

## Findings that were resolved

- **Missing sigma** on the statistics screen: found by walking the front-panel menus with a logger (CONFIGURE > MATH > STATISTICS > READ, then the arrow keys). Fixed by change 1.
- **Slow ohm screens:** redraw 45.3 ms against 24.8 ms for other screens, because of the per-frame glyph store and fixed waits. After changes 2 and 3 the ohm redraw is 26.1 ms and the whole timed block is back inside the 35 ms tick.
- **Splash overlapping the annunciator row** for the first seconds once the annunciators were enlarged. Fixed by change 7.
- **Lens interference** on one bench panel with a taller main line (48x128): that layout was rejected and reverted; the shipped layout is clear of it. See [LAYOUT.md](LAYOUT.md).
- **Gaps between rows** reduced from 16 to 8 px without touching the main line.

## Things that were checked and found fine

- **Decode against the TFT:** for the roughly 60 front-panel states photographed or filmed, the TFT shows exactly what the Blue Pill decodes. The font draws every printable ASCII glyph, the micro and degree signs and the triangle/arrow codes used in the menus.
- **Menu walk:** 95 distinct screens (SELECT MENU and each item, SET ELEMENTS, DATA RECALL, STORE, DATE, SCALE, ERROR, INT TEMP, dBm math across ranges, statistics). No unmatched pattern persisted for three or more samples.
- **Self-test screen:** the meter's `RUNNING SELF TEST` (blinking, all annunciators off) decodes and draws cleanly. The manual describes no separate display test; the TEST menu item is the self-test.
- **Boot counter:** counts to 10 and stops, no erase on a normal boot, settings page unchanged; the DCV-held reset returns the count to 1.
- **Ian's 1 V mode:** converts correctly on the 1000 mV range, with no measurable redraw cost (checked by forcing the flag over SWD; the key press itself was not exercised).
- **Page flip (tried, not merged):** works, but a 240 fps comparison showed no visible benefit. See [TIMING-EXPERIMENTS.md](TIMING-EXPERIMENTS.md).

## Still open

- **Ohm/left-column corruption after long DISPLAY OFF** (reported on Hydron's own build, which loads the glyph once): the likely cause is found and fixed in rework Stage 1. SDRAM auto-refresh was off, and the retention soak showed display memory losing bits within minutes, with none lost since. Still to confirm on a visible panel with a 60-minute DISPLAY OFF (deferred check 4 in [REWORK-PLAN.md](REWORK-PLAN.md)).
- ~~**A rare `?`**~~ (about 1 in 1,000 decoded frames) from a torn read of the capture buffer: fixed in rework Stage 4; none since in any 5-minute capture run.
- **Diag-mode check patterns** (`\x01-\x08`, `\x0B`, `\x0C`, `\x16`) draw as smileys and suits instead of blocks, on the meter's diagnostic screens only.
- **Never seen lit on the TFT:** INFO and SLOT (blank by design), SRQ (decoded from live data but not yet seen lit).
- **Not visited:** TRIGGER and CONFIGURE sub-screens, MATH parameter screens, real error text and memory-card pages. (The meter's power-on screen, `Ver.` and `GPIB Addr`, is now drawn: the
  reworked boot is ready 0.14 s after reset and the meter sends that screen at 0.34 s.)
- **`-O2` image:** run through the boot counter and the DCV reset; not yet through a full menu walk or a long display-off test.
- **Rework, not yet checked by eye or by hand:** the 60-minute DISPLAY OFF test and the glyphs (with Hydron), a menu walk
  and the glyph self-test pages. Flicker, colours and timing-adjust mode are checked. The list is under "Deferred
  visual checks" in [REWORK-PLAN.md](REWORK-PLAN.md).
