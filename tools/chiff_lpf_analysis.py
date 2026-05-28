#!/usr/bin/env python3
"""
Stat analysis for chiff LPF cutoff dithering via multiset selection.

Given N slots randomly picked from a pool of K shifts (each shift s ∈ [0, K-1]
yielding 1-pole alpha = 1/2^s), the LPF cutoff per-block is determined by the
mean alpha across slots: alpha_mean = mean(1/2^s_i).

Goal: count how many distinct alpha_mean values are reachable for given (N, K),
and how well they cover a target cutoff range (i.e., resolution in
cents/octave terms).

Usage:
  python3 chiff_lpf_analysis.py
"""

import itertools
import math
from fractions import Fraction


def count_distinct_alphas(N, K):
    """Enumerate all multisets of size N from shifts {0..K-1}, return distinct
    alpha_mean values as a sorted list of Fractions."""
    alphas = set()
    for shifts in itertools.combinations_with_replacement(range(K), N):
        total = sum(Fraction(1, 2 ** s) for s in shifts)
        alphas.add(total / N)
    return sorted(alphas)


def alpha_to_cutoff_hz(alpha, sample_rate=45000):
    """1-pole LPF: f_c ≈ alpha * SR / (2*pi)."""
    return float(alpha) * sample_rate / (2 * math.pi)


def cents_resolution(alphas):
    """For a sorted list of distinct alphas, compute the gap (in cents) between
    adjacent alphas. Returns (min_gap, max_gap, mean_gap) in cents."""
    gaps = []
    for i in range(1, len(alphas)):
        ratio = float(alphas[i]) / float(alphas[i - 1])
        cents = 1200 * math.log2(ratio)
        gaps.append(cents)
    return (min(gaps), max(gaps), sum(gaps) / len(gaps)) if gaps else (0, 0, 0)


def analyze(N, K, sample_rate=45000):
    alphas = count_distinct_alphas(N, K)
    n_distinct = len(alphas)
    min_alpha = alphas[0]
    max_alpha = alphas[-1]
    min_fc = alpha_to_cutoff_hz(min_alpha, sample_rate)
    max_fc = alpha_to_cutoff_hz(max_alpha, sample_rate)
    range_octaves = math.log2(float(max_alpha) / float(min_alpha))
    min_gap, max_gap, mean_gap = cents_resolution(alphas)
    bits_per_shift = max(1, math.ceil(math.log2(K)))
    bits_per_bin = N * bits_per_shift
    bytes_per_bin_packed = math.ceil(bits_per_bin / 8)
    bytes_per_bin_8bit = N
    print(f"N={N}, K={K}:")
    print(f"  distinct alphas:  {n_distinct}")
    print(f"  alpha range:      {float(min_alpha):.6f} .. {float(max_alpha):.4f}")
    print(f"  cutoff range Hz:  {min_fc:.2f} .. {max_fc:.2f}")
    print(f"  range (octaves):  {range_octaves:.2f}")
    print(f"  gap min/mean/max: {min_gap:.1f} / {mean_gap:.1f} / {max_gap:.1f} cents")
    print(f"  storage/bin:      {bytes_per_bin_packed}B packed ({bits_per_shift}b/shift) "
          f"| {bytes_per_bin_8bit}B unpacked")
    print()


def coverage_analysis(N, K, num_targets, sample_rate=45000):
    """For each of `num_targets` musically-spaced target cutoffs (log-spaced
    over MIDI A0..C8, i.e., 27.5 Hz .. 4186 Hz), find the closest reachable
    alpha via multiset enumeration and report the worst-case error in cents."""
    alphas = count_distinct_alphas(N, K)
    # Target cutoffs = pitch frequency for MIDI 21..108 (A0..C8) log-spaced
    f_lo, f_hi = 27.5, 4186.0
    target_alphas = []
    for i in range(num_targets):
        t = i / (num_targets - 1)
        f_c = f_lo * (f_hi / f_lo) ** t
        target_alphas.append(2 * math.pi * f_c / sample_rate)
    # Greedy closest-match (alphas are sorted)
    import bisect
    alpha_floats = [float(a) for a in alphas]
    errors_cents = []
    for tgt in target_alphas:
        idx = bisect.bisect_left(alpha_floats, tgt)
        candidates = [alpha_floats[i] for i in (idx - 1, idx) if 0 <= i < len(alpha_floats)]
        best = min(candidates, key=lambda a: abs(math.log2(a / tgt)))
        errors_cents.append(abs(1200 * math.log2(best / tgt)))
    return max(errors_cents), sum(errors_cents) / len(errors_cents)


if __name__ == "__main__":
    print("Chiff LPF dither — multiset enumeration analysis")
    print("================================================\n")
    for (N, K) in [(4, 8), (4, 12), (8, 8), (8, 12)]:
        analyze(N, K)

    print("Coverage error vs target pitch-tracked cutoffs (A0..C8 = 88 keys):")
    print(f"{'config':<14} {'128 tgt max/avg':>18} {'256 tgt max/avg':>18} {'512 tgt max/avg':>18}")
    for (N, K) in [(4, 8), (4, 12), (8, 8), (8, 12)]:
        bits_per_shift = max(1, math.ceil(math.log2(K)))
        bytes_per_bin = math.ceil(N * bits_per_shift / 8)
        parts = [f"N={N},K={K:<5} ({bytes_per_bin}B/bin)"]
        for num_bins in (128, 256, 512):
            mx, avg = coverage_analysis(N, K, num_bins)
            parts.append(f"{mx:5.1f}¢/{avg:5.1f}¢")
        print(f"{parts[0]:<22} " + "  ".join(p.rjust(16) for p in parts[1:]))
