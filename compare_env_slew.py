#!/usr/bin/env python3
"""Simulate the dithered-slew envelope core (yarns/envelope.cc) and compare it
against an ideal float slew to quantify dither artifacts.

Renders per scenario, sharing stage timing and targets:
  - sigma-delta: exact integer replica of the firmware math (Q5.27 shift,
    fraction accumulated per sample, carry selects shift+1)
  - random dither: the earlier prototype (10-bit Bernoulli dither), kept for
    comparison of noise character
  - undithered: same integer slew, but always the truncated base shift
    (shows the time-constant error dither exists to fix)
  - ideal: float slew using the dither's expected per-sample coefficient
    (the noise-free trajectory the dithered versions should follow)

Error traces are in 15-bit output LSBs (firmware output = value_q30 >> 15).
"""

import numpy as np
import matplotlib.pyplot as plt

FS = 45000  # ~ audio rate
UINT32_MAX = 0xFFFFFFFF
Q27 = 1 << 27
K_TIME_CONSTANTS_LOG2_Q5_27 = 2 << 27  # 4 time constants per stage
K_MAX_SLEW_SHIFT_Q5_27 = 27 << 27


def clz32(x):
    assert 0 < x <= UINT32_MAX
    return 32 - x.bit_length()


def slew_shift_q5_27(increment):
    """Exact replica of Envelope::Trigger's shift derivation."""
    z = clz32(increment)
    if z >= 30:
        return K_MAX_SLEW_SHIFT_Q5_27
    mantissa_frac = ((increment << z) & 0x7FFFFFFF) >> 4
    log2_n = ((z + 1) << 27) - mantissa_frac
    if log2_n <= K_TIME_CONSTANTS_LOG2_Q5_27:
        return 0
    return min(log2_n - K_TIME_CONSTANTS_LOG2_Q5_27, K_MAX_SLEW_SHIFT_Q5_27)


def xorshift32_stream(n, seed=0xCAFEBABE):
    out = np.empty(n, dtype=np.uint32)
    state = seed
    for i in range(n):
        state ^= (state << 13) & UINT32_MAX
        state ^= state >> 17
        state ^= (state << 5) & UINT32_MAX
        out[i] = state
    return out


def asr(x, s):
    """C arithmetic shift right (Python's >> on ints already floors)."""
    return x >> s


class Scenario(object):
    def __init__(self, name, attack_s, decay_s, release_s,
                 sustain_frac, gate_s, total_s):
        self.name = name
        self.attack_n = int(attack_s * FS)
        self.decay_n = int(decay_s * FS)
        self.release_n = int(release_s * FS)
        self.sustain_frac = sustain_frac
        self.gate_n = int(gate_s * FS)
        self.total_n = int(total_s * FS)

    def stage_plan(self):
        """[(target_q30, countdown or None)] resolved against the gate."""
        peak = (1 << 30) - (1 << 15)
        sustain = int(peak * self.sustain_frac)
        floor = 0
        return [
            ('A', peak, self.attack_n),
            ('D', sustain, self.decay_n),
            ('S', sustain, None),       # holds until gate off
            ('R', floor, self.release_n),
            ('X', floor, None),         # dead: holds forever
        ]


def render(scenario, mode, prng, prng_xor=0x20001000):
    """mode: 'sd' | 'dither' | 'trunc' | 'ideal'. Returns value trace in Q30
    (float for 'ideal', int for the others)."""
    dither_phase = prng_xor & 0xFFFFFFFF  # firmware seeds from prng_xor
    plan = scenario.stage_plan()
    n_total = scenario.total_n
    trace = np.empty(n_total, dtype=np.float64)

    stage_index = 0
    name, target, countdown = plan[0]
    value = 0  # int for integer modes; float for ideal
    if mode == 'ideal':
        value = 0.0

    def stage_params(countdown):
        if countdown is None:
            return None  # hold: inherit previous
        increment = UINT32_MAX // max(countdown, 1)
        samples_left = UINT32_MAX // increment  # firmware's actual countdown
        q = slew_shift_q5_27(increment)
        return samples_left, q

    params = stage_params(countdown)
    samples_left, q = params
    base_shift = q >> 27
    thresh = (q >> 17) & 0x3FF
    coeff = (1.0 - thresh / 1024.0) * 2.0 ** -base_shift \
        + (thresh / 1024.0) * 2.0 ** -(base_shift + 1)

    gate_off_pending = True
    for i in range(n_total):
        # Gate off: firmware NoteOff -> Trigger(RELEASE)
        if gate_off_pending and i >= scenario.gate_n:
            gate_off_pending = False
            stage_index = 3
            name, target, countdown = plan[stage_index]
            p = stage_params(countdown)
            if p is not None:
                samples_left, q = p
                base_shift = q >> 27
                thresh = (q >> 17) & 0x3FF
                coeff = (1.0 - thresh / 1024.0) * 2.0 ** -base_shift \
                    + (thresh / 1024.0) * 2.0 ** -(base_shift + 1)

        # Timed-stage countdown handoff (end wherever the slew got to)
        while countdown is not None and samples_left == 0:
            stage_index += 1
            name, target, countdown = plan[stage_index]
            p = stage_params(countdown)
            if p is not None:
                samples_left, q = p
                base_shift = q >> 27
                thresh = (q >> 17) & 0x3FF
                coeff = (1.0 - thresh / 1024.0) * 2.0 ** -base_shift \
                    + (thresh / 1024.0) * 2.0 ** -(base_shift + 1)

        if mode == 'ideal':
            value += (target - value) * coeff
        else:
            if mode == 'sd':
                frac = (q << 5) & 0xFFFFFFFF
                dither_phase = (dither_phase + frac) & 0xFFFFFFFF
                shift = base_shift + (1 if dither_phase < frac else 0)
            elif mode == 'dither':
                r = int(prng[i]) ^ prng_xor
                shift = base_shift + (1 if (r >> 22) < thresh else 0)
            else:
                shift = base_shift
            value += asr(target - value, shift)

        if countdown is not None:
            samples_left -= 1
        trace[i] = value
    return trace


