# Building and flashing the display controller

Everything a person or an LLM agent needs to set up a build environment, build this firmware, flash the Blue Pill, check the result and
put things back. It is written so that each step can be followed literally. Commands are for Windows with Git Bash and PowerShell, which is
what this was developed on; the notes say what changes on Linux and macOS.

> **Two rules that matter more than the rest.** (1) Never do a **full chip erase** on the Blue Pill: the last 1 KB of flash holds the TFT
> timing settings and the boot counter. (2) **Take a backup of the 64 KB flash before the first flash** (section 6), so you can always get back.

## 1. What the "display controller" is

The R6581T's vacuum-fluorescent display (VFD) is replaced by a 320 x 960 colour TFT. Between them sit two boards:

```
 R6581T meter ──(VFD signals)──> STM32F103C8 "Blue Pill" ──(SPI1)──> LT7680A-R graphics board ──> TFT panel (ST7701A controller)
                                  this firmware                         (text/graphics engine, SDRAM)
```

- The **Blue Pill** (STM32F103C8T6, Cortex-M3, 72 MHz from an 8 MHz crystal x 9) captures the meter's VFD serial stream with SPI2 and DMA,
  decodes it into characters (47 cells of 5x7 bitmaps, 18 on the main line and 29 on the aux line, plus 18 annunciators), and draws them through the LT7680.
- The **LT7680A-R** board draws text and graphics into its own SDRAM and drives the panel. The Blue Pill also bit-bangs the panel controller's own SPI to set it up.
- This firmware is **only the Blue Pill's program**. The pin map is below; the electrical build of the conversion (cables, the two boards, the panel) is
  Ian Johnston's design, documented in his gerbers and instructions (the `Hardware/` folder and his site; see `Info/` and `_ReadMe.txt`).

### Pins the firmware uses (from `Core/Inc/main.h`, `lt7680.h`, `gpio.c`)

| Function | Pins |
| --- | --- |
| VFD capture (SPI2 + DMA) | PB13 serial clock, PB15 serial data, PB11 restart (external interrupt) |
| LT7680 link (SPI1) | PA4 CS, PA5 SCK, PA6 MISO, PA7 MOSI; PB10 LT7680 reset |
| TFT controller setup (bit-banged SPI) | PB3 CS, PB4 SCK, PB5 SDI |
| Front-panel DCV key sense | PA11 (normally high) and PA12 (normally low); both low = key pressed; PA12 high at power-up = timing mode |
| Test LED | PC13 |
| Programming | SWDIO, SWCLK, GND on the Blue Pill's 4-pin SWD header (PA13, PA14) |

## 2. What you need

| Item | Version used | Notes |
| --- | --- | --- |
| Blue Pill (STM32F103C8T6) | genuine or clone | The boot screen's "BluePill speed" number tells a genuine chip from a slower clone; see [BOOT-AND-SPLASH.md](BOOT-AND-SPLASH.md). Jumpers BOOT0 and BOOT1 both at 0. |
| ST-Link V2 (or clone) | | SWD only: SWDIO, SWCLK, GND. **Leave 3V3 unconnected when the board is powered by the meter**; use the programmer's 3V3 only on a bare board on the bench. An ST-Link V3 needs a 3.3 V supply to the board (Ian's `_Programming.txt`). |
| Arm GNU Toolchain (`arm-none-eabi`) | **14.2.Rel1** (GCC 14.2.1) | Download the `mingw-w64-i686-arm-none-eabi` zip from developer.arm.com (Arm GNU Toolchain Downloads), verify its SHA-256 against the page, unzip anywhere (no installer, no admin needed). The default location the build script looks in is `%LOCALAPPDATA%\arm-gnu-toolchain`. |
| openocd | **xPack openocd 0.12.0-7** | `winget install xpack-dev-tools.openocd-xpack` (or download from xpack.dev). Needs the ST-Link driver. STM32CubeProgrammer works as an alternative for flashing only. |
| Git Bash (or any bash) | Git 2.55 | `docs/tools/build.sh` is a bash script. |
| Python 3 | 3.14 (anything recent) | Only for the analysis tools in `docs/tools/`: `Pillow` and `numpy`; `ffmpeg`/`ffprobe` for the slow-motion script. The firmware build does not need Python. |

