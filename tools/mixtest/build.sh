#!/bin/sh
# Host-compile the REAL CV output path and check that no shape, at any voice
# count, leaves the span voice.h hands out. driver.cc says why this is the one
# place the check belongs.
cd "$(dirname "$0")"
clang++ -std=c++11 -O1 -w -DTEST -I ../hosttest/shim -I ../.. \
  driver.cc ../cvtest/dac_stub.cc \
  ../../yarns/voice.cc ../../yarns/oscillator.cc ../../yarns/envelope.cc \
  ../../yarns/resources.cc ../../yarns/utils.cc -o mixtest || exit 1
./mixtest "$@" || exit 1
