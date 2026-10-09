#!/bin/sh
# Host-compile the REAL oscillator, driven as a PART: N voices into one mix.
cd "$(dirname "$0")"
clang++ -std=c++11 -O1 -w -DTEST -I ../hosttest/shim -I ../.. \
  driver.cc ../../yarns/oscillator.cc ../../yarns/envelope.cc \
  ../../yarns/resources.cc ../../yarns/utils.cc ../warptest/rng_stub.cc -o paratest || exit 1
