"""Renders tools/resonator_space/flow and measures what came out.

render(params) -> samples; features(x, midi, key_up) -> dict. Every figure is
read from the samples themselves.
"""
import os
import subprocess
import tempfile

import numpy as np
from scipy.signal import welch

FS = 45000
FLOW = os.environ.get("FLOW", os.path.join(os.path.dirname(__file__), "flow"))


def render(params):
    with tempfile.NamedTemporaryFile(suffix=".f32", delete=False) as f:
        path = f.name
    args = [FLOW] + ["%s=%s" % kv for kv in params.items()] + ["o=" + path]
    subprocess.run(args, check=True)
    x = np.fromfile(path, dtype=np.float32).astype(float)
    os.unlink(path)
    return x


def db(v):
    return 20 * np.log10(np.maximum(v, 1e-12))


def rms(x):
    return np.sqrt(np.mean(x ** 2))


def envelope_db(x, window):
    n = len(x) // window
    return db(np.sqrt(np.mean(x[: n * window].reshape(n, window) ** 2, axis=1)))


def refine_f0(x, f_guess):
    n = len(x)
    zoom = 16
    spectrum = np.abs(np.fft.rfft(x * np.hanning(n), zoom * n))
    freqs = np.fft.rfftfreq(zoom * n, 1 / FS)
    band = np.where((freqs > f_guess * 0.8) & (freqs < f_guess * 1.25))[0]
    return freqs[band[spectrum[band].argmax()]]


def features(x, midi, key_up, steady=0.3):
    f_note = 440 * 2 ** ((midi - 69) / 12)
    k_up = int(key_up * FS)
    s = x[max(0, k_up - int(steady * FS)):k_up]
    out = {"level": db(rms(s)), "peak": float(np.abs(x).max())}
    if out["level"] < -100:
        out["regime"] = "silent"
        return out
    f0 = refine_f0(s, f_note)
    out["cents"] = 1200 * np.log2(f0 / f_note)
    n = len(s)
    power = np.abs(np.fft.rfft(s * np.hanning(n))) ** 2
    freqs = np.fft.rfftfreq(n, 1 / FS)
    bin_hz = FS / n
    width = max(3 * bin_hz, 0.02 * f0)
    harmonics = []
    harmonic_mask = np.zeros_like(freqs, dtype=bool)
    for k in range(1, int(FS / 2 / f0)):
        m = np.abs(freqs - k * f0) < width
        harmonic_mask |= m
        if k <= 12:
            harmonics.append(power[m].sum())
    harmonics = np.array(harmonics)
    total = power[freqs > 20].sum()
    harmonic_total = power[harmonic_mask].sum()
    out["hnr"] = 10 * np.log10(harmonic_total / max(total - harmonic_total, 1e-30))
    out["H"] = list(10 * np.log10(harmonics / max(harmonics[0], 1e-30) + 1e-30))
    out["centroid"] = float((np.arange(1, 13) * harmonics).sum() / harmonics.sum())
    out["even_odd"] = 10 * np.log10(harmonics[1::2].sum() / max(harmonics[2::2].sum(), 1e-30))
    out["h1_share"] = float(harmonics[0] / harmonics.sum())
    # Strongest partial away from every harmonic, against the strongest harmonic.
    outside = power.copy()
    outside[harmonic_mask | (freqs < 20)] = 0
    out["inharmonic"] = 10 * np.log10(outside.max() / max(harmonics.max(), 1e-30))
    # The spectral peak's -3 dB width (Welch-averaged): a ring's bandwidth.
    wf, wp = welch(s, FS, nperseg=min(len(s), 8192))
    i = np.argmax(wp * (np.abs(wf - f0) < f0 * 0.2))
    half = wp[i] / 2
    lo = i
    while lo > 0 and wp[lo] > half:
        lo -= 1
    hi = i
    while hi < len(wp) - 1 and wp[hi] > half:
        hi += 1
    out["bandwidth_hz"] = float(wf[hi] - wf[lo])
    w10 = int(0.01 * FS)
    env = envelope_db(x[:k_up], w10)
    target = out["level"] - 3
    above = np.where(env > target)[0]
    out["onset_ms"] = float(above[0] * 10) if len(above) else None
    out["level_wander"] = float(np.std(envelope_db(s, w10)))
    w50 = int(0.05 * FS)
    segments = [s[i:i + w50] for i in range(0, len(s) - w50 + 1, w50)]
    out["pitch_wander"] = float(np.std([1200 * np.log2(refine_f0(g, f0) / f0) for g in segments]))
    tail = envelope_db(x[k_up:], w10)
    fell = np.where(tail < out["level"] - 40)[0]
    out["release_ms"] = float(fell[0] * 10) if len(fell) else None
    out["regime"] = ("tone" if out["hnr"] > 20 else "noise" if out["hnr"] < 6 else "mixed")
    return out
