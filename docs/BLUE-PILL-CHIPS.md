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

Hold DCV at power-up to open the timing-adjust page. Below the timing rows, two cyan lines identify the hardware, so a
board that behaves differently can be described without an ST-Link:

```
MCU    CPUID 411FC231  ID 00000000  Flash 64KB  Cycle counter OK
Panel  ST7701S ID no reply   LT7680 OK, 1 try
```

| Field | Where it comes from | On the bench board |
|---|---|---|
| `CPUID` | the Cortex-M3's `SCB->CPUID` | `411FC231` (Arm Cortex-M3 r1p1, as ST's STM32F103) |
| `ID` | `DBGMCU_IDCODE`. ST's errata say firmware reads it as 0 on an STM32F103 unless a debugger is attached; a clone that shows a value here behaves differently from ST's part | `20036410` with the ST-Link attached; the page shows what it reads without one |
| `Flash` | the flash-size word at `0x1FFFF7E0` | `64KB` |
| `Cycle counter` | the DWT cycle counter: present, enabled, and advancing | `OK` |
| `ST7701S ID` | the panel's RDID1-3 bytes, read once at boot over the panel's SPI. The data pin is released and read once pulled up and once pulled down | `no reply`: the line reads high whatever the pull, so the panel's data line isn't read back on this board |
| `LT7680` | how the controller's set-up ended: `OK`, or the fault bits (`01` reset, `02` no reply, `04` PLL, `08` SDRAM), and the number of tries | `OK, 1 try` |

**The LT7680 has no ID register.** Every LT768x variant has the same 128 Mb of SDRAM, so memory size doesn't tell
them apart either. What the page can show is whether the controller answered and set itself up cleanly.

**The panel ID** costs about 0.2 ms at boot. A board whose panel does answer would show its three ID bytes, which
identify the panel's controller and module maker. On Ian's board the line reads high with either pull, so it shows
`no reply`; the panel's own data output (SDO) may not be connected. `lcd_id` holds the same value for SWD
(`0xFFFFFFFF` = no reply).

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
