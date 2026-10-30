# Blue Pill chips and hardware identity

Many "Blue Pill" boards don't carry a genuine ST STM32F103C8. Common substitutes are the CKS32F103 / CS32F103,
GD32F103, APM32F103 and CH32F103. All use an Arm Cortex-M3 core and mostly run STM32 code unchanged, but they differ in
the details. This note lists what this firmware depends on, so a builder with a clone knows what to check, and what
the timing-adjust page shows about the chips on a board.

**Everything in these docs was measured on one board** with a genuine chip:

| Read over SWD | Value | Meaning |
|---|---|---|
| CPUID (`0xE000ED00`) | `0x411FC231` | Arm Cortex-M3 r1p1, as in ST's STM32F103 |
| DBGMCU_IDCODE (`0xE0042000`) | `0x20036410` | device `0x410` (medium density), revision `0x2003` (rev X) |
| Flash size (`0x1FFFF7E0`) | `0x0040` | 64 KB |
| DWT_CTRL (`0xE0001000`) | `0x40000001` | 4 comparators, cycle counter present (bit 25 `NOCYCCNT` = 0) and running |

The boot times, latencies and SPI speeds in [REWORK-PLAN.md](REWORK-PLAN.md) are for this chip. None of them has been
measured on a clone.

## What could go wrong on a clone

In order of how badly it would show.

1. **Cycle counter (DWT `CYCCNT`): the panel set-up would hang.** Since the rework (Stage 7), `DelayMicroseconds()` in
   `lcd.c` counts CPU cycles on the Cortex-M3's DWT cycle counter. It times the bit-banged SPI that sets up the ST7701S
   panel. The counter is an optional part of a Cortex-M3. Every ST STM32F103 has it, but no clone was checked here.
   If a chip lacks it, the counter never advances: the first delay never ends, and the board hangs at boot with a dark
   panel and the backlight off, because the panel set-up starts before the LT7680's. Ian's old SysTick delay
   didn't need the counter. (It had a bug of its own instead: a delay could end early.)
   - **To check a board:** read `DWT_CTRL` (`0xE0001000`) over SWD. Bit 25 (`NOCYCCNT`) must be 0. Then enable the
     counter and read `0xE0001004` twice: the value must change. The openocd lines are below.
   - **If a clone lacks it:** add a fallback to `DelayMicroseconds()` that checks once whether the counter advances and
     otherwise spins a calibrated loop. That isn't in the code, because no board here needs it.
2. **openocd refuses the chip.** CKS32 and similar clones report a different SWD ID (`0x2ba01477` instead of ST's
   `0x1ba01477`). openocd then stops with "UNEXPECTED idcode". Add `-c "set CPUTAPID 0x2ba01477"` before
   `-f target/stm32f1x.cfg`. This affects flashing and every SWD tool in `docs/tools`, not the firmware.
3. **The settings page.** The TFT timing set and boot counter live in the last 1 KB of a 64 KB part (`0x0800FC00`).
   This assumes 1 KB erase pages, as on ST's medium-density parts. A clone with a different page size would erase more
   or less than intended. Check the page size in that chip's reference manual. Many "C8" chips actually have 128 KB;
   that does no harm, because the page used is still the 64th.
4. **SPI to the LT7680 at 18 MHz** (Stage 5: 72 MHz APB2 / 4). This is the STM32F103's rated SPI maximum, and it ran
   error-free here. A clone's SPI or its pin drive could be marginal at 18 MHz. `-DLT_BUS_TEST=1` checks it: 100,000
   register read-backs and a burst write. `-DLT_SPI_PRESC=SPI_BAUDRATEPRESCALER_8` drops to 9 MHz.
5. **Clock set-up.** The firmware runs at 72 MHz from the 8 MHz crystal (HSE x 9), as Ian's did. GD32 parts can run
   faster but default to the same settings. Some clones need longer for the crystal to start; the HAL's start-up
   timeout covers the usual cases.
6. **Speed-dependent timing.** The capture copies a 235-byte frame inside the 9 ms VFD frame interrupt. At 72 MHz that
   takes a few microseconds, so any chip at the same clock has ample margin.

## Ian's "BluePill speed" test

Ian's start-up ran a short loop and showed how many passes it made, as a rough way to spot a slower clone. In this
fork it is part of the boot screen, which the rework turned off by default (`BOOT_DIAG_SHOWS=0`). So **it no longer
runs at all**, rather than running unseen. It never changed how the firmware behaved: the number was only displayed.
Build with `-DBOOT_DIAG_SHOWS=10` to see it on the first 10 boots. The number depends on the optimisation level; see
[BOOT-AND-SPLASH.md](BOOT-AND-SPLASH.md).

The register reads above tell more than the loop count: they identify the core, the device and the cycle counter
directly.

## Hardware identity on the timing-adjust page

Hold DCV at power-up to open the timing-adjust page. Its two bottom rows identify the hardware, so a board that behaves
differently can be described without an ST-Link:

