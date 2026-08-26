#!/bin/sh
# Host-compile the REAL CV output path -- yarns/voice.cc and yarns/oscillator.cc
# alongside the envelope -- and run the parity check.
#
# No transform and no portable copy: these compile for the host as they are,
# once the shim supplies <stm32f10x_conf.h>. The only substitution is the DAC
# itself, which is SPI and DMA; dac_stub.cc records what was written instead.
#
# yarns/part.cc is NOT here. ui.h pulls in the encoder driver and its GPIO
# reads, so Part::VoiceNoteOn's own chain still has no host build -- the sim is
# what mirrors it.
cd "$(dirname "$0")"
clang++ -std=c++11 -O1 -w -DTEST -I ../hosttest/shim -I ../.. \
  driver.cc dac_stub.cc \
  ../../yarns/voice.cc ../../yarns/oscillator.cc ../../yarns/envelope.cc \
  ../../yarns/resources.cc -o cvtest || exit 1
# Voice::NoteOn forces a release before every triggered note, so the first note
# after boot releases an envelope that has no ADSR yet. That read is undefined
# and lands in the vector table on target, where it faults nothing -- ASan is
# what says it happened.
clang++ -std=c++11 -O1 -w -DTEST -fsanitize=address -I ../hosttest/shim -I ../.. \
  driver.cc dac_stub.cc \
  ../../yarns/voice.cc ../../yarns/oscillator.cc ../../yarns/envelope.cc \
  ../../yarns/resources.cc -o cvtest_asan || exit 1
./cvtest_asan chiff > /dev/null || { echo "ASan FAILED: chiff"; exit 1; }
./cvtest_asan dac blocks=8 > /dev/null || { echo "ASan FAILED: dac"; exit 1; }
echo "ASan clean"
node parity.js || exit 1
