// AUDIO-RATE PULSE WIDTH: what a render costs against the waveform it is
// approximating, when the thing modulating it runs at audio rate.
//
// WHY THE OTHER METRICS CANNOT ANSWER THIS. spectrum.js, beat.js, rumble.js
// and inharmonic.js all score a render against a HARMONIC GRID at f0, because
// every shape in oscillator.cc is periodic at f0 and anything off that grid is
// therefore artifact. A modulator at an audio rate breaks that premise: its
// sidebands sit at |k*f_c +- m*f_m| and are the SOUND, not the defect. Scoring
// them as artifact ranks a correct render below a broken one.
//
// So the reference here is the waveform itself. It is piecewise constant, so
// its Fourier coefficients follow in closed form from its edge times, and
// those are solved to double precision -- no oversampling, so the reference
// contributes no floor of its own. Everything is exactly periodic in N
// samples, which makes the DFT leakage-free and bin j of a render directly
// comparable with coefficient j of the reference.
//
// A DESIGN STUDY, NOT A GATE. It models the render loop rather than linking
// oscillator.cc, so it can be pointed at a shape that does not exist yet --
// which is the whole reason it was written. golden.js is what pins real code.
//
//   node tools/osctest/pwmlab.js
//
// The rows it compares:
//   latch          a pulse from `phase < width`, edge time against the
//                  CARRIER's increment -- what branch audio-rate-pwm rendered
//   relative rate  the same latch, edge time against the rate at which phase
//                  actually closes on the width
//   two-saw        saw(phase) - saw(phase - width), each saw corrected at its
//                  own wrap, the offset saw's found from its own SIGNED motion
//   oracle         every edge placed at the time the exact solver found. The
//                  best a two-point polyBLEP can do, so a row that reaches it
//                  is limited by the correction and not by its estimator.
'use strict';

const N = 32768;
const FS = 45000;                          // yarns/drivers/dac.h kFrameHz
const INCR_PER_CYCLE = 4294967296 / N;     // exact: N is a power of two
const HALF_TURN = 2147483648;

const U32 = x => x >>> 0;
const toU32Turns = x => U32(Math.round(x * 4294967296) % 4294967296);

// oscillator.h's, transcribed.
function ThisBlepSample(t) { if (t > 65535) t = 65535; return (t * t) >>> 18; }
function NextBlepSample(t) { if (t > 65535) t = 65535; t = 65535 - t; return -((t * t) >>> 18); }
function EdgeTime(past, incr) {
  if (incr >= (1 << 16)) return Math.floor(past / (incr >>> 16));
  if (past >= incr) return 65535;
  return Math.floor((past * 65536) / incr);
}

// The width, one value per sample, shared by every render so that the only
// thing under test is what a render does with it. Indexed n+1 against the
// carrier's phase, which is the pairing a shape actually has: RENDER_MODULATED
// advances the modulator's phase in the same place it advances the carrier's.
function widths(Km, depth_turns) {
  const w = new Uint32Array(N);
  for (let n = 0; n < N; ++n) {
    w[n] = toU32Turns(0.5 + depth_turns * Math.sin(2 * Math.PI * n * Km / N));
  }
  return w;
}
const at = (w, n) => w[(n + 1) % N];

function renderLatch(Kc, w, relative) {
  const incr = U32(Kc * INCR_PER_CYCLE);
  const out = new Float64Array(N);
  let phase = 0, next_sample = 0, high = false, previous_width = w[0];
  for (let n = 0; n < N; ++n) {
    let this_sample = next_sample; next_sample = 0;
    phase = U32(phase + incr);
    const pw = at(w, n);
    const closing = relative ? (incr - ((U32(pw - previous_width)) | 0)) : incr;
    previous_width = pw;
    let self_reset = phase < incr;
    for (;;) {
      if (!high) {
        if (phase < pw) break;
        const t = closing > 0 ? EdgeTime(U32(phase - pw), U32(closing)) : 65535;
        this_sample += ThisBlepSample(t); next_sample += NextBlepSample(t); high = true;
      }
      if (high) {
        if (!self_reset) break;
        self_reset = false;
        const t = EdgeTime(phase, incr);
        this_sample -= ThisBlepSample(t); next_sample -= NextBlepSample(t); high = false;
      }
    }
    next_sample += phase < pw ? 0 : 32767;
    out[n] = (this_sample - 16384) * 2;
  }
  return out;
}

