// Vertical banding in the chiff's spectrogram -- the thing rail contact
// actually costs.
//
// Rail dwell was the wrong proxy. Max pinned-run length is 1-6 samples across
// every build tried, including one with the rail guard deleted entirely, so it
// cannot discriminate: the relax aim already breaks pin-runs -- it is the
// rail-dwell damper, and it is load-bearing. What matters is not
// how long the value sticks but that sticking DISRUPTS THE NOISE PATTERN,
// which shows up as vertical stripes in the spectrogram.
//
// A band is a time column whose broadband energy sits well below its
// neighbours -- the definition the earlier bandstat work converged on.
//
//   node banding.js [binary] [amount]
'use strict';
const { execSync } = require('child_process');

const BINARY = process.argv[2] || 'test';
const AMOUNT = +(process.argv[3] || 127);
const FS = require('./harness').frameHz(), N = 256, HOP = 64;
const BAND_LO = 5000, BAND_HI = 20000;   // the audible fizz
const DIP_DB = 6;                        // below local median = a band
const NEIGHBOURHOOD = 10;                // columns either side

function run(bin, c, amount) {
  const args = [
    'basic', amount, c.duration, `attack_setting=${c.attack}`,
    'decay_setting=64', 'release_setting=64', `sustain_setting=${c.sustain}`,
    'peak=75', `gate=${c.gate}`, 'tail=200', 'range=32767',
  ].join(' ');
  return execSync(`./${bin} ${args}`, { cwd: __dirname, maxBuffer: 1e9 })
    .toString().trim().split('\n').map(Number);
}

// Iterative radix-2 FFT, in place. Analysis only -- no firmware DSP here.
function fft(re, im) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { [re[i], re[j]] = [re[j], re[i]]; [im[i], im[j]] = [im[j], im[i]]; }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = -2 * Math.PI / len;
    const wr = Math.cos(ang), wi = Math.sin(ang);
    for (let i = 0; i < n; i += len) {
      let cr = 1, ci = 0;
      for (let k = 0; k < len / 2; k++) {
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

// Broadband energy per time column, in dB, of the chiff MINUS its chiff-free
// reference -- so the envelope's own shape is removed and only noise remains.
function columnsDb(chiff, ref) {
  const lo = Math.round(BAND_LO / FS * N), hi = Math.round(BAND_HI / FS * N);
  const cols = [];
  const re = new Float64Array(N), im = new Float64Array(N);
  for (let s = 0; s + N <= Math.min(chiff.length, ref.length); s += HOP) {
    for (let k = 0; k < N; k++) {
      const w = 0.5 - 0.5 * Math.cos(2 * Math.PI * k / (N - 1));
      re[k] = (chiff[s + k] - ref[s + k]) * w;
      im[k] = 0;
    }
    fft(re, im);
    let e = 0;
    for (let b = lo; b <= hi && b < N / 2; b++) e += re[b] * re[b] + im[b] * im[b];
    cols.push(10 * Math.log10(e + 1e-9));
  }
  return cols;
}

function bandCount(cols) {
  // Only judge columns where the burst is actually live: a band in silence is
  // not a band. Referencing this to the GLOBAL PEAK was a bug -- the chiff
  // onset is tens of dB above its own tail, so peak-25dB judged about four
  // columns out of thousands and the whole metric read zero. Reference the
  // noise floor instead: live means clearly above the quietest columns.
  const sorted = [...cols].sort((a, b) => a - b);
  const floorDb = sorted[Math.floor(sorted.length * 0.05)];
  const peakDb = sorted[sorted.length - 1];
  const liveDb = floorDb + Math.max(12, (peakDb - floorDb) * 0.15);
  const live = cols.map(c => c > liveDb);
  let bands = 0, worstDip = 0;
  for (let i = NEIGHBOURHOOD; i < cols.length - NEIGHBOURHOOD; i++) {
    if (!live[i]) continue;
    const around = [];
    for (let j = i - NEIGHBOURHOOD; j <= i + NEIGHBOURHOOD; j++) {
      if (j !== i && live[j]) around.push(cols[j]);
    }
    if (around.length < NEIGHBOURHOOD) continue;
    around.sort((a, b) => a - b);
    const median = around[around.length >> 1];
    const dip = median - cols[i];
    if (dip > DIP_DB) { bands++; if (dip > worstDip) worstDip = dip; }
  }
  return { bands, live: live.filter(Boolean).length, worstDip };
}

// Banding does not show equally at every setting. It needs the chiff to reach
// its DARK end while still audible -- a long window on a HELD note -- and it
// shows near a rail, which low sustain (release floor-riding) provokes. A grid
// of short gates cannot see it at all, which is how the first pass here
// reported zero everywhere.
const CASES = [];
for (const gate of [400, 3000, 8000]) {
  for (const duration of [33, 68, 90, 110, 120, 127]) {
    for (const sustain of [5, 40, 70, 120]) {
      for (const attack of [8, 40, 96]) {
        CASES.push({ attack, duration, sustain, gate });
      }
    }
  }
}

let totalBands = 0, totalLive = 0;
const rows = [];
for (const c of CASES) {
  const r = bandCount(columnsDb(run(BINARY, c, AMOUNT), run(BINARY, c, 0)));
  totalBands += r.bands; totalLive += r.live;
  rows.push({ ...c, ...r });
}
console.log(`banding in ${BAND_LO / 1000}-${BAND_HI / 1000}kHz residual,` +
  ` ${BINARY}, amount ${AMOUNT}, ${CASES.length} cases`);
rows.sort((a, b) => b.bands - a.bands || b.worstDip - a.worstDip);
console.log('worst cases:');
console.log('  atk  dur  sus  gate    live   banded   worst dip');
for (const r of rows.slice(0, 8)) {
  console.log('  ' + String(r.attack).padStart(3) + String(r.duration).padStart(5) +
    String(r.sustain).padStart(5) + String(r.gate).padStart(6) +
    String(r.live).padStart(8) + String(r.bands).padStart(9) +
    (r.worstDip ? r.worstDip.toFixed(1) + ' dB' : '-').padStart(12));
}
const rate = totalLive ? totalBands / totalLive * 100 : 0;
console.log(`\ntotal: ${totalBands} banded of ${totalLive} live columns (${rate.toFixed(3)}%)`);
