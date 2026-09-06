// WHERE A SHAPE'S ENERGY ACTUALLY IS. Reads samples (one per line) on stdin and
// prints a Welch-averaged periodogram's peaks, plus the power in the bands the
// open questions are about.
//
// It exists for questions a golden cannot answer: a hash says two renders
// differ, never that one of them hums.
//
// TRAP the whistle plan already records: narrowband noise has no second-scale
// steady state at high Q, so ONE window is a draw from a Rayleigh envelope and
// not a level. Welch-averaging many windows is what makes the peak POSITION
// trustworthy; the peak's HEIGHT still needs a long render and several seeds.
//
//   ./osctest dump shape=21 hold=1 timbre=0 sweep=4 pitch=60 blocks=4000 \
//     | node spectrum.js rate=45000 f0=261.6
'use strict';
const N = 8192;                       // window, and the FFT's length
const HOP = N / 2;                    // 50% overlap, Hann

function opt(key, fallback) {
  const hit = process.argv.find(a => a.startsWith(key + '='));
  return hit ? +hit.slice(key.length + 1) : fallback;
}

// Iterative radix-2 Cooley-Tukey, real input. No dependency, and N is fixed at
// a power of two by construction above.
function fft(re, im) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; ++i) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      let t = re[i]; re[i] = re[j]; re[j] = t;
      t = im[i]; im[i] = im[j]; im[j] = t;
    }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = -2 * Math.PI / len;
    const wr = Math.cos(ang), wi = Math.sin(ang);
    for (let i = 0; i < n; i += len) {
      let cr = 1, ci = 0;
      for (let k = 0; k < len / 2; ++k) {
        const ur = re[i + k], ui = im[i + k];
        const vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci;
        const vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
        re[i + k] = ur + vr; im[i + k] = ui + vi;
        re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
        const nr = cr * wr - ci * wi;
        ci = cr * wi + ci * wr; cr = nr;
      }
    }
  }
}

const text = require('fs').readFileSync(0, 'utf8');
const x = text.trim().split('\n').map(Number);
const rate = opt('rate', 45000);
const f0 = opt('f0', 0);
const skip = opt('skip', 0);          // samples of onset to drop

const hann = new Float64Array(N);
for (let i = 0; i < N; ++i) hann[i] = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / N);

const power = new Float64Array(N / 2);
let windows = 0;
for (let start = skip; start + N <= x.length; start += HOP) {
  const re = new Float64Array(N), im = new Float64Array(N);
  for (let i = 0; i < N; ++i) re[i] = x[start + i] * hann[i];
  fft(re, im);
  for (let k = 0; k < N / 2; ++k) power[k] += re[k] * re[k] + im[k] * im[k];
  ++windows;
}
if (!windows) { console.error('too few samples for one window'); process.exit(1); }

const binHz = rate / N;
let total = 0;
for (let k = 1; k < N / 2; ++k) total += power[k];

// The peaks, as a fraction of the whole so the answer does not depend on level.
const ranked = [];
for (let k = 2; k < N / 2 - 1; ++k) {
  if (power[k] > power[k - 1] && power[k] >= power[k + 1]) ranked.push(k);
}
ranked.sort((a, b) => power[b] - power[a]);
console.log(`${windows} windows of ${N} at ${rate} Hz  (${binHz.toFixed(1)} Hz/bin)`);
console.log('top peaks:');
for (const k of ranked.slice(0, 5)) {
  const share = power[k] / total;
  console.log(`   ${(k * binHz).toFixed(1).padStart(9)} Hz   ` +
              `${(10 * Math.log10(share)).toFixed(1).padStart(6)} dB of total` +
              (f0 ? `   = ${(k * binHz / f0).toFixed(3)} x f0` : ''));
}
function band(lo, hi) {
  let s = 0;
  for (let k = Math.max(1, Math.round(lo / binHz)); k <= Math.round(hi / binHz) && k < N / 2; ++k) s += power[k];
  return 10 * Math.log10(s / total);
}
console.log('bands, as dB of total power:');
console.log(`   0-60 Hz (mains-ish)      ${band(0, 60).toFixed(1)}`);
console.log(`   60-200 Hz                ${band(60, 200).toFixed(1)}`);
console.log(`   the block rate, 703 Hz   ${band(703 - binHz, 703 + binHz).toFixed(1)}` +
            `   (+/- one bin)`);
if (f0) console.log(`   f0 +/- 10%               ${band(f0 * 0.9, f0 * 1.1).toFixed(1)}`);

// ALIASING, WHICH IS THE POWER THAT IS NOT AT A HARMONIC.
//
// The top-peaks view cannot show this: a shape's aliases are individually far
// below its harmonics and there are many of them, so they never enter a top
// five. What matters is their SUM. Every bin within tolerance of k*f0 for some
// integer k is signal; everything else, above a floor to skip the DC skirt, is
// alias. A pure tone reads about -80 dB here, which is the window's own leakage
// and the floor of what this can resolve.
if (f0) {
  // WIDE ENOUGH FOR THE WINDOW, or a harmonic reports itself as alias. A Hann
  // main lobe is two bins either side, so a tolerance narrower than that counts
  // a harmonic's own leakage against it -- at MIDI 48 that read 37% of the
  // power as aliasing for a shape whose peaks are all exact harmonics.
  const TOLERANCE = Math.max(0.02 * f0, 2 * binHz);
  let aliasPower = 0, harmonicPower = 0;
  for (let k = 2; k < N / 2; ++k) {
    const hz = k * binHz;
    if (hz < f0 * 0.5) continue;   // below the fundamental is skirt, not alias
    const nearest = Math.round(hz / f0);
    const isHarmonic = nearest >= 1 &&
        Math.abs(hz - nearest * f0) < TOLERANCE;
    if (isHarmonic) harmonicPower += power[k]; else aliasPower += power[k];
  }
  const ratio = aliasPower / (harmonicPower + aliasPower);
  console.log(`   NOT AT A HARMONIC        ${(10 * Math.log10(ratio)).toFixed(1)}` +
              `   <- aliasing, as dB of the total`);
}
