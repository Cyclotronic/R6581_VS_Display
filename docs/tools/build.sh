#!/bin/bash
# Build the R6581_VS_Display firmware with the Arm GNU toolchain.
#   build.sh <image-name>            -> docs/tools/images/<image-name> (override with OUT_DIR)/firmware.{elf,hex,bin,map}
# Environment:
#   OPT=0|2|s|1|3        optimisation level (default 0 = what Ian's Debug build uses)
#   EXTRA_CFLAGS="..."   e.g. "-DGLYPH_SELFTEST=1", "-DPAGEFLIP=1 -DTIMING_PROBE=1"
#   REPO=<path>          source tree (default: this repository)
#   ARM_GNU_BIN=<path>   toolchain bin dir (default the per-user unzip of Arm GNU Toolchain 14.2.Rel1)
# Compiler flags come from the repo's own VisualGDB *.gcc.rsp, minus the VisualGDB profiler hooks.
# GCC 14 makes an implicit function declaration an error; Ian's code has one (DisplayCloneDeterminationAux),
# so those three diagnostics are kept as warnings rather than editing his source.
set -e
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NAME=${1:?usage: build.sh <image-name>}
REPO=${REPO:-$HERE/../..}
OUT=${OUT_DIR:-$HERE/images}/$NAME
OBJ=${TMPDIR:-/tmp}/r6581-obj-$NAME
TC=${ARM_GNU_BIN:-/c/Users/$USERNAME/AppData/Local/arm-gnu-toolchain/bin}
CC=$TC/arm-none-eabi-gcc.exe
OPT=${OPT:-0}
mkdir -p "$OUT" "$OBJ"; cd "$REPO"
CF="-mthumb -mcpu=cortex-m3 -ICore/Inc -IDrivers/CMSIS/Device/ST/STM32F1xx/Include -IDrivers/CMSIS/Include -IDrivers/STM32F1xx_HAL_Driver/Inc -ggdb -O$OPT -DARM_MATH_CM3 -Dflash_layout -DSTM32F103C8 -DUSE_HAL_DRIVER -DSTM32F103xB -DDEBUG=1 -ffunction-sections -fdata-sections -Wall -Wno-error=implicit-function-declaration -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -Wno-unused-variable -Wno-unused-but-set-variable $EXTRA_CFLAGS"
objs=()
for f in Core/Src/*.c Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal{,_cortex,_dma,_exti,_flash,_flash_ex,_gpio,_gpio_ex,_pwr,_rcc,_rcc_ex,_spi,_tim,_tim_ex}.c; do
  o="$OBJ/$(basename ${f%.c}).o"; "$CC" $CF -c "$f" -o "$o"; objs+=("$o")
done
o="$OBJ/startup.o"; "$CC" $CF -x assembler-with-cpp -c Core/Startup/startup_stm32f103c8tx.S -o "$o"; objs+=("$o")
"$CC" -o "$OUT/firmware.elf" -Wl,-gc-sections -Wl,-Map="$OUT/firmware.map" -TFiles/STM32F103C8TX_FLASH.ld --specs=nano.specs --specs=nosys.specs -Wl,--no-warn-rwx-segments -mthumb -mcpu=cortex-m3 "${objs[@]}"
"$TC/arm-none-eabi-objcopy.exe" -O ihex "$OUT/firmware.elf" "$OUT/firmware.hex"
"$TC/arm-none-eabi-objcopy.exe" -O binary "$OUT/firmware.elf" "$OUT/firmware.bin"
"$TC/arm-none-eabi-size.exe" "$OUT/firmware.elf"
echo "OPT=-O$OPT  EXTRA_CFLAGS=$EXTRA_CFLAGS" > "$OUT/BUILD_FLAGS.txt"
