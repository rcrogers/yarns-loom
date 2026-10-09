#!/usr/bin/env python3
"""Score candidates against the reference library reach.py built.

A candidate's nearest-neighbour distance says whether the shipped shapes
already make that sound. The scale comes from the library's own spread: two
different settings of a shipped shape sit ~0.26 apart, and the furthest pair
in the whole library is ~2.1. So a candidate under ~1 is a duplicate of
something we ship, and one past ~2 is outside the set entirely.

Calibrated against a listener's verdicts on named renders -- the metric is only
worth anything if it agrees with those.
"""
import json, sys, numpy as np
sys.path.insert(0, "/Users/rcrogers/Repos/mutable-instruments/mutable-dev-environment/eurorack-modules/tools/paratest")
from reach import render, features, vec, SHAPES

T = "/Users/rcrogers/.claude/jobs/064553a4/tmp"
Z = np.load(f"{T}/ref_Z.npy"); mu = np.load(f"{T}/ref_mu.npy"); sd = np.load(f"{T}/ref_sd.npy")
meta = json.load(open(f"{T}/ref_meta.json"))

# name, listener verdict, binary, params
CANDIDATES = [
  ("bowed_then_release", "identical to WHISTLE", "paratest_proto",
   dict(voices=1, alloc=1, switch=0, to=4, sustain=26000, blocks=600, exciter=0,
        attack=25, decay=15, pitch=69, timbre=20000, release_at=300)),
  ("bell_long", "within what PING can do", "paratest_proto",
   dict(voices=1, alloc=1, switch=0, to=4, sustain=0, blocks=600, exciter=0,
        attack=33, decay=15, pitch=69, timbre=26000)),
  ("mallet_hard_medium", "PING with a short sharp exciter", "paratest_proto",
   dict(voices=1, alloc=1, switch=0, to=4, sustain=0, blocks=600, exciter=0,
        attack=25, decay=15, pitch=69, timbre=20000)),
  ("tick_dry", "no physical modeling audible", "paratest_proto",
   dict(voices=1, alloc=1, switch=0, to=4, sustain=0, blocks=600, exciter=0,
        attack=10, decay=15, pitch=69, timbre=6000)),
  ("pluck_attempt_quiet", "you can do better", "paratest_proto",
   dict(voices=1, alloc=1, switch=0, to=4, sustain=0, blocks=600, exciter=0,
        attack=5, decay=15, pitch=69, timbre=26000)),
  ("GLITCH -> WHISTLE A4", "the target sound, unreachable", "paratest",
   dict(voices=1, alloc=1, noteon=0, switch=1, **{"from": 3}, to=4, blocks=900,
        settle=700, prime=4, pitch=69, timbre=18000)),
  ("GLITCH -> PING_BP A4", "the target sound, unreachable", "paratest",
   dict(voices=1, alloc=1, noteon=0, switch=1, **{"from": 3}, to=8, blocks=900,
        settle=700, prime=8, pitch=69, timbre=18000)),
  ("GLITCH -> WHISTLE A2", "same, low register", "paratest",
   dict(voices=1, alloc=1, noteon=0, switch=1, **{"from": 3}, to=4, blocks=900,
        settle=700, prime=4, pitch=45, timbre=18000)),
]

print("%-28s %-34s %7s  %s" % ("candidate", "listener verdict", "NN dist", "nearest shipped setting"))
print("-" * 118)
for name, verdict, binary, kw in CANDIDATES:
    x = render(binary, **kw)
    f = features(x)
    if f is None:
        print("%-28s %-34s %7s  (below the noise floor -- nothing to compare)" % (name, verdict, "n/a"))
        continue
    z = (vec(f) - mu) / sd
    d = np.linalg.norm(Z - z, axis=1)
    i = int(d.argmin())
    m = meta[i]
    print("%-28s %-34s %7.2f  %s Q=%d atk=%d sus=%d exc=%d p=%d"
          % (name, verdict, d[i], m["shape"], m["timbre"], m["attack"],
             m["sustain"], m["exciter"], m["pitch"]))