function renderTwoSaw(Kc, w) {
  const incr = U32(Kc * INCR_PER_CYCLE);
  const out = new Float64Array(N);
  let phase = 0, next_sample = 0;
  let previous_offset = U32(0 - w[0]);
  for (let n = 0; n < N; ++n) {
    let this_sample = next_sample; next_sample = 0;
    phase = U32(phase + incr);
    const pw = at(w, n);
    const offset = U32(phase - pw);
    const offset_increment = (U32(offset - previous_offset)) | 0;
    if (phase < incr) {
      const t = EdgeTime(phase, incr);
      this_sample -= ThisBlepSample(t); next_sample -= NextBlepSample(t);
    }
    // Subtracting the offset saw inverts its step, so its residual carries the
    // opposite sign to the carrier's -- and inverts again when it travels
    // backwards, which it does whenever the width outruns the phase.
    if (offset_increment >= 0) {
      if (offset < offset_increment) {
        const t = EdgeTime(offset, U32(offset_increment));
        this_sample += ThisBlepSample(t); next_sample += NextBlepSample(t);
      }
    } else {
      const magnitude = U32(-offset_increment);
      if (previous_offset < magnitude) {
        const t = EdgeTime(U32(-offset), magnitude);
        this_sample -= ThisBlepSample(t); next_sample -= NextBlepSample(t);
      }
    }
    previous_offset = offset;
    next_sample += (phase >>> 17) - (offset >>> 17) - (pw >>> 17) + 32768;
    out[n] = (this_sample - 16384) * 2;
  }
  return out;
}

// Every edge of the true waveform. The count per carrier period is a reading
// in its own right: two is a pulse, more is a width that outran the phase.
function idealEdges(Kc, Km, depth_turns) {
  const value = s => {
    const phase = s * Kc / N;
    const w = 0.5 + depth_turns * Math.sin(2 * Math.PI * s * Km / N);
    return (phase - Math.floor(phase)) >= w ? 1 : -1;
  };
  const steps = N * 64;
  const edges = [];
  let prev = value(0);
  for (let i = 1; i <= steps; ++i) {
    const s = i * N / steps;
    const v = value(s);
    if (v !== prev) {
      let lo = (i - 1) * N / steps, hi = s;
      for (let k = 0; k < 60; ++k) {
        const mid = 0.5 * (lo + hi);
        if (value(mid) === prev) lo = mid; else hi = mid;
      }
      edges.push({ t: 0.5 * (lo + hi), step: v - prev });
      prev = v;
    }
  }
  return edges;
}

// c_j = (1 / (i 2 pi j)) sum_k step_k e^{-i 2 pi j t_k / N}
function idealSpectrum(edges) {
  const re = new Float64Array(N / 2), im = new Float64Array(N / 2);
  for (const e of edges) {
    const base = -2 * Math.PI * e.t / N;
    for (let j = 1; j < N / 2; ++j) {
      const a = base * j, k = e.step / (2 * Math.PI * j);
      re[j] += k * Math.sin(a);
      im[j] -= k * Math.cos(a);
    }
  }
  return { re, im };
}

function renderOracle(Kc, Km, depth_turns, edges) {
  const out = new Float64Array(N);
  const corr = new Float64Array(N);
  for (const e of edges) {
    const n = Math.ceil(e.t) - 1;                     // the edge lies in (n, n+1]
    const t = Math.round(((n + 1) - e.t) * 65535);
    const sign = e.step / 2;
    corr[(n + N) % N] += sign * ThisBlepSample(t);
    corr[(n + 1 + N) % N] += sign * NextBlepSample(t);
  }
  for (let n = 0; n < N; ++n) {
    const phase = n * Kc / N;
    const w = 0.5 + depth_turns * Math.sin(2 * Math.PI * n * Km / N);
    const naive = (phase - Math.floor(phase)) >= w ? 32767 : 0;
    out[n] = (naive + corr[n] - 16384) * 2;
  }
  return out;
}

function fft(re, im) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; ++i) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { let t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = -2 * Math.PI / len, wr = Math.cos(ang), wi = Math.sin(ang);
    for (let i = 0; i < n; i += len) {
      let cr = 1, ci = 0;
      for (let k = 0; k < len / 2; ++k) {
        const ur = re[i + k], ui = im[i + k];
        const vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci;
        const vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
        re[i + k] = ur + vr; im[i + k] = ui + vi;
        re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
        const nr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nr;
      }
    }
  }
}

