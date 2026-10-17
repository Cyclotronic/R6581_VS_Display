# Notes from bench testing this firmware

These notes come from reviewing and testing Ian Johnston's R6581(T) TFT firmware on a real R6581T, with an
ST-Link on the Blue Pill's SWD header and a GPIB link to the meter. Ian's code is untouched where it was
not a problem; every change is explained here, with how to undo it.

| Document | What it covers |
| --- | --- |
| [BOOT-AND-SPLASH.md](BOOT-AND-SPLASH.md) | **Behaviour change:** the speed test and splash now run at boot, for the first 10 boots only. Why, how it works, how to reset it, how to get Ian's original behaviour back. |
| [TIMING-EXPERIMENTS.md](TIMING-EXPERIMENTS.md) | Redraw times per build and per screen, the 35 ms tick budget, optimisation levels, the page-flip experiment, what was tried and rejected. |
| [LAYOUT.md](LAYOUT.md) | Panel geometry, the three display rows, the layout table, the taller-middle-line experiment (rejected: lens interference). |
| [BENCH-TOOLS.md](BENCH-TOOLS.md) | The scripts in [`tools/`](tools/): build script, SWD reader, menu-walk logger, timing probe reader, slow-motion video analysis. |

The newest build (`-O2`, new boot screen) is `../Prebuilt/test-2026-10-17-O2-bootscreen/`. Ready-to-flash builds are in [`../Prebuilt/`](../Prebuilt/); each folder has its own README, checksums and flashing commands.

Other changes in this fork (sigma and ohm as loaded-once custom glyphs, red ERROR, larger annunciators) are described in
[`../Prebuilt/hydron-test-2026-10-16/README.md`](../Prebuilt/hydron-test-2026-10-16/README.md).
