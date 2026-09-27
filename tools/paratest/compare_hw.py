#!/usr/bin/env python3
"""CALIBRATE THE HARNESS AGAINST THE MODULE.

Everything reach.py says is model-against-model. The harness reproduces a code
path, not a Yarns: five differences between the two have been found by reading
the firmware and fixed, and the renders still do not sound like the hardware.
Reading further is guessing, and guessing has a record here.

So this takes a RECORDING of the real thing and reports, feature by feature,
where the harness disagrees with it. A feature that matches is one the harness
models; one that does not is the next thing to fix, named rather than guessed.
Until this has been run, no absolute claim from reach.py is safe -- only its
internal ordering is.

  ./venv-analysis/bin/python tools/paratest/compare_hw.py recording.wav \
      -- voices=1 alloc=1 noteon=0 switch=1 from=3 to=4 blocks=900 \
         settle=700 prime=4 pitch=69 timbre=18000

The recording wants to be the bare glitch: one note's worth, no effects, and
the sample rate in the file's header. Mono or stereo both work.
"""
import sys, wave, struct, math
import numpy as np
sys.path.insert(0, "/Users/rcrogers/Repos/mutable-instruments/mutable-dev-environment/eurorack-modules/tools/paratest")
from reach import render, features, KEYS

def read_wav(path):
    w = wave.open(path)
    n, ch, width, rate = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
    raw = w.readframes(n)
    if width == 2:
        x = np.array(struct.unpack("<%dh" % (n * ch), raw), dtype=float)
    elif width == 3:
        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        x = ((b[:, 2] << 16) | (b[:, 1] << 8) | b[:, 0]).astype(np.int32)
        x = np.where(x & 0x800000, x - (1 << 24), x).astype(float) / 256.0
    else:
        raise SystemExit("need 16- or 24-bit PCM, got %d-bit" % (width * 8))
    if ch > 1:
        x = x.reshape(-1, ch).mean(axis=1)
    # the harness renders at 45 kHz; resample by linear interpolation so the
    # time-domain features are comparable
    if rate != 45000:
        t = np.arange(x.size) / rate
        t2 = np.arange(0, t[-1], 1 / 45000.0)
        x = np.interp(t2, t, x)
    # trim leading silence so the onset lines up with a render's
    env = np.abs(x)
    thr = env.max() * 0.02
    hit = np.nonzero(env > thr)[0]
    if hit.size:
        x = x[max(0, hit[0] - 64):]
    return x

if __name__ == "__main__":
    if "--" not in sys.argv:
        raise SystemExit(__doc__)
    cut = sys.argv.index("--")
    wav = sys.argv[1]
    kw = dict(a.split("=", 1) for a in sys.argv[cut+1:])
    hw = features(read_wav(wav))
    sim = features(render("paratest", **kw))
    if hw is None or sim is None:
        raise SystemExit("one of the two had nothing measurable in it")
    print("%-16s %14s %14s %10s" % ("feature", "hardware", "harness", "ratio"))
    print("-" * 58)
    for k in KEYS:
        a, b = hw[k], sim[k]
        r = (a / b) if abs(b) > 1e-9 else float("inf")
        flag = "" if 0.5 <= abs(r) <= 2.0 else "   <-- differs"
        print("%-16s %14.2f %14.2f %10.2f%s" % (k, a, b, r, flag))
    print("\nfeatures marked differ are what the harness does not model.")
