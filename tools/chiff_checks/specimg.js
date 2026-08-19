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

// A CONSTANT-Q FILTERBANK, ONE FILTER PER PIXEL ROW.
//
// There are no windows and no tiers here. Each row gets a complex resonator
// whose bandwidth is f/Q and whose time constant is Q/f, so BOTH vary
// continuously with the row -- fine and slow at the bottom, wide and fast at
// the top. Nothing switches, so nothing can band: the texture evolves with the
// axis because the analysis does.
//
// It replaces a bank of fixed-window STFTs, which had to pick a window per row
// and put a visible seam wherever the choice changed. Cross-fading the tiers
// blended their LEVELS and left their TEXTURES, so the seams survived.
// Q SETS HOW MANY PIXEL ROWS A BIN COVERS, and that is the whole point of a
// log axis: bandwidth f/Q against a row width of 0.0222f is Q-independent of
// frequency, so a bin is the same height everywhere on the plot. Q = 12 puts it
// at ~3.8 rows -- deliberately coarse, because relative resolution is cheap at
// the top and the settling time it buys is what makes the bottom possible.
const CQ_Q = +(process.env.CQ_Q || 12);
// A FLOOR ON BANDWIDTH, SET BY THE RENDER. A filter of bandwidth B needs about
// 1/(pi*B) seconds to settle; asking for less than this many settling times
// inside the render means the picture is the analysis waking up. Below the
// crossover the bins get relatively wider, which is honest -- it is the
// physical limit of a short render, not a choice.
const CQ_SETTLES_PER_RENDER = 6;
const CQ_PAD_TAUS = 3;      // reflection padding, in filter time constants
// 5 Hz, not 20. The chiff's corner at the bottom of AMOUNT is ~5 Hz, so an axis
// starting at 20 Hz cuts off the very thing it is meant to show -- the old
// fixed bin 0 spanned DC..22 Hz and swept it all into one bright band, which is
// why this was invisible until the bins got fine enough to look.
const SPEC_REF = +(process.env.SPEC_REF || 24), SPEC_RANGE_DB = 96, SPEC_FMIN = 5;
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

// Halve the rate, [1,3,3,1]/8. A row only ever uses a level where its own
// frequency is far below that level's Nyquist, so this is enough anti-aliasing.
function decimate(x) {
  const n = x.length >> 1, d = new Float64Array(n), last = x.length - 1;
  for (let i = 0; i < n; i++) {
    const j = i * 2;
    d[i] = (x[j > 0 ? j - 1 : 0] + 3 * x[j] +
            3 * x[j + 1 < last ? j + 1 : last] + x[j + 2 < last ? j + 2 : last]) / 8;
  }
  return d;
}

// The [1,3,3,1]/8 decimator's magnitude response, so its passband droop can be
// divided back out. At fs/8 it is -2.06 dB per stage and it accumulates over
// the pyramid, drawing a visible step at every level boundary.
function decimResponse(w) {
  return Math.abs((2 * Math.cos(1.5 * w) + 6 * Math.cos(0.5 * w)) / 8);
}

