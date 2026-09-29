#!/bin/sh
cd "$(dirname "$0")"
sh ../leveltest/build.sh || exit 1
clang++ -std=c++11 -O1 -w -DTEST -I ../hosttest/shim -I ../.. \
  driver.cc ../cvtest/dac_stub.cc ../../yarns/voice.cc ../../yarns/oscillator.cc \
  ../../yarns/envelope.cc ../../yarns/resources.cc ../../yarns/utils.cc \
  -o steptest || exit 1
./steptest || exit 1
