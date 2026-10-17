# Ian's 1 V display mode (off by default in this fork)

## What it does

On the **1000 mV DCV range**, pressing the front-panel **DCV** key toggles a display mode that shows the reading in volts instead of
millivolts. The firmware takes the main line, for example `+ 999.99709   mVDC`, and shows it as `+ 0.99999709   VDC` (the number
divided by 1000, by moving the decimal point, so no digits are lost), and replaces the aux line `1000mV Range` with `1 V Range`.
Pressing DCV again turns it off. The meter itself is not affected; it is only a different way of drawing the same reading.

It applies only when all of these are true: the mode is on, the aux line says `1000mV Range`, the main line has digits and is not
`OVERLOAD`. On every other range or function the DCV press does nothing visible. The state is not kept across power cycles.

## How it works in the code

- `Core/Src/main.c`: in the main loop, a DCV press (pins A11 and A12 both low) toggles `oneVoltmode` once per press. (Holding DCV
  at power-up is a different thing: the TFT timing-adjust mode, and in this fork the reset of the boot-screen count.)
- `Core/Src/display.c`, `DisplayMain()`: when `oneVoltmode && onethousandmVmodedetected`, the digits are re-formatted with `sscanf`
  and string moves, then drawn with the normal main-line format. `DisplayAux()` sets `onethousandmVmodedetected` and draws `1 V Range`.

## Why it is off in this fork

Hydron did not want it in his copy, and the build made for his testing leaves it out. The code is still there, behind a build option:

| You want | Build with |
| --- | --- |
| 1 V mode off (default) | nothing; the DCV press no longer toggles it, so the code is inert |
| Ian's 1 V mode back | `-DONE_VOLT_MODE=1`, e.g. `EXTRA_CFLAGS="-DONE_VOLT_MODE=1" bash docs/tools/build.sh my-image` |

A build with `-DONE_VOLT_MODE=1` is byte-identical to the previous test build, which behaved as Ian's firmware does.

## Checked on the bench

With the mode forced on (the flag set over SWD, equivalent to one DCV press) on the 1000 mV range, the TFT showed the reading in volts and
`1 V Range` on the aux line, as described above. The redraw time did not change (21.1 ms with the mode on against 21.0 ms off, `-O2`
timing probe), so the conversion costs nothing noticeable. The front-panel press itself was not exercised.
