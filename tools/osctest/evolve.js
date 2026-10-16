// HOW A SPECTRUM MOVES THROUGH A NOTE, which a single number over the whole
// render cannot show. Reads samples (one per line) on stdin.
//
// It exists because crest factor conflates two different things: a wide
// spectrum and a narrowband signal whose envelope wanders. Narrowband noise has
// a Rayleigh envelope, so it reads "peaky" while being spectrally pure. Only a
// spectrum per window separates them.
//
//   spread   energy-weighted sd about the centroid, in Hz -- the actual width
//   centroid first moment, in Hz -- brightness
//   inband   fraction of energy within +/-half a window's resolution of the
//            peak bin, which is 1 for a pure tone and small for noise
//
//   ./paratest dump ... | node evolve.js rate=45000 win=1024 hop=512
'use strict';
function opt(k, d) {
  const a = process.argv.find(s => s.startsWith(k + '='));
  return a ? Number(a.split('=')[1]) : d;
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
    const ang = -2 * Math.PI / len;
    for (let i = 0; i < n; i += len) {
      for (let k = 0; k < len / 2; ++k) {
        const wr = Math.cos(ang * k), wi = Math.sin(ang * k);
        const ur = re[i + k], ui = im[i + k];
        const vr = re[i + k + len / 2] * wr - im[i + k + len / 2] * wi;
        const vi = re[i + k + len / 2] * wi + im[i + k + len / 2] * wr;
        re[i + k] = ur + vr; im[i + k] = ui + vi;
        re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
      }
    }
  }
}
const rate = opt('rate', 45000);
const N = opt('win', 1024);
const HOP = opt('hop', N / 2);
const x = require('fs').readFileSync(0, 'utf8').trim().split('\n').map(Number);
const df = rate / N;
console.log('  t_ms     rms   centroid    spread   peak_hz  inband');
for (let s = 0; s + N <= x.length; s += HOP) {
  const re = new Float64Array(N), im = new Float64Array(N);
  let rms = 0;
  for (let i = 0; i < N; ++i) {
    const w = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / (N - 1));   // Hann
    re[i] = x[s + i] * w;
    rms += x[s + i] * x[s + i];
  }
  rms = Math.sqrt(rms / N);
  fft(re, im);
  const half = N / 2;
  const p = new Float64Array(half);
  let tot = 0, peak = 0, peakBin = 0;
  for (let k = 1; k < half; ++k) {
    p[k] = re[k] * re[k] + im[k] * im[k];
    tot += p[k];
    if (p[k] > peak) { peak = p[k]; peakBin = k; }
  }
  if (tot <= 0) continue;
  let centroid = 0;
  for (let k = 1; k < half; ++k) centroid += k * df * p[k];
  centroid /= tot;
  let spread = 0;
  for (let k = 1; k < half; ++k) {
    const d = k * df - centroid;
    spread += d * d * p[k];
  }
  spread = Math.sqrt(spread / tot);
  let inband = 0;
  for (let k = Math.max(1, peakBin - 1); k <= Math.min(half - 1, peakBin + 1); ++k) inband += p[k];
  inband /= tot;
  const f = (v, w, d) => v.toFixed(d).padStart(w);
  console.log([f(s / rate * 1000, 7, 1), f(rms, 8, 0), f(centroid, 10, 0),
               f(spread, 9, 0), f(peakBin * df, 9, 0), f(inband, 8, 3)].join(''));
}
