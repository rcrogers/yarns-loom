#!/bin/sh
# Compile the OSCILLATOR into a bare-metal Cortex-M3 ELF, so its renders can be
# run under QEMU and diffed against the same code compiled for the host.
#
# This is the twin of tools/qemutest, which does it for the envelope's render
# loop. It exists for the same reason: hand-written asm has to be proven
# IDENTICAL to the C it replaces, and no golden can do that -- a golden pins
# what the code does, not that two implementations of it agree.
#
# It is worth having BEFORE any asm is written. Run against a tree with no asm
# in it, it proves the harness itself: host and target must already agree, and
# any difference found now is a portability bug in the C, not an asm bug.
set -e
cd "$(dirname "$0")"
ROOT=../..
BIN=/usr/local/arm-4.8.3/bin
GXX="$BIN/arm-none-eabi-g++"
GCC="$BIN/arm-none-eabi-gcc"

ARCH="-mcpu=cortex-m3 -mthumb -mfloat-abi=soft"
CXXFLAGS="$ARCH -O2 -DTEST -ffunction-sections -fdata-sections \
  -fno-exceptions -fno-rtti -fshort-enums -I ../hosttest/shim -I $ROOT"

$GXX $CXXFLAGS -c "$ROOT/yarns/oscillator.cc" -o oscillator.o
$GXX $CXXFLAGS -c "$ROOT/yarns/envelope.cc"   -o envelope.o
$GXX $CXXFLAGS -c "$ROOT/yarns/resources.cc"  -o resources.o
$GXX $CXXFLAGS -c "$ROOT/yarns/utils.cc"      -o utils.o
$GXX $CXXFLAGS -c ../osctest/driver.cc        -o driver.o
$GXX $CXXFLAGS -c ../warptest/rng_stub.cc     -o rng_stub.o
$GCC $ARCH -O2 -std=gnu99 -c startup.c        -o startup.o

$GXX $ARCH --specs=nano.specs -nostartfiles -T lm3s6965.ld -Wl,--gc-sections \
  oscillator.o envelope.o resources.o utils.o driver.o rng_stub.o startup.o \
  -o test.elf
