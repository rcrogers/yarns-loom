"""A note's timbre frame by frame: what changes within it as well as where it
settles. Each 20 ms frame: level, periodicity at the note, harmonic centroid,
even/odd balance, pitch."""
import numpy as np

FS = 45000
FRAME = 900
ZOOM = 8192


def frame_features(s, f0):
    level = 20 * np.log10(np.sqrt(np.mean(s ** 2)) + 1e-12)
    if level < -100:
        return dict(level=level)
    # Periodicity: the best normalised correlation one period on, over +/-5 %.
    period = FS / f0
    best, best_lag = -1.0, period
    for lag in range(int(period * 0.95), int(period * 1.05) + 2):
        a, b = s[:-lag], s[lag:]
        c = np.dot(a, b) / np.sqrt(np.dot(a, a) * np.dot(b, b) + 1e-30)
        if c > best:
            best, best_lag = c, lag
    p = np.abs(np.fft.rfft(s * np.hanning(len(s)), ZOOM)) ** 2
    f = np.fft.rfftfreq(ZOOM, 1 / FS)
    band = (f > f0 * 0.9) & (f < f0 * 1.1)
    f_est = f[band][np.argmax(p[band])]
    harmonics = []
    for k in range(1, 13):
        if k * f_est > FS / 2 - f0: break
        m = np.abs(f - k * f_est) < 0.5 * f0
        harmonics.append(p[m].max())
    h = np.array(harmonics)
    return dict(level=level, periodicity=float(best), cents=1200 * np.log2(f_est / f0),
                centroid=float((np.arange(1, len(h) + 1) * h).sum() / h.sum()),
                even_odd=10 * np.log10(h[1::2].sum() / max(h[2::2].sum(), 1e-30)))


def note_frames(x, f0, start=0, stop=None):
    stop = len(x) if stop is None else stop
    return [frame_features(x[i:i + FRAME], f0) for i in range(start, stop - FRAME + 1, FRAME)]
