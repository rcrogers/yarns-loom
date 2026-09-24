#!/bin/sh
# Compile the envelope render loop's ARM asm path into a bare-metal Cortex-M3
# ELF (tools/qemutest/test.elf) for the QEMU differential harness. Runs inside
# the docker image (arm-none-eabi + qemu). verify.sh calls this once, then runs
# test.elf per scenario under QEMU and diffs against the host C reference.
#
# Reuses the host harness's driver.cc and dac.h shim, and compiles the REAL
# yarns/envelope.cc so __arm__ selects the hand-written asm (the whole point).
# -DTEST only affects the dead C-clip path; the asm path is unaffected.
set -e
cd "$(dirname "$0")"
ROOT=../..
BIN=/usr/local/arm-4.8.3/bin
GXX="$BIN/arm-none-eabi-g++"
GCC="$BIN/arm-none-eabi-gcc"

ARCH="-mcpu=cortex-m3 -mthumb -mfloat-abi=soft"
CXXFLAGS="$ARCH -O2 -DTEST -ffunction-sections -fdata-sections \
  -fno-exceptions -fno-rtti -I ../hosttest/shim -I $ROOT"

$GXX $CXXFLAGS -c "$ROOT/yarns/envelope.cc"  -o envelope.o
$GXX $CXXFLAGS -c "$ROOT/yarns/resources.cc" -o resources.o
$GXX $CXXFLAGS -c "$ROOT/yarns/utils.cc"     -o utils.o
$GXX $CXXFLAGS -c ../hosttest/driver.cc      -o driver.o
$GCC $ARCH -O2 -std=gnu99 -c startup.c -o startup.o

$GXX $ARCH --specs=nano.specs -nostartfiles -T lm3s6965.ld -Wl,--gc-sections \
  envelope.o resources.o utils.o driver.o startup.o -o test.elf
