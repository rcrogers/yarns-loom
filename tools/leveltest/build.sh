#!/bin/sh
# Per-voice output level, measured on the real render path. Not a gate: the
# numbers are the input to voicing decisions the user makes, and freezing them
# would freeze a decision that has not been taken.
cd "$(dirname "$0")"
# One source of truth for the labels: the enum itself, commented-out entries
# skipped so the indices stay aligned with fn_table_.
{ echo 'static const char* const kShapeNames[] = {'
  sed -n '/^enum OscillatorShape/,/^};/p' ../../yarns/oscillator.h \
    | grep -o '^  OSC_SHAPE_[A-Z_0-9]*' | sed 's/^  OSC_SHAPE_/  "/;s/$/",/'
  echo '};'
} > shape_names.h
clang++ -std=c++11 -O1 -w -DTEST -I ../hosttest/shim -I ../.. \
  driver.cc ../cvtest/dac_stub.cc ../../yarns/voice.cc ../../yarns/oscillator.cc \
  ../../yarns/envelope.cc ../../yarns/resources.cc ../../yarns/utils.cc -o leveltest || exit 1
