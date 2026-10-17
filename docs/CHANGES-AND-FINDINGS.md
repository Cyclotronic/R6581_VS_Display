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
| Layout options and the taller main line | shipped layout in [LAYOUT.md](LAYOUT.md); taller line rejected (tick limit, lens interference) | [LAYOUT.md](LAYOUT.md), `images/layout_*.png`, `tools/layout_diagram.py` |
| Boot screen design | centred, inside X 128 to 352 | [BOOT-AND-SPLASH.md](BOOT-AND-SPLASH.md), `images/boot_screen.png`, `tools/boot_screen_mockup.py`, `tools/make_logo.py` |
| Ian's 1 V mode | works, off by default | [ONE-VOLT-MODE.md](ONE-VOLT-MODE.md) |
| Menu walk for decode gaps | no persistent unmatched glyph; sigma found | [BENCH-TOOLS.md](BENCH-TOOLS.md), `tools/menu_logger.py`, `tools/swd.py` |
| Sigma bitmap | generated, drawn as a user-defined glyph | `tools/make_sigma.py` |
| Building without VisualGDB | GCC 14 note, build options | `tools/build.sh`, [BENCH-TOOLS.md](BENCH-TOOLS.md) |

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

- **Ohm/left-column corruption after long DISPLAY OFF** (reported on Hydron's own build, which loads the glyph once): not reproduced here in three short off periods, and not expected on Ian's per-frame build. The reload after DISPLAY OFF in this fork has not had a long (30 to 60 minute) test, including at `-O2`.
- **A rare `?`** (about 1 in 1,000 decoded frames at `-O0`, none in 1,219 at `-O2`) from a torn read of the capture buffer; it lasts one sample.
- **Diag-mode check patterns** (`\x01-\x08`, `\x0B`, `\x0C`, `\x16`) draw as smileys and suits instead of blocks, on the meter's diagnostic screens only.
- **Never seen lit on the TFT:** INFO and SLOT (blank by design), SRQ (decoded from live data but not yet seen lit).
- **Not visited:** TRIGGER and CONFIGURE sub-screens, MATH parameter screens, real error text, memory-card pages, and the meter's power-on screen (`Ver.` and `GPIB Addr`).
- **`-O2` image:** run through the boot counter and the DCV reset; not yet through a full menu walk or a long display-off test.
