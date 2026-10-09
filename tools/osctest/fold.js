// THE FOLD'S OWN PITCH, over time, and whether it GLIDES or STEPS.
//
// A CZ shape is periodic at f0, so all its content sits at harmonics of f0 and
// every alias lands at |k*f0 - m*rate|. Such a fold moves k times faster than
// the note: at MIDI 96 that is 21 for m=1 and 43 for m=2, so ONE PITCH UNIT --
// 1/128 semitone -- moves the m=2 fold by 41 Hz. What the user hears is that
// tone's pitch, and no energy figure can see it. The level is flat within 3 dB
// while the frequency sweeps through DC.
//
// Feed it a GLIDE (`pitch_raw2=`), which is linear in pitch. The fold is linear
// in pitch too, so a straight line is the whole of the answer and the RESIDUAL
// from one is the steppedness.
//
//   ./osctest dump shape=4 hold=1 warp=1 timbre=0 sweep=4 \
//     pitch_raw=12288 pitch_raw2=12292 blocks=4000 \
//     | node fold.js rate=45000 lo=20 hi=400 print=1
//
// MEASURED there on CZ_PULSE_LP, four pitch units of glide:
//
//   pitch_quantized=1   22 22 22 22 22 37 39 39 39 39 39 39 39 80 80 ... 121 121
//   pitch_quantized=0   19 24 29 34 40 45 50 55 61 67 71 77 81 87 93 ... 144 150
//
// off-line rms 10.2 Hz against 1.9 Hz. The risers are 41 Hz -- 7 to 12 SEMITONES
// of the artifact for 0.78 cents of note.
//
// TWO THINGS IT NEEDS.
//   - ONE FOLD IN THE BAND. Two and the frame's peak jumps between them and the
//     trajectory is nonsense. Bracket it with rumble.js first.
//   - A SLOW GLIDE. The frame is 0.73 s, so a tread has to be longer than that;
//     `blocks=4000` over four pitch units gives 1.4 s.
const N = 1 << 15;
function opt(k, d) { const a = process.argv.find(s => s.startsWith(k + '=')); return a ? Number(a.split('=')[1]) : d; }
function fft(re, im, inverse) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; ++i) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { let t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = (inverse ? 2 : -2) * Math.PI / len;
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
  if (inverse) for (let i = 0; i < n; ++i) { re[i] /= n; im[i] /= n; }
}

const x = require('fs').readFileSync(0, 'utf8').trim().split('\n').map(Number);
const rate = opt('rate', 45000), lo = opt('lo', 20), hi = opt('hi', 400);
const print = opt('print', 0);
// Hopped by a quarter of a frame. 1.4 Hz bins at 45 kHz, against risers of 41.
const HOP = N / 4;
const hann = new Float64Array(N);
for (let i = 0; i < N; i++) hann[i] = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / (N - 1));
const binHz = rate / N;
const kLo = Math.max(2, Math.ceil(lo / binHz)), kHi = Math.min(N / 2 - 2, Math.floor(hi / binHz));
const t = [], f = [], a = [];
for (let start = 0; start + N <= x.length; start += HOP) {
  const re = new Float64Array(N), im = new Float64Array(N);
  for (let i = 0; i < N; i++) re[i] = x[start + i] * hann[i];
  fft(re, im, false);
  let best = -1, bestK = kLo;
  for (let k = kLo; k <= kHi; k++) {
    const p = re[k] * re[k] + im[k] * im[k];
    if (p > best) { best = p; bestK = k; }
  }
  // Parabolic on the log magnitudes: the step to resolve is a few bins.
  const l = Math.log(re[bestK - 1] ** 2 + im[bestK - 1] ** 2);
  const c = Math.log(best);
  const r = Math.log(re[bestK + 1] ** 2 + im[bestK + 1] ** 2);
  const d = 0.5 * (l - r) / (l - 2 * c + r);
  t.push((start + N / 2) / rate);
  f.push((bestK + (isFinite(d) ? d : 0)) * binHz);
  a.push(Math.sqrt(best));
}
// Weighted by amplitude: a frame where the fold is quiet is reading something
// else's peak.
let sw = 0, stw = 0, sfw = 0, stt = 0, stf = 0;
for (let i = 0; i < f.length; ++i) {
  const w = a[i] * a[i];
  sw += w; stw += w * t[i]; sfw += w * f[i]; stt += w * t[i] * t[i]; stf += w * t[i] * f[i];
}
const slope = (stf * sw - stw * sfw) / (stt * sw - stw * stw);
const intercept = (sfw - slope * stw) / sw;
let sr = 0;
for (let i = 0; i < f.length; ++i) {
  const r = f[i] - (intercept + slope * t[i]);
  sr += a[i] * a[i] * r * r;
}
const swept = Math.abs(slope) * (t[t.length - 1] - t[0]);
const rms = Math.sqrt(sr / sw);
console.log(
  `swept ${swept.toFixed(0)} Hz  off-line rms ${rms.toFixed(1)} Hz  ` +
  `= ${(100 * rms / Math.max(swept, 1e-9)).toFixed(1)}% of the sweep`);
if (print) {
  const out = [];
  for (let i = 0; i < f.length; ++i) out.push(`${f[i].toFixed(0)}`);
  console.log('  ' + out.join(' '));
}