Ian built it with Visual Studio 2022 and the VisualGDB plugin (`R6581_VS_Display.sln`, output in `VisualGDB/Debug/`). That still works if you have
VisualGDB; this document uses the plain toolchain build, which needs neither.

## 3. Get the source

```bash
git clone https://github.com/Cyclotronic/R6581_VS_Display.git
cd R6581_VS_Display
```

- This is a fork of `Ian-Johnston/R6581_VS_Display` (the upstream is Ian's original). The changes are listed in
  [CHANGES-AND-FINDINGS.md](CHANGES-AND-FINDINGS.md).
- **Line endings:** `.gitattributes` has `* text=auto`, and `.sh` files must stay LF or bash fails with `\r` errors. This repository forces LF for `.sh`;
  if you see `$'\r': command not found`, run `dos2unix docs/tools/build.sh` or re-clone with `git config core.autocrlf false`.

## 4. Build

```bash
# default: -O0, the same optimisation level as Ian's Debug build
bash docs/tools/build.sh my-build

# optimised, as in the newest Prebuilt images
OPT=2 bash docs/tools/build.sh my-build-O2

# with options (see the table in section 7)
OPT=2 EXTRA_CFLAGS="-DBOOT_DIAG_SHOWS=255 -DONE_VOLT_MODE=1" bash docs/tools/build.sh my-build

# toolchain somewhere else
ARM_GNU_BIN="/c/tools/arm-gnu-toolchain-14.2.rel1/bin" bash docs/tools/build.sh my-build
```

Output goes to `docs/tools/images/<name>/` (override the folder with `OUT_DIR`): `firmware.elf`, `firmware.hex`, `firmware.bin`, `firmware.map`,
and `BUILD_FLAGS.txt` recording `OPT` and `EXTRA_CFLAGS`. That folder is build output; do not commit it.

**What the script does** (so you can reproduce it in another build system): for every file in `Core/Src/*.c` plus the HAL sources
`stm32f1xx_hal{,_cortex,_dma,_exti,_flash,_flash_ex,_gpio,_gpio_ex,_pwr,_rcc,_rcc_ex,_spi,_tim,_tim_ex}.c` it runs
`arm-none-eabi-gcc -mthumb -mcpu=cortex-m3 -ICore/Inc -IDrivers/CMSIS/Device/ST/STM32F1xx/Include -IDrivers/CMSIS/Include -IDrivers/STM32F1xx_HAL_Driver/Inc -ggdb -O<OPT> -DARM_MATH_CM3 -Dflash_layout -DSTM32F103C8 -DUSE_HAL_DRIVER -DSTM32F103xB -DDEBUG=1 -ffunction-sections -fdata-sections -Wall ...`,
assembles `Core/Startup/startup_stm32f103c8tx.S`, then links with `-TFiles/STM32F103C8TX_FLASH.ld --specs=nano.specs --specs=nosys.specs -Wl,-gc-sections`
and converts with `objcopy` to `.hex` and `.bin`. The compile flags were taken from the project's own VisualGDB response files.

**Known build trap:** GCC 14 treats an implicit function declaration as an error, and Ian's `main.c` has one (`DisplayCloneDeterminationAux`, plus
`RunBluePillSpeedTestOffline`). The script therefore passes `-Wno-error=implicit-function-declaration` (and the matching `incompatible-pointer-types` and
`int-conversion`) so that Ian's source is built unmodified. Those warnings in the output are expected. With an older compiler they do not appear.

**Expected result:** no `error:` lines, then a size line. Reference sizes for the current `master`:

| Build | text | data | bss |
| --- | ---: | ---: | ---: |
| `-O0` | about 38,900 | 160 | 3,560 |
| `-O2` | about 30,300 | 160 | 3,500 |

