// THE STRONGEST INHARMONIC LINE, anywhere in the spectrum, and where it sits.
//
// `spectrum.js` reports the SUM of everything off-harmonic, which a broadband
// floor and a single loud line read alike. The ear does not: a line is a tone,
// and a tone that jumps as the note moves is heard as stepping. `rumble.js`
// finds such a line only below f0. This finds it at any frequency.
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
// A bin is HARMONIC if it lies within the Hann main lobe of a multiple of f0.
// Two bins either side, which is what spectrum.js established as the floor.
const TOL = 2;
function harmonic(k) {
  const r = (k * binHz) / f0;
  return Math.abs(r - Math.round(r)) * f0 / binHz <= TOL;
}
let sum = 0;
const peaks = [];
for (let k = 4; k < N / 2 - 1; k++) {
  if (harmonic(k)) continue;
  sum += power[k];
  // A local maximum, so one broad line is not reported as many.
  if (power[k] >= power[k-1] && power[k] > power[k+1]) peaks.push([power[k], k]);
}
peaks.sort((a, b) => b[0] - a[0]);
const top = peaks.slice(0, 3).map(
    ([p, k]) => `${(k*binHz).toFixed(0)}Hz@${(10*Math.log10(p/total)).toFixed(1)}`);
console.log(`${(10*Math.log10(sum/total)).toFixed(1)} ${top.join(' ')}`);
