#!/usr/bin/env python3
"""Emit a host-portable copy of yarns/envelope.cc.

The firmware source is the ONE source of truth for the chiff model. Every
off-target consumer (the clang test harness, the Emscripten engine that drives
chiff_sim.html) is compiled from this transform rather than reimplementing the
model, so none of them can drift from the firmware.

The render loop's ARM asm is guarded by `#if defined(__arm__) && __ARM_ARCH
>= 7`, so off-target compilers (the clang host harness, the Emscripten sim)
define no __arm__ and take the pure-C `#else` reference -- nothing to swap.
This stays the single entry point (build.sh and simengine call it); SWAPS
below stays empty unless an *unguarded* ARM-only construct is introduced.

Usage: portable_envelope.py <repo_root> <output.cc>
"""
import sys

# (arm_asm, portable_c) pairs for any *unguarded* ARM-only construct. Empty:
# the render loop's asm is behind `#if defined(__arm__) && __ARM_ARCH >= 7`,
# so the host preprocessor already takes the pure-C #else -- nothing to swap.
SWAPS = []


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    repo_root, output = sys.argv[1], sys.argv[2]
    source = open(repo_root + '/yarns/envelope.cc').read()
    for arm_asm, portable_c in SWAPS:
        if arm_asm not in source:
            sys.exit('portable_envelope: a SWAP anchor moved -- update this '
                     'script to match yarns/envelope.cc')
        source = source.replace(arm_asm, portable_c)
    open(output, 'w').write(source)


if __name__ == '__main__':
    main()
