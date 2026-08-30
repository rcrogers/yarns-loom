#!/bin/sh
# Does a SIGNED timbre modulation survive each shape's warp? A warp is an
# absolute-position map, so warping a delta is meaningless -- this calls the
# real Oscillator::WarpTimbre and WarpTimbreDelta and reports, per shape,
# whether a negative modulation still moves timbre downward.
cd "$(dirname "$0")"
python3 ../portable_envelope.py ../.. envelope_host.cc
clang++ -std=c++11 -O1 -w -DTEST -I ../hosttest/shim -I ../.. \
  warptimbre.cc ../../yarns/oscillator.cc ../../yarns/resources.cc ../../yarns/utils.cc \
  envelope_host.cc rng_stub.cc -o warptimbre && ./warptimbre
