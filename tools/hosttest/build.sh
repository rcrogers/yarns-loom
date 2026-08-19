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
clang++ -std=c++11 -O1 -w -DTEST -I shim -I ../.. envelope_host.cc ../../yarns/resources.cc driver.cc -o test && node battery.js
