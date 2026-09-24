#!/bin/sh
# Host-compile the REAL yarns/oscillator.cc and pin every shape's output.
#
# rng_stub.cc supplies stmlib::Random's state, which the noise shapes draw
# from; the driver reseeds it per shape so one shape's hash does not depend on
# how many draws the shapes before it took.
cd "$(dirname "$0")"
clang++ -std=c++11 -O1 -w -DTEST -I ../hosttest/shim -I ../.. \
  driver.cc ../../yarns/oscillator.cc ../../yarns/envelope.cc \
  ../../yarns/resources.cc ../../yarns/utils.cc ../warptest/rng_stub.cc -o osctest || exit 1
node golden.js || exit 1
# And that no shape wraps when the timbre goes below zero, which a negative
# TIMBRE MOD ENVELOPE reaches. driver.cc's `negative` mode says why.
./osctest negative || exit 1
# EVERY SHAPE THROUGH THE SANITISER, over what its own warp can hand it. Three
# faults came out of this that no golden could see: a negative cutoff shifted
# into a table index, a signed value shifted left as though unsigned, and a
# divide whose divisor reaches zero. ubsan.cc says how the domain is chosen.
clang++ -std=c++11 -O1 -g -w -DTEST \
  -fsanitize=signed-integer-overflow,shift,integer-divide-by-zero \
  -fno-sanitize-recover=all  -I ../hosttest/shim -I ../.. \
  ubsan.cc ../../yarns/oscillator.cc ../../yarns/envelope.cc \
  ../../yarns/resources.cc ../../yarns/utils.cc ../warptest/rng_stub.cc \
  -o oscubsan || exit 1
./oscubsan || exit 1
