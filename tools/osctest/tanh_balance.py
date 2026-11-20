#!/usr/bin/env python3
# Where the tanh variants rest: the carrier offsets kTanhCarrierOffset holds,
# for the gain law RenderTransferTanh and WarpTimbre apply.
#
# A variant with offset c spans 5 + s doublings from gain 0.5, with
# s = -log2(1 - c^2) / 2 (kTanhGainShift_q12). Its even share is the even
# harmonics' fraction of the harmonic power, h1..h199, averaged over a uniform
# TIMBRE sweep. Balanced is the offset whose share is halfway between odd's
# (c = 0) and even's (c = 2/3).
#
# Also prints where the even harmonics' power peaks against c at each gain, the
# claim that sets even's 2/3.
#
#   python3 tools/osctest/tanh_balance.py      (needs numpy)
import numpy as np

N = 4096
THETA = 2 * np.pi * np.arange(N) / N
SWEEP = np.linspace(0, 1, 33)
GAIN_LEAST = 0.5
KNOB_DOUBLINGS = 5
EVEN_OFFSET = 2 / 3


def shift(offset):
    return -0.5 * np.log2(1 - offset * offset)


def harmonic_power(offset, gain):
    y = np.tanh(gain * (np.sin(THETA) + offset))
    return np.abs(np.fft.rfft(y - y.mean()) / N * 2)[1:200] ** 2


def sweep_even_share(offset):
    gains = GAIN_LEAST * 2 ** ((KNOB_DOUBLINGS + shift(offset)) * SWEEP)
    return np.mean([h[1::2].sum() / h.sum()
                    for h in (harmonic_power(offset, g) for g in gains)])


odd, even = sweep_even_share(0.0), sweep_even_share(EVEN_OFFSET)
target = (odd + even) / 2
grid = np.arange(0.30, 0.42, 0.001)
balanced = grid[np.argmin(np.abs([sweep_even_share(c) - target for c in grid]))]
print(f"even share over the sweep: odd {odd:.3f}, even {even:.3f}; "
      f"balanced at offset {balanced:.3f} ({target:.3f})")

offsets = np.arange(0.4, 0.95, 0.002)
top_gain = GAIN_LEAST * 2 ** (KNOB_DOUBLINGS + shift(EVEN_OFFSET))
print("gain   even power peaks at offset   2/3 against the peak")
for gain in (1, 2, 4, 8, 16, top_gain):
    power = np.array([harmonic_power(c, gain)[1::2].sum() for c in offsets])
    at_even = harmonic_power(EVEN_OFFSET, gain)[1::2].sum()
    print(f"{gain:5.1f}  {offsets[power.argmax()]:.3f}"
          f"                        {10 * np.log10(at_even / power.max()):+.2f} dB")