K_CHIFF_FASTEST_SHIFT_Q5_27 = 2 << 27


def render_chiff(scenario, chiff_amount, prng, prng_xor=0x20001000):
    """Integer replica of RenderStage<CHIFF=true> over the scenario."""
    plan = scenario.stage_plan()
    trace = np.empty(scenario.total_n, dtype=np.float64)

    peak = plan[0][1]
    floor = 0
    span_shifted = (peak - floor) >> 10
    gate = chiff_amount << 3

    stage_index = 0
    name, target, countdown = plan[0]
    increment = UINT32_MAX // max(countdown, 1)
    samples_left = UINT32_MAX // increment
    nominal = slew_shift_q5_27(increment)
    dither_phase = prng_xor & 0xFFFFFFFF

    # Arm chiff against the attack's nominal shift
    full_drop = nominal - K_CHIFF_FASTEST_SHIFT_Q5_27 \
        if nominal > K_CHIFF_FASTEST_SHIFT_Q5_27 else 0
    deficit = (full_drop >> 7) * chiff_amount
    attack_samples = UINT32_MAX // increment
    decrement = max(deficit // attack_samples, 1) if deficit else 0

    value = 0
    gate_off_pending = True
    for i in range(scenario.total_n):
        if gate_off_pending and i >= scenario.gate_n:
            gate_off_pending = False
            stage_index = 3
            name, target, countdown = plan[stage_index]
            increment = UINT32_MAX // max(countdown, 1)
            samples_left = UINT32_MAX // increment
            nominal = slew_shift_q5_27(increment)
        while countdown is not None and samples_left == 0:
            stage_index += 1
            name, target, countdown = plan[stage_index]
            if countdown is not None:
                increment = UINT32_MAX // max(countdown, 1)
                samples_left = UINT32_MAX // increment
                nominal = slew_shift_q5_27(increment)

        random = int(prng[i]) ^ prng_xor
        deficit = deficit - decrement if deficit > decrement else 0
        shift_q = nominal - deficit if nominal > deficit else 0
        frac = (shift_q << 5) & 0xFFFFFFFF
        dither_phase = (dither_phase + frac) & 0xFFFFFFFF
        shift = (shift_q >> 27) + (1 if dither_phase < frac else 0)
        sample_target = target
        if deficit and ((random >> 12) & 0x3FF) < gate:
            sample_target = floor + span_shifted * (random & 0x3FF)
        value += asr(sample_target - value, shift)
        if countdown is not None:
            samples_left -= 1
        trace[i] = value
    return trace


def chiff_figure(scenarios):
    C_TEXT = '#0b0b0b'
    C_TEXT2 = '#52514e'
    C_GRID = '#e5e4e0'
    # Sequential blue ramp (dataviz reference): one hue, chiff amount = depth
    AMOUNT_COLORS = [('#86b6ef', 32), ('#2a78d6', 96), ('#0d366b', 127)]

    sc = scenarios[0]  # fast ADSR is the interesting chiff case
    prng = xorshift32_stream(sc.total_n)
    t = np.arange(sc.total_n) / FS
    lsb = 1 << 15

    fig, axes = plt.subplots(2, 1, figsize=(13, 7), facecolor='#fcfcfb')
    fig.suptitle('Chiff: random slew targets under a ramping shift deficit (%s)'
                 % sc.name, color=C_TEXT, fontsize=12)
    zoom_n = int(0.100 * FS)
    for ax, n in zip(axes, (sc.total_n, zoom_n)):
        base = render(sc, 'dither', prng)
        ax.plot(t[:n], base[:n] / lsb, color='#eda100', lw=2.0,
                label='chiff 0')
        for color, amount in AMOUNT_COLORS:
            tr = render_chiff(sc, amount, prng)
            ax.plot(t[:n], tr[:n] / lsb, color=color, lw=0.9,
                    label='chiff %d' % amount)
        ax.set_ylabel('output (15-bit LSB)', color=C_TEXT2, fontsize=9)
        ax.legend(frameon=False, fontsize=8)
        ax.set_facecolor('#fcfcfb')
        ax.grid(True, color=C_GRID, lw=0.6)
        ax.tick_params(colors=C_TEXT2, labelsize=8)
        for s in ax.spines.values():
            s.set_color(C_GRID)
    axes[0].set_title('full envelope', color=C_TEXT, fontsize=10)
    axes[1].set_title('attack zoom (first 100 ms)', color=C_TEXT, fontsize=10)
    axes[1].set_xlabel('time (s)', color=C_TEXT2, fontsize=9)
    fig.tight_layout(rect=[0, 0, 1, 0.95])
    fig.savefig('env_chiff_sim.png', dpi=130)
    print('wrote env_chiff_sim.png')


def main():
    scenarios = [
        Scenario('fast (A 20ms, D 250ms, R 600ms)',
                 0.020, 0.250, 0.600, 0.5, gate_s=0.5, total_s=1.4),
        Scenario('slow (A 1s, D 2s, R 3s)',
                 1.0, 2.0, 3.0, 0.6, gate_s=4.0, total_s=8.0),
    ]

    # Palette (dataviz reference, light mode)
    C_DITHER = '#2a78d6'   # blue
    C_IDEAL = '#eda100'    # yellow
    C_TRUNC = '#e34948'    # red
    C_TEXT = '#0b0b0b'
    C_TEXT2 = '#52514e'
    C_GRID = '#e5e4e0'

    fig, axes = plt.subplots(3, 2, figsize=(13, 10), facecolor='#fcfcfb')
    fig.suptitle('Dithered-slew envelope vs ideal float slew',
                 color=C_TEXT, fontsize=13)

    for col, sc in enumerate(scenarios):
        prng = xorshift32_stream(sc.total_n)
        sd = render(sc, 'sd', prng)
        dith = render(sc, 'dither', prng)
        trunc = render(sc, 'trunc', prng)
        ideal = render(sc, 'ideal', prng)
        t = np.arange(sc.total_n) / FS
        lsb = 1 << 15  # one 15-bit output LSB in Q30

        ax = axes[0][col]
        ax.plot(t, ideal / lsb, color=C_IDEAL, lw=2.4, label='ideal (float)')
        ax.plot(t, sd / lsb, color=C_DITHER, lw=1.0, label='sigma-delta slew')
        ax.plot(t, trunc / lsb, color=C_TRUNC, lw=1.0, ls='--',
                label='undithered (trunc shift)')
        ax.set_title(sc.name, color=C_TEXT, fontsize=10)
        ax.set_ylabel('output (15-bit LSB)', color=C_TEXT2, fontsize=9)
        ax.legend(frameon=False, fontsize=8)

        ax = axes[1][col]
        ax.plot(t, (dith - ideal) / lsb, color=C_TRUNC, lw=0.8,
                label='random dither − ideal')
        ax.plot(t, (sd - ideal) / lsb, color=C_DITHER, lw=0.8,
                label='sigma-delta − ideal')
        ax.set_ylabel('error (LSB)', color=C_TEXT2, fontsize=9)
        ax.legend(frameon=False, fontsize=8)

        # Ripple/noise = deviation from local mean (the ideal trace already
        # isolates trajectory error; high-pass what's left)
        kernel = np.ones(64) / 64.0
        err_sd = (sd - ideal) / lsb
        err_dith = (dith - ideal) / lsb
        hp = err_sd - np.convolve(err_sd, kernel, mode='same')
        hp_dith = err_dith - np.convolve(err_dith, kernel, mode='same')
        ax = axes[2][col]
        ax.plot(t, hp_dith, color=C_TRUNC, lw=0.5, label='random dither')
        ax.plot(t, hp, color=C_DITHER, lw=0.6, label='sigma-delta')
        ax.set_ylabel('HP ripple/noise (LSB)', color=C_TEXT2, fontsize=9)
        ax.set_xlabel('time (s)', color=C_TEXT2, fontsize=9)
        ax.legend(frameon=False, fontsize=8)

        for row in range(3):
            a = axes[row][col]
            a.set_facecolor('#fcfcfb')
            a.grid(True, color=C_GRID, lw=0.6)
            a.tick_params(colors=C_TEXT2, labelsize=8)
            for s in a.spines.values():
                s.set_color(C_GRID)

        # Console stats
        def db(x):
            return 20 * np.log10(max(x, 1e-12) / 32768.0)

        def stat(label, x):
            print('  %-26s: max %8.2f LSB (%6.1f dBFS)   rms %7.3f LSB (%6.1f dBFS)'
                  % (label, np.max(np.abs(x)), db(np.max(np.abs(x))),
                     np.sqrt(np.mean(x ** 2)), db(np.sqrt(np.mean(x ** 2)))))
        print('== %s ==' % sc.name)
        stat('sigma-delta - ideal', err_sd)
        stat('random dither - ideal', err_dith)
        stat('HP ripple (sigma-delta)', hp)
        stat('HP noise (random dither)', hp_dith)
        stat('undithered - ideal', (trunc - ideal) / lsb)

    fig.tight_layout(rect=[0, 0, 1, 0.97])
    fig.savefig('env_slew_sim.png', dpi=130)
    print('wrote env_slew_sim.png')

    chiff_figure(scenarios)


if __name__ == '__main__':
    main()
