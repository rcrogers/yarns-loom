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

const N = 512, HOP = 64, SPEC_REF_DB = -60;

loadPage().then(page => {
  const p = {
    attack, decay: 64, sustain: 70, release: 64,
    amplitudeModVelocity: 0, velocity: 127,
    amount, chiffDuration, gateMs: 600, tailMs: 600, seed,
  };
  const res = page.render(p);
  const ref = page.render(Object.assign({}, p, { amount: 0 })).out;
  const PEAK = page.PEAK;

  const re = new Float64Array(N), im = new Float64Array(N);
  const frames = [];
  for (let s = 0; s + N <= res.out.length; s += HOP) {
    for (let k = 0; k < N; k++) {
      const h = 0.5 - 0.5 * Math.cos(2 * Math.PI * k / (N - 1));
      re[k] = ((res.out[s + k] - ref[s + k]) / PEAK) * h;
      im[k] = 0;
    }
    page.fft(re, im);
    const mag = new Float64Array(N / 2);
    for (let b = 0; b < N / 2; b++) mag[b] = Math.hypot(re[b], im[b]);
    frames.push(mag);
  }

  const W = frames.length, H = N / 2;
  const px = Buffer.alloc(W * H);
  for (let x = 0; x < W; x++) {
    for (let y = 0; y < H; y++) {
      const db = 20 * Math.log10(frames[x][H - 1 - y] + 1e-9);
      const t = Math.max(0, Math.min(1, (db - SPEC_REF_DB) / -SPEC_REF_DB));
      px[y * W + x] = Math.round(t * 255);
    }
  }
  fs.writeFileSync('/tmp/_spec.pgm',
    Buffer.concat([Buffer.from(`P5\n${W} ${H}\n255\n`), px]));
  execSync(`sips -s format png /tmp/_spec.pgm --out ${out} >/dev/null 2>&1`);
  console.log(`wrote ${out}  ${W}x${H}` +
    `  (x: 0..${((W * HOP + N / 2) / page.FS * 1000).toFixed(0)}ms, y: ${page.FS / 2}Hz at top)`);
  console.log(`  attack setting ${attack} = ${res.attackSamples} smp,` +
    ` chiff duration ${chiffDuration} = ${res.windowN} smp, amount ${amount}`);
}).catch(e => { console.error(e); process.exit(1); });
