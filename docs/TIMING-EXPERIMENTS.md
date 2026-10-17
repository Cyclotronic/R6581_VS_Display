# Timing experiments

The main loop runs a 35 ms tick (`SetTimerDuration(35)`). A screen update that takes longer than that makes the loop
late; one that is close to it leaves the visible page being rewritten most of the time. All numbers are CPU cycle counts
(`DWT->CYCCNT`) at 72 MHz, read over SWD while the board runs normally, so they include everything the firmware really does.

## How it was measured

Build with `-DTIMING_PROBE=1`. The firmware then keeps these globals, which a debugger reads without halting the CPU:
`tp_draw_last/min/max` (the text redraw), `tp_total_last/max` (the whole timed block including the settle delay and button
read), `tp_period_last/min/max` (entry-to-entry period of the block), `tp_blocks`, `tp_main_loops`. Divide by 72000 for
milliseconds. [`tools/measure.py`](tools/measure.py) flashes an image, lets it run and reads them through openocd; see
[BENCH-TOOLS.md](BENCH-TOOLS.md).

## Optimisation and delays: what is and is not different from Ian's build

- **The builds here are not optimised.** Every image in `Prebuilt/` is built at `-O0`, the same as Ian's Debug build. The
  speed-ups below come from removing work and waits, not from the compiler. `-O2` was measured (about 26 % faster redraw,
  identical decode) but is **not** used; it is the obvious next step if more margin is wanted.
- **Fixed delays removed in the glyph path.** Ian's code stored the ohm symbol again for every frame and waited with fixed
  `Delay_NonBlocking(5)` and `(2)` calls around it (about 17 ms per ohm screen). The glyphs are now loaded once (and after
  DISPLAY OFF), and the draw routine (`DrawLineWithGlyphs`) waits only for the LT7680 to report ready
  (`WaitForLT7680Ready()`), with no fixed delays. Other delays in Ian's code, such as the settle delay in the timed block, are unchanged.
- **A delay was added, once.** The boot screen holds for 4 s (`BOOT_SCREEN_MS`) and the speed test runs, for the first 10 boots
  only (see [BOOT-AND-SPLASH.md](BOOT-AND-SPLASH.md)). Nothing is added to the live loop.

## Results (all `-O0`, the optimisation level of Ian's Debug build)

| Build | DCV redraw | 2W ohms redraw | Whole timed block | Notes |
| --- | --- | --- | --- | --- |
| Ian's original | 24.8 ms | **45.3 ms** | 50.9 ms on ohm screens | Ohm symbol re-stored every frame, with fixed `Delay_NonBlocking(5)`/`(2)` waits |
| + custom glyphs (loaded once) | 24.8 ms | **26.1 ms** | 31.6 ms | Ohm and sigma live in their own SDRAM region; no per-frame reload |
| + Hydron-size layout (aux 32x64) | 27.0 ms | 28.1 ms | 32.4 to 33.8 ms | The layout in this fork |
| taller main line (48x128) | 28.9 ms | 30.0 ms | 34.3 to 35.7 ms, period up to 43 ms | **Rejected**: at the edge of the tick, and it caused lens interference on one panel |

What the numbers say:

- **The extra 20 ms on ohm screens was mostly fixed waits.** About 17 ms was `Delay_NonBlocking` calls around the glyph
  reload and about 3 ms the reloads themselves. Loading the glyphs once and selecting them with the CGRAM start register
  removed both, with no change to what is drawn.
- **At `-O0` the timed block is close to the tick.** The original block was 30 ms typical and 37 ms at its worst, above
  35 ms, with the visible page being rewritten about 68 % of the time. Every extra pixel drawn pushes it towards the limit,
  which is why a taller main line (more pixels per frame) is not free.
- **Optimisation helps a lot and decodes identically.** Redraw of all text: 25 ms at `-O0`, 18.4 ms at `-O2`,
  19.5 ms at `-Os`. The decoded display was identical at `-O2` in a 42-step run. `-O2` has **not** been adopted: the boot
  speed number and Ian's tested behaviour are `-O0`. If you want margin, `-O2` is the first thing to try.
- **A rare `?`** (about 1 in 1,000 decoded frames at `-O0`, 0 in 1,219 at `-O2`) is a torn read of the DMA capture buffer:
  the decoder reads a packet while the next one is arriving. It lasts one sample and never persists.

## Page flip (tried, not merged)

The LT7680 can switch the displayed image by writing its main-image start address (`REG[20h-23h]`) and exposes a VSYNC
flag (`REG[0Ch]` bit 4). The idea: draw the next frame in a second buffer and flip, so a half-drawn frame is never shown.

- The flip works: VSYNC was seen on every flip (0 timeouts) and a single-byte write of `REG[22h]` flips atomically.
- Update period was about 32 to 50 ms at `-O2` and about 49 ms at `-O0` with the flip.
- A 240 fps slow-motion comparison of the flip build against the original (per-digit onset timing, 59 and 43 reading
  changes): the median spread of digit onset was 1 frame (4.2 ms) in both; 71 % of changes were within one frame with the
  flip against 53 % without. What looked like tearing in photos was largely the LCD's own pixel response: an old digit
  fades over about 8 frames (about 33 ms).
- Conclusion: no visible benefit, a second buffer to manage and extra latency. Not used.

The code for this is not in this repository; the measurement method is in [BENCH-TOOLS.md](BENCH-TOOLS.md) (`events240.py`).

## TFT panel timing presets

Not changed. Ian's table of six porch/pulse presets and how each behaves on his panel is in
[`../Info/TFT Timing.txt`](../Info/TFT%20Timing.txt); the DCV-at-power-up mode that steps through them is unchanged. See
[BOOT-AND-SPLASH.md](BOOT-AND-SPLASH.md) for its one side effect: it also restarts the boot count.
