#!/bin/sh
# Host-compile the REAL yarns/envelope.cc and run the check battery.
# envelope_host.cc comes from tools/portable_envelope.py -- the single source
# transform shared with the sim engine. The render loop's ARM asm is guarded by
# __arm__, so the host preprocessor takes the pure-C #else -- nothing to swap.
# Regenerated on every run.
cd "$(dirname "$0")"
python3 ../portable_envelope.py ../.. envelope_host.cc
# battery.js runs analyze.js once per seed and fails on any seed. A statistical
# limit checked against one realization is checked against luck.
clang++ -std=c++11 -O1 -w -DTEST -I shim -I ../.. envelope_host.cc ../../yarns/resources.cc ../../yarns/utils.cc driver.cc -o test || exit 1
# UNDEFINED BEHAVIOUR IS NOT VISIBLE IN THE OUTPUT. A signed overflow renders
# whatever the compiler felt like that day, and every check here would still
# pass. This build traps it instead. The cases are the ones that reach the
# extremes: a full-range note with the bias at a rail, and full tremolo.
clang++ -std=c++11 -O1 -w -DTEST -fsanitize=signed-integer-overflow,shift \
  -fno-sanitize-recover=all -I shim -I ../.. \
  envelope_host.cc ../../yarns/resources.cc ../../yarns/utils.cc driver.cc -o test_ubsan || exit 1
for case in \
  "basic 127 90 attack_setting=127 range=32767 bias_lfo=32767" \
  "basic 127 90 attack_setting=40 range=32767 bias_lfo=32767 bias_lfo_blocks=1" \
  "basic 127 127 attack_setting=8 range=32767 bias_lfo=32767 peak=100 sustain=100" \
  "basic 96 49 attack_setting=40 range=32767 tremolo=65535" \
  "basic 127 33 attack_setting=16" \
  "basic 0 64 attack_setting=40 gate=900 tremolo=48000 bias_lfo=32767 adjust_bias=2000000000" \
  "basic 0 64 attack_setting=60 gate=900 rescale=3"; do
  ./test_ubsan $case > /dev/null || { echo "UBSan FAILED: $case"; exit 1; }
done
echo "UBSan clean"
# GOLDEN FIRST. It is the bit-exactness pin, and "sample 4211 moved" localises a
# refactor that the battery would report as a statistic drifting.
node golden.js || exit 1
node battery.js || exit 1
# Envelope::Rescale has one caller in the firmware and had no test at all.
node rescale.js || exit 1
# The render loop's target overshoot: nothing failed when it was removed.
node arrival.js || exit 1
# One behavioural number per case, tolerant of small movement, against its own
# recorded baseline. Green here and red in golden means a deliberate change;
# red here means the shape moved.
node anomaly.js