**Flash budget:** the linker script gives 64 KB at `0x08000000`, but the **last 1 KB page (`0x0800FC00`) belongs to the settings**, so code and data must stay
below 63 KB (64,512 bytes). There is about 25 KB of headroom at `-O0`. If a build grows past it, the link may still succeed and then overwrite the settings page on flashing. Check the size line.

**Reproducibility:** on the author's machine a rebuild of a published commit with the same toolchain was byte-identical to the image that was flashed and published
(`firmware.bin`). Compare yours with the checksums in `Prebuilt/<folder>/SHA256SUMS.txt`: `sha256sum -b docs/tools/images/<name>/firmware.bin`. The `.elf` and
`.map` contain build paths and will differ. This has not been tried on a second machine.

## 5. Connect the programmer

1. Power the meter off, or work on a bare board with the programmer supplying 3.3 V (never both).
2. Connect SWDIO, SWCLK and GND from the ST-Link to the Blue Pill's SWD header. Leave the programmer's 3V3 pin disconnected when the board is in the meter.
3. Power the meter on (the Blue Pill is powered by it). Check the link:

```powershell
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg -c "init" -c "halt" -c "mdw 0x08000000 4" -c "resume" -c "shutdown"
```

If `openocd` is not on the path, call it by full path and add `-s <openocd-dir>/openocd/scripts`. Expect a line about the target
(`stm32f1x.cpu`, Cortex-M3) and four words of flash. Clone boards (CKS32 and similar) can report a different IDCODE, which openocd may refuse; that is a known issue
with those chips and the fix is to override the TAP ID in the openocd command; it has not come up on the board used here.

**Always end an openocd command with `-c "shutdown"`** (or `-c "exit"`). Without it openocd stays running, holds the ST-Link and the next command hangs until it times out.

## 6. Back up, flash, verify, restore

**Back up first** (64 KB, includes the settings page):

```powershell
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg -c "init" -c "halt" -c "dump_image my_backup.bin 0x08000000 0x10000" -c "shutdown"
```

Keep `my_backup.bin` safe. If you want to be sure it is complete, dump a second time and compare the two files.

**Flash and verify, then run** (use the `.hex`; the `.bin` is for `0x08000000`):

```powershell
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg -c "init" -c "halt" -c "program docs/tools/images/my-build/firmware.hex verify" -c "reset run" -c "shutdown"
```

Success is `Verified OK` in the output. `program` erases only the pages the image occupies, so the settings page (above the image) is kept; that is why timing settings
and the boot counter survive a reflash. STM32CubeProgrammer also works: open the `.hex`, connect over SWD, tick *Verify*, **leave *Erase full chip* unticked**.

**Restore the original, or a backup:**

```powershell
# Ian's original firmware (the repo's Debug hex): this keeps the settings page as it is
openocd ... -c "program VisualGDB/Debug/R6581_VS_Display.hex verify" -c "reset run" -c "shutdown"

# your own full backup (rewrites all 64 KB including the settings page)
openocd ... -c "program my_backup.bin 0x08000000 verify" -c "reset run" -c "shutdown"
```

### The settings page (`0x0800FC00`, 1 KB)

| Offset | Contents |
| --- | --- |
| words 0 to 7 (bytes 0 to 31) | TFT timing: `LCD_VBPD`, `LCD_VFPD`, `LCD_VSPW`, `LCD_HBPD`, `LCD_HFPD`, `LCD_HSPW`, refresh rate, 4-character panel type (`AdaF` or `BuyD`). Written with defaults on first boot if invalid. |
| words 16 to 31 (bytes 64 to 127) | Boot counter markers: each of the first boots programs the next word from `0xFFFFFFFF` to `0x00000000`; the count is the number of zero words |

Read it without halting: `-c "init" -c "mdw 0x0800FC00 8" -c "mdw 0x0800FC40 16" -c "shutdown"`. How the counter and the DCV-at-power-up reset work is in [BOOT-AND-SPLASH.md](BOOT-AND-SPLASH.md).

## 7. Build options (compile-time, via `EXTRA_CFLAGS="-D..."`)

