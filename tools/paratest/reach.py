#!/usr/bin/env python3
"""IS THIS SOUND NEW, or can the shapes we already ship make it?

Every acoustic claim in the resonator work so far was a guess checked by ear
one render at a time, and three of the five guesses were wrong -- a render that
"sounded like a pluck" turned out to be WHISTLE unchanged. The ear is the
arbiter, but it cannot be asked 400 times, and a single number over one render
says nothing about whether the SAME sound was already available.

So: render a dense REFERENCE LIBRARY from the shipped shapes over their whole
control grid, describe every render by the same feature vector, and ask of a
candidate how far it sits from the nearest reference. A candidate that lands on
top of a reference is not a new sound however it was produced. One that sits far
away is new, and worth an ear.

The metric is calibrated against verdicts a listener already gave, which is the
only thing that makes it trustworthy: it has to agree that the renders called
"identical to WHISTLE" and "within what PING can do" are near-duplicates.

  ./venv-analysis/bin/python tools/paratest/reach.py
"""
import subprocess, sys, math, itertools
import numpy as np

ROOT = "/Users/rcrogers/Repos/mutable-instruments/mutable-dev-environment/eurorack-modules"
RATE = 45000.0
BLOCK = 64

SHAPES = {4: "WIND", 5: "WHISTLE", 6: "PING_LP", 7: "PING_BP", 8: "PING_HP"}

def render(binary, **kw):
    args = [f"{ROOT}/tools/paratest/{binary}", "dump"]
    args += [f"{k}={v}" for k, v in kw.items()]
    out = subprocess.run(args, capture_output=True, text=True).stdout
    return np.fromstring(out, dtype=np.int32, sep="\n") if out.strip() else np.zeros(1)

