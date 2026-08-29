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
  ../../yarns/resources.cc ../../yarns/utils.cc -o cvtest || exit 1
# And the PANEL CHAIN above it. part.cc links here -- the arpeggiator, the
# looper, the just-intonation processor and settings all compile for the host,
# and the two globals it reaches for are defined in the driver, because multi.cc
# and midi_handler.cc are the ui.h-bound ones.
clang++ -std=c++11 -O1 -w -DTEST -I ../hosttest/shim -I ../.. \
  panel_driver.cc dac_stub.cc \
  ../../yarns/part.cc ../../yarns/voice.cc ../../yarns/oscillator.cc \
  ../../yarns/envelope.cc ../../yarns/arpeggiator.cc ../../yarns/looper.cc \
  ../../yarns/just_intonation_processor.cc ../../yarns/settings.cc \
  ../../yarns/resources.cc ../../yarns/utils.cc -o paneltest || exit 1
# Voice::NoteOn forces a release before every triggered note, so the first note
# after boot releases an envelope that has no ADSR yet. That read is undefined
# and lands in the vector table on target, where it faults nothing -- ASan is
# what says it happened.
clang++ -std=c++11 -O1 -w -DTEST -fsanitize=address -I ../hosttest/shim -I ../.. \
  driver.cc dac_stub.cc \
  ../../yarns/voice.cc ../../yarns/oscillator.cc ../../yarns/envelope.cc \
  ../../yarns/resources.cc ../../yarns/utils.cc -o cvtest_asan || exit 1
./cvtest_asan chiff > /dev/null || { echo "ASan FAILED: chiff"; exit 1; }
./cvtest_asan dac blocks=8 > /dev/null || { echo "ASan FAILED: dac"; exit 1; }
echo "ASan clean"
node parity.js || exit 1
# The sim's copy of Part::VoiceNoteOn against the original.
node panel.js || exit 1