| Option | Default | Effect |
| --- | --- | --- |
| `BOOT_DIAG_SHOWS=n` | 10 | Number of boots that show the speed test and boot screen (0 = never, 255 = every boot) |
| `BOOT_SCREEN_MS=n` | 4000 | How long the boot screen stays up |
| `SPLASH_IN_LOOP=1` | 0 | Ian's splash drawn in the live loop again (untested with the current layout) |
| `ONE_VOLT_MODE=1` | 0 | Ian's 1 V display mode (DCV press on the 1000 mV range shows volts); see [ONE-VOLT-MODE.md](ONE-VOLT-MODE.md) |
| `GLYPH_SELFTEST=1` | 0 | Runs a glyph and layout test pattern once at boot (12 pages) before normal operation; for checking the display, not for normal use |
| `TIMING_PROBE=1` | 0 | Keeps cycle-count timing variables (`tp_*`) readable over SWD; see [TIMING-EXPERIMENTS.md](TIMING-EXPERIMENTS.md) |
| `OPT=0/2/s` (environment, not a -D) | 0 | Optimisation level |

## 8. Check that it works

1. **Boot screen:** after the flash the board restarts. For the first 10 boots (counting from the stored count) you should see, a few seconds after power-up, the centred
   boot screen (speed number, the two notes, timing numbers with `boot n/10`, credits with Ian's character); then the live display. The picture is in
   [BOOT-AND-SPLASH.md](BOOT-AND-SPLASH.md).
2. **Live display:** the meter's main and aux lines and annunciators appear and follow the meter. An ohm range and the statistics screen (sigma) are good tests of the glyph path.
3. **Settings intact:** dump `0x0800FC00` (section 6) and check the first eight words are still your timing values.
4. **Optional, with the SWD tools in `docs/tools/`:** `swd.py` reads the decoded display buffers without halting the CPU (start openocd as a server with
   `openocd -f interface/stlink.cfg -f target/stm32f1x.cfg -c init` and it listens on telnet port 4444); `menu_logger.py` walks the menus and lists characters the decoder could not match;
   `measure.py` reads the timing probe. See [BENCH-TOOLS.md](BENCH-TOOLS.md).

## 9. Making changes

Where things are:

| Task | Where |
| --- | --- |
| Add or fix a decoded character | `bitmap_characters[]` in `Core/Src/main.c`: a 5x7 pattern (7 bytes, one per row) and the character it maps to. An unmatched pattern shows as `?` |
| Add a custom (user-defined) glyph such as ohm or sigma | Glyph data and `LoadCustomGlyphs_LT()` in `Core/Src/lt7680.c`; the placeholder-to-glyph table (`GlyphCodeFor`, `HasGlyph`) and `DrawLineWithGlyphs()` in `Core/Src/display.c`; the decode entry in `main.c`. `docs/tools/make_sigma.py` shows how a glyph is generated |
| Change a row's size or position | `Xpos_*`, `Ypos_*` macros in `Core/Inc/display.h` and the `LineFmt` table in `Core/Src/display.c`; layout in [LAYOUT.md](LAYOUT.md) |
| Change the boot screen | `DisplayBootScreen()` and the `BOOT_*_ROW_X` constants in `Core/Src/display.c`; the logo bitmap is `Core/Inc/boot_logo.h`, rebuilt by `docs/tools/make_logo.py` |
| Change TFT timing presets | the `*_SETTINGS` arrays in `Core/Src/main.c`; Ian's notes on each preset are in `Info/TFT Timing.txt` |

Rules learned the hard way (full reasons in the other notes):

- The display geometry is rotated: **X is the vertical screen position, Y runs along the line**, and the top 80 X-pixels are hidden. Keep live content inside X 128 to 352 (the rows now drawn) unless you have checked the visible area on your panel.
- The main loop has a **35 ms tick**. At `-O0` a full redraw takes about 25 to 30 ms, so adding pixels or fixed delays to the live path can make updates late. Measure with `TIMING_PROBE=1` before and after. See [TIMING-EXPERIMENTS.md](TIMING-EXPERIMENTS.md).
- Do not put fixed delays around glyph loading; wait on `WaitForLT7680Ready()` instead.
- Anything that **erases the settings page** also resets the boot counter and rewrites the timing words; write to it only through the `EEPROM_*` helpers in `main.c`.
- Glyph memory lives in SDRAM at `0x200000` and is lost when the display is turned off; it is reloaded when the display wakes. A new glyph set must go through the same path.
- After a change: build at `-O0` and `-O2`, check the size, flash, verify, run the checks in section 8, and walk an ohm range, the statistics screen and DISPLAY OFF/ON.

## 10. Publishing a build

The convention used in `Prebuilt/`:

1. Commit the source change first. Name the build folder `Prebuilt/<name>/` and the files `R6581_VS_Display_<name>_<commit>.hex` and `.bin`, built from that exact commit.
2. Write `SHA256SUMS.txt` (`sha256sum -b *.hex *.bin > SHA256SUMS.txt`) and a README saying what changed, how to flash (this document's section 6 commands), and what to check.
3. Rebuild from the committed tree and confirm the `.bin` is identical to the one you flashed and tested.
4. Commit the folder and push. Pushing is the step that makes it public; do it deliberately.
5. Do not publish vendor manuals, private captures or anything that identifies a bench or a person; keep third-party code and datasheets out of the repository.

## 11. Troubleshooting

| Symptom | Likely cause and fix |
| --- | --- |
| `$'\r': command not found` running `build.sh` | CRLF line endings; `dos2unix docs/tools/build.sh` |
| `arm-none-eabi-gcc.exe: not found` | Set `ARM_GNU_BIN` to the toolchain's `bin` folder. On Linux or macOS the script also hard-codes `.exe`; edit those four names |
| `error: implicit declaration` | You removed the `-Wno-error=implicit-function-declaration` flag, or are building with another script; GCC 14 needs it for Ian's source |
| openocd hangs or reports the ST-Link busy | A previous openocd is still running; end the process (`taskkill /F /IM openocd.exe`) and always finish commands with `shutdown` |
| `init` fails, no target | Check SWDIO/SWCLK/GND, the board has power, and BOOT0 is at 0; try `-c "adapter speed 480"` for a long cable |
| Verify fails | Do not run the board; reflash. If it repeats, check that the image is under 63 KB and try a lower adapter speed |
| Screen blank after flashing | Power-cycle the meter; check you flashed the `.hex` for this board and that the settings page was not erased (timing values corrupt cause a blank or unstable TFT). Hold DCV at power-up to step through the timing presets, or restore your backup |
| Boot screen never appears | The boot count is already 10 or more; hold DCV at power-up, or erase the markers by following the reset in [BOOT-AND-SPLASH.md](BOOT-AND-SPLASH.md) |
| Odd `?` characters on screen | A pattern missing from `bitmap_characters`; use `menu_logger.py` to find it. A single `?` for one frame is a torn read and harmless |

## 12. Repository map

| Path | What |
| --- | --- |
| `Core/Src`, `Core/Inc` | The firmware: `main.c` (decoder, main loop, settings, boot counter), `display.c` (drawing, layout, boot screen), `lt7680.c` (graphics board driver, glyphs, logo), `lcd.c` (TFT controller setup), SPI/DMA/GPIO/timer setup |
| `Core/Startup`, `Files/` and `STM32F103C8TX_FLASH.ld` | Startup code and the linker script (64 KB flash at `0x08000000`, 20 KB RAM) |
| `Drivers/` | CMSIS and the STM32F1 HAL |
| `VisualGDB/Debug/` | Ian's original build output, including the hex used as the revert target |
| `Hardware/` | Ian's gerbers for the LT7680 controller board (also the source of the boot-screen character) |
| `Info/` | Ian's notes: TFT timing presets, VFD packet decode spreadsheets |
| `Prebuilt/` | Ready-to-flash builds with notes and checksums |
| `docs/` | These notes and the tools in `docs/tools/` |
