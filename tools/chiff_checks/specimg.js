// Render the chiff's noise spectrogram to a PNG and LOOK at it. Per the plan,
// image inspection is the only instrument that has reliably matched the user's
// ears; scalar column stats have repeatedly not.
//
// Samples come from the page, which runs the compiled firmware. Controls are
// front-panel SETTINGS.
// Usage: node specimg.js out.png [amount] [chiffDuration] [attack] [seedHex]
'use strict';
const fs = require('fs');
const { execSync } = require('child_process');
const { loadPage } = require('./page.js');

const out = process.argv[2] || '/tmp/specimg.png';
const amount = +(process.argv[3] || 127);
const chiffDuration = +(process.argv[4] || 90);
const attack = +(process.argv[5] || 40);
const seed = parseInt(process.argv[6] || 'CAFEBABE', 16) >>> 0;

const N = 512, FFT = 1024, HOP = 64;
// MATCHES THE PAGE: log frequency axis, 96 dB range, the same perceptual ramp.
// A linear axis to 22 kHz drew 94% of the low-AMOUNT chiff in two pixel rows.
const SPEC_REF = 24, SPEC_RANGE_DB = 96, SPEC_FMIN = 20;
const SPEC_RAMP = [
  [  4,  6, 22], [ 32, 18, 88], [ 46, 70,142], [ 34,124,136],
  [ 68,172,104], [168,208, 76], [248,230, 62], [255,252,190],
];
function rampColor(t) {
  const x = Math.max(0, Math.min(1, t)) * (SPEC_RAMP.length - 1);
  const i = Math.min(SPEC_RAMP.length - 2, Math.floor(x)), f = x - i;
  const a = SPEC_RAMP[i], b = SPEC_RAMP[i + 1];
  return [Math.round(a[0] + (b[0]-a[0])*f), Math.round(a[1] + (b[1]-a[1])*f),
          Math.round(a[2] + (b[2]-a[2])*f)];
}

loadPage().then(page => {
  const p = {
    attack, decay: 64, sustain: 70, release: 64,
    amplitudeModVelocity: 0, velocity: 127,
    amount, chiffDuration, gateMs: 600, tailMs: 600, seed,
  };
  const res = page.render(p);
  const ref = page.render(Object.assign({}, p, { amount: 0 })).out;
  const PEAK = page.PEAK, FS = page.FS, binHz = FS / FFT;

  const re = new Float64Array(FFT), im = new Float64Array(FFT);
  const frames = [];
  for (let s = 0; s + N <= res.out.length; s += HOP) {
    for (let k = 0; k < N; k++) {
      const h = 0.5 - 0.5 * Math.cos(2 * Math.PI * k / (N - 1));
      re[k] = ((res.out[s + k] - ref[s + k]) / PEAK) * h;
      im[k] = 0;
    }
    for (let k = N; k < FFT; k++) { re[k] = 0; im[k] = 0; }
    page.fft(re, im);
    const mag = new Float64Array(FFT / 2);
    for (let b = 0; b < FFT / 2; b++) mag[b] = Math.hypot(re[b], im[b]);
    frames.push(mag);
  }

  const W = frames.length, H = 320, maxBin = FFT / 2;
  const fMax = FS / 2;
  const logMin = Math.log(SPEC_FMIN), logSpan = Math.log(fMax) - logMin;
  const rowHz = (row) => Math.exp(logMin + (1 - row / H) * logSpan);
  const px = Buffer.alloc(W * H * 3);
  for (let y = 0; y < H; y++) {
    let b0 = Math.max(0, Math.floor(rowHz(y + 1) / binHz));
    let b1 = Math.ceil(rowHz(y) / binHz);
    if (b1 <= b0) b1 = b0 + 1;
    for (let x = 0; x < W; x++) {
      let acc = 0, cnt = 0;
      for (let b = b0; b < b1 && b < maxBin; b++) { acc += frames[x][b]; cnt++; }
      const mag = cnt ? acc / cnt : 0;
      const db = 20 * Math.log10(mag / SPEC_REF + 1e-12);
      const [r, g, bl] = rampColor((db + SPEC_RANGE_DB) / SPEC_RANGE_DB);
      const i = (y * W + x) * 3;
      px[i] = r; px[i + 1] = g; px[i + 2] = bl;
    }
  }
  fs.writeFileSync('/tmp/_spec.ppm',
    Buffer.concat([Buffer.from(`P6\n${W} ${H}\n255\n`), px]));
  execSync(`sips -s format png /tmp/_spec.ppm --out ${out} >/dev/null 2>&1`);
  console.log(`wrote ${out}  ${W}x${H}  (x: 0..${((W * HOP + N / 2) / FS * 1000).toFixed(0)}ms,` +
    ` y: log ${SPEC_FMIN}Hz..${(fMax/1000).toFixed(1)}kHz)`);
  console.log(`  attack setting ${attack} = ${res.attackSamples} smp,` +
    ` chiff duration ${chiffDuration} = ${res.windowN} smp, amount ${amount}`);
}).catch(e => { console.error(e); process.exit(1); });
