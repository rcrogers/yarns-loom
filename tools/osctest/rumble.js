// RUMBLE: everything below f0 that is not DC. A shape periodic at f0 has no
// business putting energy there, so all of it is artifact. Reports the total
// and where the strongest component sits, because the ear hears a moving tone.
const N = 8192, HOP = N / 2;
function opt(k, d) { const a = process.argv.find(s => s.startsWith(k + '=')); return a ? Number(a.split('=')[1]) : d; }
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

const x = require('fs').readFileSync(0, 'utf8').trim().split('\n').map(Number);
const rate = opt('rate', 45000), f0 = opt('f0', 0);
const hann = new Float64Array(N);
for (let i = 0; i < N; i++) hann[i] = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / (N - 1));
const power = new Float64Array(N / 2);
for (let start = 0; start + N <= x.length; start += HOP) {
  const re = new Float64Array(N), im = new Float64Array(N);
  for (let i = 0; i < N; i++) re[i] = x[start + i] * hann[i];
  fft(re, im);
  for (let k = 0; k < N / 2; k++) power[k] += re[k] * re[k] + im[k] * im[k];
}
const binHz = rate / N;
let total = 0; for (let k = 1; k < N / 2; k++) total += power[k];
// Stop well short of f0. THREE BINS IS NOT ENOUGH: a fundamental at -4 dB has a
// Hann skirt tens of bins wide, and measuring it reads as a fold sitting just
// under f0. 0.8 x f0 is clear of it and still holds every fold that matters,
// since a fold that close to f0 is not heard as bass anyway.
const kHi = Math.floor((0.8 * f0) / binHz);
const kLo = 4;                        // above DC and the window's own leak
let sum = 0, best = -1, bestK = 0;
for (let k = kLo; k < kHi; k++) {
  sum += power[k];
  if (power[k] > best) { best = power[k]; bestK = k; }
}
const dbTotal = 10 * Math.log10(sum / total);
const dbPeak = 10 * Math.log10(best / total);
console.log(`${dbTotal.toFixed(1)} ${(bestK * binHz).toFixed(0)} ${dbPeak.toFixed(1)}`);
