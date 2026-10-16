"""Measurements for outputs that stay noisy: long, averaged, never one window.

A narrowband noise's level and pitch move on a timescale of 1 / bandwidth, so
every figure here is taken over seconds.
"""
import numpy as np
from scipy.signal import butter, hilbert, sosfiltfilt, welch

FS = 45000


def band_power(f, p, centre, half_width):
    return p[np.abs(f - centre) < half_width].sum()


def noise_features(x, f0, settle=1.0):
    s = x[int(settle * FS):]
    r = np.sqrt(np.mean(s ** 2))
    f, p = welch(s, FS, nperseg=1 << 16)
    hw = max(0.05 * f0, 3)
    fundamental = band_power(f, p, f0, hw)
    out = {
        "level": 20 * np.log10(r),
        "crest": np.percentile(np.abs(s), 99.9) / r,
        "band2": 10 * np.log10(band_power(f, p, 2 * f0, hw) / fundamental + 1e-30),
        "band3": 10 * np.log10(band_power(f, p, 3 * f0, hw) / fundamental + 1e-30),
        "band5": 10 * np.log10(band_power(f, p, 5 * f0, hw) / fundamental + 1e-30),
    }
    # The fundamental alone: its amplitude and frequency, averaged over 8
    # periods (fast) and over 200 ms (slow drift). A narrowband noise's
    # frequency jumps where its envelope nulls, so the spread is the IQR's
    # and the jumps are counted apart, as slips: windows more than 50 c off.
    sos = butter(2, [f0 / 1.3, f0 * 1.3], btype="band", fs=FS, output="sos")
    a = hilbert(sosfiltfilt(sos, s))
    phase = np.unwrap(np.angle(a))
    for name, w in (("fast", int(8 * FS / f0)), ("slow", int(0.2 * FS))):
        n = len(a) // w
        amp = np.abs(a[: n * w]).reshape(n, w).mean(axis=1)
        out["amp_wander_%s_db" % name] = float(np.std(20 * np.log10(amp + 1e-12)))
        freq = (phase[w:n * w:w] - phase[:n * w - w:w]) / w * FS / (2 * np.pi)
        cents = 1200 * np.log2(np.maximum(freq, 1) / f0)
        q1, q3 = np.percentile(cents, [25, 75])
        out["pitch_wander_%s_c" % name] = float((q3 - q1) / 1.349)
        out["slips_%s_per_s" % name] = float(np.sum(np.abs(cents - np.median(cents)) > 50) / (len(s) / FS))
    # Settled: the level of the last half against the first, after settle.
    half = len(s) // 2
    out["drift_db"] = float(10 * np.log10(np.mean(s[half:] ** 2) / np.mean(s[:half] ** 2)))
    return out
