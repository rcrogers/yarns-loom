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

// MULTI-RESOLUTION, matching the page: one window length cannot serve a log
// axis. Each tier is its own STFT; a row draws from the coarsest tier that
// still resolves it. 512 -> 43.9 Hz bins, 32768 -> 0.7 Hz.
const SPEC_TIERS = [512, 2048, 8192, 32768];
const SPEC_MIN_FRAMES = 3, SPEC_HOP_MIN = 16, SPEC_MAX_FRAMES = 2400;
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
  const PEAK = page.PEAK, FS = page.FS, totalN = res.out.length;

  const tiers = [];
  for (const W of SPEC_TIERS) {
    const FFT = W * 2, span = totalN - W;
    if (span < W * SPEC_MIN_FRAMES) continue;
    const hop = Math.max(SPEC_HOP_MIN, W >> 4, Math.ceil(span / SPEC_MAX_FRAMES));
    const nFrames = Math.max(1, Math.floor(span / hop));
    const re = new Float64Array(FFT), im = new Float64Array(FFT);
    const frames = new Array(nFrames);
    const norm = 1 / Math.sqrt(W / SPEC_TIERS[0]);
    for (let f = 0; f < nFrames; f++) {
      const s0 = f * hop;
      for (let k = 0; k < W; k++) {
        const h = 0.5 - 0.5 * Math.cos(2 * Math.PI * k / (W - 1));
        re[k] = ((res.out[s0+k] - ref[s0+k]) / PEAK) * h * norm; im[k] = 0;
      }
      for (let k = W; k < FFT; k++) { re[k] = 0; im[k] = 0; }
      page.fft(re, im);
      const m = new Float64Array(FFT/2);
      for (let b = 0; b < FFT/2; b++) m[b] = Math.hypot(re[b], im[b]);
      frames[f] = m;
    }
    tiers.push({ frames, nFrames, hop, window: W, binHz: FS/FFT,
                 maxBin: FFT/2, centre: W/2 });
    console.log(`  tier ${String(W).padStart(5)}: ${String(nFrames).padStart(4)} frames,` +
      ` ${(FS/FFT).toFixed(2).padStart(6)} Hz bins, hop ${hop}`);
  }

  const W_PX = 1036, H = 320, fMax = FS/2;
  const logMin = Math.log(SPEC_FMIN), logSpan = Math.log(fMax) - logMin;
  const rowHz = (row) => Math.exp(logMin + (1 - row / H) * logSpan);
  const tierFor = (bw) => { let best = tiers[tiers.length-1];
    for (const t of tiers) if (t.binHz <= bw) { best = t; break; } return best; };
  const px = Buffer.alloc(W_PX * H * 3);
  for (let y = 0; y < H; y++) {
    const fLo = rowHz(y+1), fHi = rowHz(y);
    const t = tierFor(fHi - fLo);
    let b0 = Math.max(0, Math.floor(fLo / t.binHz));
    let b1 = Math.ceil(fHi / t.binHz);
    if (b1 <= b0) b1 = b0 + 1;
    for (let x = 0; x < W_PX; x++) {
      const s0 = x / W_PX * totalN, s1 = (x+1) / W_PX * totalN;
      let f0 = Math.ceil((s0 - t.centre)/t.hop), f1 = Math.ceil((s1 - t.centre)/t.hop);
      if (f0 < 0) f0 = 0;
      if (f1 > t.nFrames) f1 = t.nFrames;
      if (f1 <= f0) { f0 = Math.min(t.nFrames-1, Math.max(0,f0)); f1 = f0+1; }
      let acc = 0, cnt = 0;
      for (let f = f0; f < f1; f++) {
        const col = t.frames[f];
        for (let b = b0; b < b1 && b < t.maxBin; b++) { acc += col[b]; cnt++; }
      }
      const mag = cnt ? acc/cnt : 0;
      const db = 20*Math.log10(mag/SPEC_REF + 1e-12);
      const [r,g,bl] = rampColor((db + SPEC_RANGE_DB)/SPEC_RANGE_DB);
      const i = (y*W_PX + x)*3;
      px[i]=r; px[i+1]=g; px[i+2]=bl;
    }
  }
  fs.writeFileSync('/tmp/_spec.ppm',
    Buffer.concat([Buffer.from(`P6\n${W_PX} ${H}\n255\n`), px]));
  execSync(`sips -s format png /tmp/_spec.ppm --out ${out} >/dev/null 2>&1`);
  console.log(`wrote ${out}  ${W_PX}x${H}  (x: 0..${(totalN/FS*1000).toFixed(0)}ms,` +
    ` y: log ${SPEC_FMIN}Hz..${(fMax/1000).toFixed(1)}kHz)  amount ${amount}`);
}).catch(e => { console.error(e); process.exit(1); });
