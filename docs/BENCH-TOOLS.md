# Bench tools

Everything in [`tools/`](tools/) was used to produce the numbers in these notes. They are small, Windows-oriented and not
polished; take what is useful. Python 3 with `numpy` and `Pillow` is needed for the video and layout scripts.

| Script | What it does |
| --- | --- |
| `build.sh` | Builds the firmware with the Arm GNU Toolchain (tested with 14.2.Rel1) without VisualGDB: `OPT=2 EXTRA_CFLAGS="-DTIMING_PROBE=1" bash docs/tools/build.sh name` gives `docs/tools/images/name/firmware.{elf,hex,bin,map}` and `BUILD_FLAGS.txt`. `ARM_GNU_BIN` points at the toolchain `bin`. GCC 14 makes an implicit declaration an error and Ian's code has one (`DisplayCloneDeterminationAux`), so the script downgrades that diagnostic to a warning rather than editing his source. |
| `swd.py` | Non-halting reader of the decoded display buffers and variables through openocd's telnet port 4444 (`read_memory`). `use_elf()` takes the addresses from the ELF of the image actually running. |
| `menu_logger.py` | Logs the Blue Pill's decoded main line, aux line and annunciators while you walk the front-panel menus; keeps every distinct screen, every character seen, and any 5x7 pattern that stayed unmatched (`?`) for 3 or more samples. It sends nothing to the meter. `SWD_ELF=<elf> python -I menu_logger.py <out-dir>`. |
| `measure.py` | Flashes an image and reads the timing probe variables (see [TIMING-EXPERIMENTS.md](TIMING-EXPERIMENTS.md)). Set `OPENOCD_DIR` to an xPack openocd install. |
| `events240.py` | Per-digit update timing from a 120/240 fps slow-motion clip: finds each reading change and the frame in which each digit column starts to move. Atomic (page-flip) updates start all columns in the same frame. |
| `layout_diagram.py` | Draws the to-scale layout pictures in `images/`. |
| `make_sigma.py` | Generates the 16x32 and 12x24 sigma bitmaps used for the custom glyph. |

## Method that worked

1. Flash an image over the SWD header (SWDIO, SWCLK, GND; the meter powers the board) and leave openocd running as a server.
2. Drive the meter over GPIB with commands from its SCPI reference, read what the Blue Pill decoded, and compare it with what the TFT shows in a photo.
3. Walk the front-panel menus with `menu_logger.py` running to find decode-table gaps (a `?` that persists). That is how the
   missing sigma on the statistics screen was found (CONFIGURE > MATH > STATISTICS > READ, then the arrow keys).
4. Take a full backup of the 64 KB flash before the first flash, and restore it by programming the `.bin` at `0x08000000` with
   verify. The last 1 KB holds the TFT settings and the boot counter.
5. A 30 fps phone video is not enough to judge redraw artefacts; 240 fps is, and `events240.py` quantifies it.

The GPIB side (a 42-step sequence that drove the meter through its functions, with beeps as photo cues) depended on a
private instrument library and is not included; any GPIB library with the commands from the R6581T SCPI reference will do.