```
MCU  CPUID 411FC231 ID 00000000 64KB CYCCNT OK
Panel  ST7701S FFFFFF  Pwr 9C  LT7680 OK, 1 try
```

The whole page stays inside the band the meter's window shows, in the largest built-in font; see
[LAYOUT.md](LAYOUT.md#timing-adjust-page).

| Field | Where it comes from | On the bench board |
|---|---|---|
| `CPUID` | the Cortex-M3's `SCB->CPUID` | `411FC231` (Arm Cortex-M3 r1p1, as ST's STM32F103) |
| `ID` | `DBGMCU_IDCODE`. ST's errata say firmware reads it as 0 on an STM32F103 unless a debugger is attached; a clone that shows a value here behaves differently from ST's part | `20036410` with the ST-Link attached; the page shows what it reads without one |
| `64KB` | the flash size, from the word at `0x1FFFF7E0` | `64KB` |
| `CYCCNT` | the DWT cycle counter: present, enabled and advancing (`OK`), or `NONE` | `OK` |
| `ST7701S` | the panel's RDID1-3 bytes (`DAh`, `DBh`, `DCh`), read once at boot over the panel's SPI | `FFFFFF`: the module maker left them unprogrammed |
| `Pwr` | the panel's power-mode register (`0Ah`) from the same read. `9C` = booster on, awake, normal mode, display on. It shows the panel answers and is set up | `9C` |
| `LT7680` | how the controller's set-up ended: `OK`, or the fault bits (`01` reset, `02` no reply, `04` PLL, `08` SDRAM), and the number of tries | `OK, 1 try` |

If the panel doesn't answer at all, the field reads `no reply`.

**The LT7680 has no ID register.** Every LT768x variant has the same 128 Mb of SDRAM, so memory size doesn't tell
them apart either. What the page can show is whether the controller answered and set itself up cleanly.

**The panel read** costs about 0.2 ms at boot. The data pin (PB5) is released, and each byte is read twice: once with
the STM32's pull-up and once with its pull-down. If the two differ, nothing drove the line. `lcd_id` and `lcd_pwr` hold
the same values for SWD (`lcd_id` = `0xFFFFFFFF` if the panel didn't answer).

**The panel's ID bytes don't identify it on this hardware.** The ST7701S datasheet leaves RDID1-3 for the module maker
to program ("RDID1 ... identifies the LCD module's manufacturer"; default `xxh`). BuyDisplay's ER-TFT4.58-1 leaves them
at `FF`, and its datasheet gives no values. A panel from another maker may show real bytes. That is the variance this
line is for.

The path was checked end to end on the bench board, the first time it read `FFFFFF`:

- **At idle, PB5 follows the STM32's own pull** (low with the pull-down, high with the pull-up), so nothing else holds
  the line.
- **During the read the panel drives it:** the power-mode byte reads `9C`, the expected value.
- **The driver board passes the line both ways.** BuyDisplay's ER-PCBA5981-1 lists header pin 18 (`LCD_DIN`) as an input
  only, and its datasheet has no schematic. But the outline drawing shows no buffer: only the LT7680, a regulator and the
  flash chip. The LT7680 has no panel-SPI pins of its own.

**What the datasheets let you configure.**

- **ER-TFT4.58-1 panel:** nothing. The interface is fixed at 3-wire SPI plus 18-bit RGB by the ST7701S's `IM[3:0]` pins,
  which are wired on the panel itself.
- **ER-PCBA5981-1 driver board:**
  - J1/J2 choose the backlight control: J1 short and J2 open = an external input (the default), J1 open and J2 short =
    the LT7680's PWM.
  - JP1 is the board's flash programming header.
  - Nothing affects the panel's SPI line.

Datasheets used: [ER-PCBA5981-1](https://www.buydisplay.com/download/manual/ER-PCBA5981-1_Datasheet.pdf) (the LT7680
controller board), [ER-TFT4.58-1](https://www.buydisplay.com/download/manual/ER-TFT4.58-1_Datasheet.pdf) (the panel),
and Sitronix's ST7701S datasheet V1.1.

## Reading a board's identity over SWD

With the board powered by the meter and an ST-Link on SWD:

```bash
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg -c "init" -c "mdw 0xE000ED00" -c "mdw 0xE0042000" -c "mdh 0x1FFFF7E0" -c "mww 0xE0001000 0x40000001" -c "mdw 0xE0001000" -c "mdw 0xE0001004" -c "mdw 0xE0001004" -c "shutdown"
```

- **First line:** the CPUID. `0x411FC231` is what ST's STM32F103 reports.
- **Second line:** the device ID; the low 12 bits are `410` on a medium-density STM32F103.
- **Third line:** the flash size in KB, in hex.
- **The `mww`:** enables the cycle counter.
- **Fourth line:** `DWT_CTRL` (bit 25 must be 0).
- **Last two lines:** must differ.

This works while the firmware is running and changes nothing in flash. The firmware enables the counter at boot anyway.
