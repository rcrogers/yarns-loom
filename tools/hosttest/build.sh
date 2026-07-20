#!/bin/sh
# Host-compile the REAL yarns/envelope.cc and run the 23-check battery.
# envelope_host.cc = envelope.cc with the one smull asm swapped for C
# (regenerate whenever envelope.cc changes).
cd "$(dirname "$0")"
python3 - <<'PYEOF'
src = open('../../yarns/envelope.cc').read()
old = '''      int32_t ramp_lo, ramp_hi;
      __asm__("smull %0, %1, %2, %3"
              : "=&r"(ramp_lo), "=r"(ramp_hi)
              : "r"(slew_alpha_q31), "r"(decay_q32));
      slew_alpha_q31 -= ramp_hi;'''
new = '''      int32_t ramp_hi = (int32_t)(
        ((int64_t)slew_alpha_q31 * (int32_t)decay_q32) >> 32);
      slew_alpha_q31 -= ramp_hi;'''
assert old in src, "smull anchor moved -- update build.sh"
open('envelope_host.cc','w').write(src.replace(old, new))
PYEOF
clang++ -std=c++11 -O1 -w -DTEST -I shim -I ../.. envelope_host.cc ../../yarns/resources.cc driver.cc -o test && node analyze.js
