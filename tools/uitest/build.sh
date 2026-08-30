#!/bin/sh
# Host-compile the REAL yarns/drivers/display.cc and run the display checks.
#
# -DAPPLICATION, because that is the build the module runs: without it
# RefreshSlow has no scrolling, no blink counter and no prefix flash, which is
# most of what there is to check.
#
# gpio_stub.cc is the only substitution. The driver bit-bangs four pins on
# GPIOB and the stub decodes them back into the segment word standing at each
# character, so the checks read the panel rather than the driver's members.
cd "$(dirname "$0")"
clang++ -std=c++11 -O1 -w -DTEST -DAPPLICATION -I ../hosttest/shim -I ../.. \
  driver.cc gpio_stub.cc ../../yarns/drivers/display.cc \
  ../../yarns/resources.cc -o uitest || exit 1
node display.js || exit 1
