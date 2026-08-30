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