def features(x):
    """A fixed description of one render. Each entry is something the ear
    attends to in a struck/bowed resonator, and nothing here is a proxy for
    another: level, how long it rings, whether the decay is one exponential,
    how wide the onset is, how wide it settles, how long that takes, and
    whether it sustains at all."""
    x = x.astype(np.float64)
    if x.size < 4096 or np.all(x == 0):
        return None
    # OSCILLATION, NOT OFFSET. PING drives its filter with the envelope's DC, so
    # a sustained PING is a standing offset with ~4 counts of AC in it. Measured
    # with the offset included it reads as loud as a ring and matches one, which
    # is how an earlier version of this file called a DC thump a pluck. Every
    # amplitude below is taken from the signal with its local mean removed.
    dc = np.convolve(x, np.ones(BLOCK) / BLOCK, mode="same")
    ac = x - dc
    dc_level = float(np.abs(dc).max())
    env = np.array([np.abs(ac[i:i+BLOCK]).max() for i in range(0, ac.size - BLOCK, BLOCK)])
    pk = env.max()
    if pk < 8:                                  # below the DAC's own noise
        return None
    at = int(env.argmax())
    # RISE. The one thing that separates a charged filter being connected from
    # an envelope opening a gate, and its absence is why the first version of
    # this file called the switch glitch a duplicate of WHISTLE.
    lo, hi = 0.1 * pk, 0.9 * pk
    above_lo = np.nonzero(env[:at+1] >= lo)[0]
    above_hi = np.nonzero(env[:at+1] >= hi)[0]
    i_lo = int(above_lo[0]) if above_lo.size else 0
    i_hi = int(above_hi[0]) if above_hi.size else at
    rise_ms = max(i_hi - i_lo, 0) * BLOCK / RATE * 1000
    to_peak_ms = at * BLOCK / RATE * 1000
    # ring: blocks from the peak to -20 dB, and whether that fall is straight
    below = np.nonzero(env[at:] < pk / 10.0)[0]
    decay_ms = (below[0] * BLOCK / RATE * 1000) if below.size else 1e4
    seg = env[at:at + max(4, (below[0] if below.size else 40))]
    seg = np.maximum(seg, 1.0)
    if seg.size >= 4:
        t = np.arange(seg.size)
        fit = np.polyfit(t, np.log(seg), 1)
        resid = np.log(seg) - np.polyval(fit, t)
        ss = np.sum((np.log(seg) - np.log(seg).mean()) ** 2)
        decay_r2 = 1 - np.sum(resid ** 2) / ss if ss > 0 else 0.0
    else:
        decay_r2 = 0.0
    # spectrum per window, from the onset
    N = 1024
    win = np.hanning(N)
    spreads, inbands, centroids = [], [], []
    for s in range(0, min(ac.size - N, int(0.35 * RATE)), N // 2):
        seg2 = ac[s:s+N] * win
        if np.abs(seg2).max() < 4:
            spreads.append(np.nan); inbands.append(np.nan); centroids.append(np.nan); continue
        p = np.abs(np.fft.rfft(seg2)) ** 2
        p[0] = 0
        tot = p.sum()
        if tot <= 0:
            spreads.append(np.nan); inbands.append(np.nan); centroids.append(np.nan); continue
        f = np.fft.rfftfreq(N, 1 / RATE)
        c = float((f * p).sum() / tot)
        centroids.append(c)
        spreads.append(float(math.sqrt(((f - c) ** 2 * p).sum() / tot)))
        k = int(p.argmax())
        inbands.append(float(p[max(0, k-1):k+2].sum() / tot))
    spreads = np.array(spreads); inbands = np.array(inbands); centroids = np.array(centroids)
    ok = ~np.isnan(spreads)
    if ok.sum() < 2:
        return None
    onset_spread = float(spreads[ok][0]); onset_inband = float(inbands[ok][0])
    onset_centroid = float(centroids[ok][0])
    settled_spread = float(np.nanmedian(spreads[ok][-3:]))
    settled_inband = float(np.nanmedian(inbands[ok][-3:]))
    tonal = np.nonzero(inbands[ok] > 0.95)[0]
    settle_ms = (tonal[0] * (N / 2) / RATE * 1000) if tonal.size else 1e4
    # does it sustain? late rms against the peak's
    late = ac[int(0.6 * ac.size):]
    sustain_ratio = float(np.sqrt((late ** 2).mean()) / pk) if late.size else 0.0
    return dict(
        peak_db=20 * math.log10(pk / 32767.0),
        rise_ms=rise_ms,
        to_peak_ms=to_peak_ms,
        decay_ms=min(decay_ms, 1e4),
        decay_r2=decay_r2,
        onset_spread=onset_spread,
        settled_spread=settled_spread,
        settle_ms=min(settle_ms, 1e4),
        onset_inband=onset_inband,
        settled_inband=settled_inband,
        centroid_fall=onset_centroid / max(1.0, float(np.nanmedian(centroids[ok][-3:]))),
        sustain_ratio=sustain_ratio,
        ac_over_dc=pk / max(dc_level, 1.0),
    )

KEYS = ["peak_db", "rise_ms", "to_peak_ms", "decay_ms", "decay_r2", "onset_spread", "settled_spread",
        "settle_ms", "onset_inband", "settled_inband", "centroid_fall", "sustain_ratio", "ac_over_dc"]

def vec(f):
    # log the quantities the ear hears logarithmically, so a factor is a step
    return np.array([
        f["peak_db"],
        math.log10(max(f["rise_ms"], 0.5)) * 20,
        math.log10(max(f["to_peak_ms"], 0.5)) * 20,
        math.log10(max(f["decay_ms"], 1.0)) * 20,
        f["decay_r2"] * 20,
        math.log10(max(f["onset_spread"], 1.0)) * 20,
        math.log10(max(f["settled_spread"], 1.0)) * 20,
        math.log10(max(f["settle_ms"], 1.0)) * 20,
        f["onset_inband"] * 40,
        f["settled_inband"] * 40,
        math.log10(max(f["centroid_fall"], 0.05)) * 20,
        math.log10(max(f["sustain_ratio"], 1e-4)) * 10,
        math.log10(max(f["ac_over_dc"], 1e-3)) * 20,
    ])

def build_reference(binary, pitches, verbose=True):
    grid = list(itertools.product(
        sorted(SHAPES), [6000, 12000, 18000, 24000, 30000],
        [5, 15, 25, 35], [0, 26000], [0, 127]))
    lib = []
    for i, (shape, timbre, attack, sustain, exciter) in enumerate(grid):
        for pitch in pitches:
            x = render(binary, voices=1, alloc=1, switch=0, to=shape, blocks=600,
                       decay=15, attack=attack, sustain=sustain, exciter=exciter,
                       pitch=pitch, timbre=timbre, te=0)
            f = features(x)
            if f:
                lib.append((f, dict(shape=SHAPES[shape], timbre=timbre, attack=attack,
                                    sustain=sustain, exciter=exciter, pitch=pitch)))
        if verbose and i % 40 == 0:
            print(f"  reference {i}/{len(grid)} ({len(lib)} usable)", file=sys.stderr)
    return lib

if __name__ == "__main__":
    # The register matters more than anything else here: the phenomenon is
    # 7 dB louder and 4x longer at MIDI 48 than at 84, and a library that
    # samples only the top of the keyboard calls it a duplicate.
    pitches = [45, 57, 69, 81]   # A2 A3 A4 A5
    print("building the reference library from the SHIPPED shapes...", file=sys.stderr)
    lib = build_reference("paratest_stock", pitches)
    V = np.array([vec(f) for f, _ in lib])
    mu, sd = V.mean(axis=0), V.std(axis=0)
    sd[sd < 1e-6] = 1.0
    Z = (V - mu) / sd
    print(f"reference library: {len(lib)} renders from {len(SHAPES)} shipped shapes\n")
    # how far apart are the references themselves? that sets the scale of "same"
    from scipy.spatial.distance import cdist
    D = cdist(Z, Z)
    np.fill_diagonal(D, np.inf)
    nn_own = D.min(axis=1)
    print("SCALE. nearest-neighbour distance WITHIN the reference set:")
    for q in (50, 90, 99):
        print(f"  {q}th percentile {np.percentile(nn_own, q):6.2f}")
    print(f"  max              {nn_own.max():6.2f}")
    np.save("/Users/rcrogers/.claude/jobs/064553a4/tmp/ref_Z.npy", Z)
    np.save("/Users/rcrogers/.claude/jobs/064553a4/tmp/ref_mu.npy", mu)
    np.save("/Users/rcrogers/.claude/jobs/064553a4/tmp/ref_sd.npy", sd)
    import json
    json.dump([m for _, m in lib], open("/Users/rcrogers/.claude/jobs/064553a4/tmp/ref_meta.json", "w"))
