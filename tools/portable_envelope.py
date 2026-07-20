#!/usr/bin/env python3
"""Emit a host-portable copy of yarns/envelope.cc.

The firmware source is the ONE source of truth for the chiff model. Every
off-target consumer (the clang test harness, the Emscripten engine that drives
chiff_sim.html) is compiled from this transform rather than reimplementing the
model, so none of them can drift from the firmware.

The only thing that cannot survive off ARM is the inline `smull`; it is
replaced by the identical 64-bit expression. Everything else is byte-identical
to yarns/envelope.cc.

Usage: portable_envelope.py <repo_root> <output.cc>
"""
import sys

SMULL_ASM = '''      int32_t ramp_lo, ramp_hi;
      __asm__("smull %0, %1, %2, %3"
              : "=&r"(ramp_lo), "=r"(ramp_hi)
              : "r"(slew_alpha_q31), "r"(decay_q32));
      slew_alpha_q31 -= ramp_hi;'''

SMULL_C = '''      int32_t ramp_hi = (int32_t)(
        ((int64_t)slew_alpha_q31 * (int32_t)decay_q32) >> 32);
      slew_alpha_q31 -= ramp_hi;'''


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    repo_root, output = sys.argv[1], sys.argv[2]
    source = open(repo_root + '/yarns/envelope.cc').read()
    if SMULL_ASM not in source:
        sys.exit('portable_envelope: smull anchor moved -- update this script '
                 'to match yarns/envelope.cc')
    open(output, 'w').write(source.replace(SMULL_ASM, SMULL_C))


if __name__ == '__main__':
    main()