// Split by band, because polyBLEP leaves most of its own error at the top by
// construction and the ear reads an alias near the fundamental as a tone.
// The delay and gain are minimised over: a render's one-sample BLEP lead and
// its amplitude convention are not defects.
const BANDS = [[20, 2000], [2000, 8000], [8000, 16000], [16000, 22500]];
function measure(x, ideal) {
  const re = Float64Array.from(x), im = new Float64Array(N);
  fft(re, im);
  const half = N / 2;
  const ir = new Float64Array(half), ii = new Float64Array(half);
  let signal = 0;
  for (let j = 1; j < half; ++j) {
    ir[j] = ideal.re[j] * N * 32768; ii[j] = ideal.im[j] * N * 32768;
    signal += ir[j] * ir[j] + ii[j] * ii[j];
  }
  let best = Infinity, bestD = 0, bestG = 1;
  for (let d = -3; d <= 3; d += 1 / 256) {
    let cross = 0, self = 0;
    for (let j = 1; j < half; ++j) {
      const a = 2 * Math.PI * j * d / N, c = Math.cos(a), s = Math.sin(a);
      const xr = re[j] * c - im[j] * s, xi = re[j] * s + im[j] * c;
      cross += xr * ir[j] + xi * ii[j];
      self += xr * xr + xi * xi;
    }
    const g = self > 0 ? cross / self : 0;
    const err = signal - 2 * g * cross + g * g * self;
    if (err < best) { best = err; bestD = d; bestG = g; }
  }
  const bands = BANDS.map(() => 0);
  for (let j = 1; j < half; ++j) {
    const a = 2 * Math.PI * j * bestD / N, c = Math.cos(a), s = Math.sin(a);
    const xr = bestG * (re[j] * c - im[j] * s), xi = bestG * (re[j] * s + im[j] * c);
    const er = xr - ir[j], ei = xi - ii[j];
    const p = er * er + ei * ei;
    const hz = j * FS / N;
    for (let b = 0; b < BANDS.length; ++b) if (hz >= BANDS[b][0] && hz < BANDS[b][1]) bands[b] += p;
  }
  return {
    total: 10 * Math.log10(best / signal),
    bands: bands.map(p => 10 * Math.log10(p / signal)),
  };
}

// The offset saw's motion is read as a signed 32-bit difference, so it carries
// a direction only while its magnitude stays under a half turn.
function nyquistLoad(Kc, Km, depth_turns) {
  return ((Kc / N) + depth_turns * 2 * Math.PI * Km / N) * 4294967296 / HALF_TURN;
}

const fmt = r => `${r.total.toFixed(1).padStart(6)} |` +
  r.bands.map(b => b.toFixed(1).padStart(7)).join('');

console.log('error against the exact waveform, dB below its power');
console.log(`${N} samples at ${FS} Hz\n`);

for (const Kc of [234, 936, 1872]) {
  console.log(`======== carrier ${(Kc * FS / N).toFixed(0)} Hz`);
  console.log(`                                     total |   <2k   2-8k  8-16k   >16k`);
  {
    const w = widths(0, 0);
    const ideal = idealSpectrum(idealEdges(Kc, 0, 0));
    console.log(`  static 50% (VARIABLE_PULSE)      ${fmt(measure(renderLatch(Kc, w, false), ideal))}`);
  }
  for (const ratio of [1, 1.5, 2, 3]) {
    for (const depth of [0.15, 0.45]) {
      const Km = Math.round(Kc * ratio);
      const w = widths(Km, depth);
      const edges = idealEdges(Kc, Km, depth);
      const ideal = idealSpectrum(edges);
      console.log(`  f_m/f_c ${ratio.toFixed(1)}  +-${(depth * 100).toFixed(0)}%   ` +
        `edges/cyc ${(edges.length / Kc).toFixed(2)}   load ${nyquistLoad(Kc, Km, depth).toFixed(2)}`);
      console.log(`    latch                          ${fmt(measure(renderLatch(Kc, w, false), ideal))}`);
      console.log(`    relative rate                  ${fmt(measure(renderLatch(Kc, w, true), ideal))}`);
      console.log(`    two-saw                        ${fmt(measure(renderTwoSaw(Kc, w), ideal))}`);
      console.log(`    oracle                         ${fmt(measure(renderOracle(Kc, Km, depth, edges), ideal))}`);
    }
  }
  console.log('');
}