// One row: resonate, rectify, smooth. Returns the envelope at that level's rate.
//
// ZERO PHASE, BY RUNNING THE FILTER BOTH WAYS. A causal resonator's envelope
// lags by about its own time constant, and that time constant varies with the
// row -- so a single transient lands at a different X in every band, which
// reads as the bands being misaligned in time. The forward pass lags by that
// much and the backward pass leads by exactly the same, so their geometric mean
// is centred at every frequency. It is also the average in dB, which is what
// the display shows.
function cqRow(x, fs, f, bwFloor, droop) {
  const bw = Math.max(f / CQ_Q, bwFloor);
  const r = Math.exp(-Math.PI * bw / fs);
  const theta = 2 * Math.PI * f / fs;
  const pr = r * Math.cos(theta), pi = r * Math.sin(theta);
  const n = x.length;
  // REFLECTION PADDING, or the filter's own ramp-up is the picture: started
  // from zero, the bottom of the plot shows the analysis waking up.
  const pad = Math.min(n, Math.ceil(CQ_PAD_TAUS * fs / (Math.PI * bw)));
  const a = 1 - r;
  const run = (from, to, step) => {
    let yr = 0, yi = 0;
    for (let k = 0; k < pad; k++) {          // pre-roll on reflected signal
      const i = from + step * (pad - k);
      const t = pr * yr - pi * yi + x[i >= 0 && i < n ? i : from];
      yi = pr * yi + pi * yr; yr = t;
    }
    const mag = new Float64Array(n);
    let e = 0;
    for (let i = from; i !== to; i += step) {
      const t = pr * yr - pi * yi + x[i];
      yi = pr * yi + pi * yr; yr = t;
      e += a * (Math.sqrt(yr * yr + yi * yi) - e);
      mag[i] = e;
    }
    return mag;
  };
  const fwd = run(0, n, 1), bwd = run(n - 1, -1, -1);
  // WHITE NOISE READS THE SAME AT EVERY ROW. A resonator's output rms is
  // sigma/sqrt(1-r^2), which would slope upward with bandwidth; dividing it out
  // gives per-Hz density, which is what the fixed-bin display showed.
  const scale = Math.sqrt(1 - r * r) * Math.sqrt(0.375 * 512) / droop;
  const env = new Float64Array(n);
  for (let i = 0; i < n; i++) env[i] = Math.sqrt(fwd[i] * bwd[i]) * scale;
  return env;
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

  const base = new Float64Array(totalN);
  for (let i = 0; i < totalN; i++) base[i] = (res.out[i] - ref[i]) / PEAK;

  const W_PX = 1036, H = 320, fMax = FS / 2;
  const logMin = Math.log(SPEC_FMIN), logSpan = Math.log(fMax) - logMin;
  const rowHz = (row) => Math.exp(logMin + (1 - row / H) * logSpan);

  // Decimation pyramid: a row runs at the lowest rate that still leaves its own
  // frequency an octave and a half under Nyquist. Without it the bottom rows
  // would run 45000 filter steps per millisecond of signal for no benefit.
  const levels = [base];
  const px = Buffer.alloc(W_PX * H * 3);
  const renderSeconds = totalN / FS;
  const bwFloor = CQ_SETTLES_PER_RENDER / (Math.PI * renderSeconds);
  let rowsPerLevel = {}, peakMag = 0;
  for (let y = 0; y < H; y++) {
    const f = rowHz(y + 0.5);
    // Decimate only while the row sits an octave below the next level's
    // Nyquist; the droop is compensated but the anti-aliasing still has to hold.
    let L = 0, droop = 1;
    while (f < (FS / (1 << (L + 1))) / 8 && (levels[L].length >> 1) > 64) {
      if (levels.length === L + 1) levels.push(decimate(levels[L]));
      droop *= decimResponse(2 * Math.PI * f / (FS / (1 << L)));
      L++;
    }
    rowsPerLevel[L] = (rowsPerLevel[L] || 0) + 1;
    const fsL = FS / (1 << L);
    const env = cqRow(levels[L], fsL, f, bwFloor, droop);
    for (let x = 0; x < W_PX; x++) {
      const s0 = Math.floor(x / W_PX * env.length);
      const s1 = Math.max(s0 + 1, Math.floor((x + 1) / W_PX * env.length));
      let acc = 0;
      for (let i = s0; i < s1 && i < env.length; i++) acc += env[i];
      const mag = acc / (s1 - s0);
      if (mag > peakMag) peakMag = mag;
      const db = 20 * Math.log10(mag / SPEC_REF + 1e-12);
      const [r, g, bl] = rampColor((db + SPEC_RANGE_DB) / SPEC_RANGE_DB);
      const i = (y * W_PX + x) * 3;
      px[i] = r; px[i + 1] = g; px[i + 2] = bl;
    }
  }
  console.log(`  peak density ${peakMag.toFixed(3)} -> peak sits at ${(20*Math.log10(peakMag/SPEC_REF)).toFixed(1)} dB of the -${SPEC_RANGE_DB}..0 ramp`);
  console.log(`  bandwidth floor ${bwFloor.toFixed(2)} Hz (render ${renderSeconds.toFixed(2)}s),` +
    ` crossover at ${(bwFloor * CQ_Q).toFixed(0)} Hz`);
  console.log('  rows per decimation level:', JSON.stringify(rowsPerLevel),
    ` (Q=${CQ_Q}, ${levels.length} levels)`);
  fs.writeFileSync('/tmp/_spec.ppm',
    Buffer.concat([Buffer.from(`P6\n${W_PX} ${H}\n255\n`), px]));
  execSync(`sips -s format png /tmp/_spec.ppm --out ${out} >/dev/null 2>&1`);
  console.log(`wrote ${out}  ${W_PX}x${H}  (x: 0..${(totalN/FS*1000).toFixed(0)}ms,` +
    ` y: log ${SPEC_FMIN}Hz..${(fMax/1000).toFixed(1)}kHz)  amount ${amount}`);
}).catch(e => { console.error(e); process.exit(1); });
