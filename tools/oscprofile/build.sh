#!/bin/sh
# Link the firmware's own oscillator objects into a QEMU image and disassemble
# it. Runs in the toolchain container, from the repo root, after a firmware
# build (make firmware): build/yarns/*.o are the objects that ship.
#
# The driver is compiled with the firmware's exact flags, taken from the
# makefile's own command for oscillator.o: -fshort-enums alone changes the ABI
# of every enum parameter the driver passes.
set -e
OUT=build/oscprofile
BIN=/usr/local/arm-4.8.3/bin
mkdir -p "$OUT"
for object in oscillator envelope resources utils random; do
  [ -f "build/yarns/$object.o" ] || { echo "no build/yarns/$object.o: make firmware first"; exit 1; }
done
FLAGS=$(make -f yarns/makefile -n -B build/yarns/oscillator.o 2>/dev/null \
  | grep -- ' -c .*yarns/oscillator.cc -o ' \
  | sed -e 's/^[^ ]* -c //' -e 's/ yarns\/oscillator.cc -o build\/yarns\/oscillator.o$//' \
        -e 's/ -fstack-usage//')
[ -n "$FLAGS" ] || { echo "cannot read the firmware's compile flags"; exit 1; }
ARCH="-mcpu=cortex-m3 -mthumb -mfloat-abi=soft"

$BIN/arm-none-eabi-g++ -c $FLAGS tools/oscprofile/driver.cc -o $OUT/driver.o
$BIN/arm-none-eabi-gcc $ARCH -O2 -std=gnu99 -c tools/oscqemu/startup.c -o $OUT/startup.o
$BIN/arm-none-eabi-g++ $ARCH --specs=nano.specs -nostartfiles \
  -T tools/oscprofile/profile.ld -Wl,--gc-sections \
  $OUT/driver.o $OUT/startup.o \
  build/yarns/oscillator.o build/yarns/envelope.o build/yarns/resources.o \
  build/yarns/utils.o build/yarns/random.o -lm \
  -o $OUT/profile.elf
# -dl: osc_profile.py reports unexecuted instructions by source line.
$BIN/arm-none-eabi-objdump -dl $OUT/profile.elf > $OUT/profile.dis
$BIN/arm-none-eabi-objdump -d build/yarns/yarns.elf > $OUT/firmware.dis
$BIN/arm-none-eabi-nm $OUT/profile.elf > $OUT/profile.sym
