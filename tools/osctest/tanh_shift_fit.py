#!/usr/bin/env python3
# The tanh variants' top-gain shifts (kTanhDoublings_q12): for one carrier,
# the doublings at which a biased variant's DIN 45692 sharpness best tracks the
# odd variant's over the knob.
#
# Renders tanh(g (carrier + offset)) itself, g = 0.5 * 2^((5 + shift) t), at
# MIDI 36, 48, 60 and 72, each normalised to 70 dB SPL with 50 ms fades, and
# scores the RMS of (variant - odd) over t in percent of odd's range. Against
# the firmware's own render this model reads within 0.008 acum.
#
#   python3 tools/osctest/tanh_shift_fit.py CARRIER SHIFT,SHIFT,... [VARIANT]
#   CARRIER is sine, tri or expo. Needs numpy, scipy and mosqito.
import sys
from multiprocessing import Pool
import numpy as np
from scipy.signal import resample_poly
from mosqito.sq_metrics import loudness_zwst
from mosqito.sq_metrics.sharpness.sharpness_din.sharpness_din_from_loudness import (
    sharpness_din_from_loudness)

SAMPLE_RATE = 45000
MODEL_RATE = 48000
SECONDS = 1.0
FADE_SECONDS = 0.05
SPL = 70
P_REF = 20e-6
PITCHES = (36, 48, 60, 72)
KNOB = np.arange(0, 101, 5) / 100
KNOB_DOUBLINGS = 5
GAIN_LEAST = 0.5
OFFSETS = {"odd": 0.0, "balanced": 0.371, "even": 2 / 3}   # kTanhCarrierOffset


def carrier(name, phase):
    quarter = (phase * 4) % 1
    rising = (phase % 0.5) < 0.25
    x = np.where(rising, quarter, 1 - quarter)
    shape = {"sine": np.sin(x * np.pi / 2), "tri": x, "expo": 1 - (1 - x) ** 3}[name]
    return np.where(phase < 0.5, shape, -shape)


def sharpness(name, offset, shift, pitch, knob):
    n = int(SECONDS * SAMPLE_RATE)
    phase = (np.arange(n) * 440 * 2 ** ((pitch - 69) / 12) / SAMPLE_RATE) % 1
    y = np.tanh(GAIN_LEAST * 2 ** ((KNOB_DOUBLINGS + shift) * knob)
                * (carrier(name, phase) + offset))
    y -= y.mean()
    pa = y / np.sqrt(np.mean(y ** 2)) * P_REF * 10 ** (SPL / 20)
    fade = int(FADE_SECONDS * SAMPLE_RATE)
    ramp = 0.5 - 0.5 * np.cos(np.pi * np.arange(fade) / fade)
    pa[:fade] *= ramp
    pa[-fade:] *= ramp[::-1]
    loudness, specific, _ = loudness_zwst(resample_poly(pa, MODEL_RATE // 3000, SAMPLE_RATE // 3000), MODEL_RATE)
    return float(sharpness_din_from_loudness(loudness, specific))


def curve(args):
    return [sharpness(*args, k) for k in KNOB]


def curves(pool, name, offset, shift):
    return np.array(pool.map(curve, [(name, offset, shift, p) for p in PITCHES]))


if __name__ == "__main__":
    name, shifts = sys.argv[1], [float(s) for s in sys.argv[2].split(",")]
    variants = sys.argv[3:] or ["balanced", "even"]
    with Pool() as pool:
        odd = curves(pool, name, 0.0, 0.0)
        for variant in variants:
            for shift in shifts:
                gap = (curves(pool, name, OFFSETS[variant], shift) - odd) \
                    / np.ptp(odd, axis=1, keepdims=True) * 100
                rms = np.mean(np.sqrt(np.mean(gap ** 2, axis=1)))
                upper = np.mean(gap[:, KNOB >= 0.5])
                print(f"{name} {variant:8s} shift {shift:+.2f}: RMS {rms:5.2f}%  "
                      f"upper half {upper:+6.2f}%", flush=True)
