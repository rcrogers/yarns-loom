// THE ACCEPTANCE TEST FOR THE KNOB'S ZONES (user, 2026-08-03): no noise at
// AMOUNT 0, significantly filtered white noise at 32, unfiltered white noise at
// 64, aggressively clipped/distorted noise at 96, extremely harsh noise at 127.
//
// Reports, per AMOUNT, on the chiff alone -- the run minus an AMOUNT 0 run at
// the same settings, which cancels the nominal envelope and leaves the chiff
// plus whatever the mean did on its behalf:
//
//   step   mean |sample step|, the DC-FREE loudness metric. Plain rms folds in
//          the mean's reserve and has read x0.76..x1.32 where the truth was
//          x0.91..x1.06.
//   rms    of the residual with its window mean removed, for the level.
//   rho1   LAG-1 AUTOCORRELATION, which IS the filter: a one-pole at rate r
//          driven by white noise has rho1 = 1 - r. So rho1 -> 0 is unfiltered
//          (white) and rho1 -> 1 is heavily filtered. This is the number that
//          says whether 64 is actually unfiltered.
//   kurt   kurtosis, DECAY-NORMALISED (divided by a local rms) because
//          unnormalised it reads the non-stationary variance instead: a square
//          is 1.00, uniform white 1.79, gaussian 3.00. This is the number that
//          says whether the top is actually squaring off.
//   flip   fraction of samples whose step changes sign. 0.5 is white; above it
//          the spectrum tilts toward Nyquist, which is what a driven multi-level
//          input does before the drive is large enough to reach a random square.
//
// ONE REALIZATION CANNOT CARRY ANY OF THESE. MEASURED over 8 seeds before this
// was fixed: rms spans 120..179 at AMOUNT 8 (49%), and kurt@11ms spans
// 1.33..2.31 at AMOUNT 24 -- on a scale where a square is 1.00, uniform white
// 1.79 and gaussian 3.00, so one draw spans two whole characters. Seed 0 sat at
// or near the TOP of nearly every row. The table below is the MEAN over SEEDS
// realizations, and the spread table under it is what says which columns to
// trust: `step` moves 1-3%, the shape columns far more.
//
// JUDGE THE ONSET, NOT THE POOL. Pooled over a chiff's whole life every one of
// these flattens out, which is how an earlier round concluded there was no
// character axis at all.
const { execSync } = require('child_process');

const AMOUNTS = [0, 8, 16, 24, 32, 48, 64, 80, 96, 112, 127];
const SEEDS = Number(process.env.SEEDS || 8);
const ATTACK_MS = 249;
const DURATION = 90;
const ONSET_MS = 50;
const SR = 45;  // samples per ms

function run(args) {
  return require('./harness').run(args)
    .toString().trim().split('\n').map(Number);
}

// Local rms over a centred window, for normalising the decay out before
// measuring shape. Wide enough to be a level, short enough to track the decay.
function localRms(x, half) {
  const out = new Array(x.length);
  let sum = 0, n = 0;
  for (let i = 0; i < x.length; i++) {
    if (i === 0) {
      for (let j = 0; j <= half && j < x.length; j++) { sum += x[j] * x[j]; n++; }
    } else {
      const add = i + half, drop = i - half - 1;
      if (add < x.length) { sum += x[add] * x[add]; n++; }
      if (drop >= 0) { sum -= x[drop] * x[drop]; n--; }
    }
    out[i] = Math.sqrt(sum / Math.max(n, 1));
  }
  return out;
}

function stats(residual) {
  const n = residual.length;
  let mean = 0;
  for (const v of residual) mean += v;
  mean /= n;
  const centred = residual.map((v) => v - mean);

  let rms = 0;
  for (const v of centred) rms += v * v;
  rms = Math.sqrt(rms / n);

  let step = 0;
  for (let i = 1; i < n; i++) step += Math.abs(residual[i] - residual[i - 1]);
  step /= (n - 1);

  let cov = 0;
  for (let i = 1; i < n; i++) cov += centred[i] * centred[i - 1];
  const rho1 = rms > 0 ? cov / (n - 1) / (rms * rms) : 0;

  // Decay-normalised kurtosis: divide by the local rms first, so a falling
  // level cannot masquerade as a heavy tail. THE HALF-WINDOW MUST STAY SMALL
  // AGAINST THE SLICE -- at 220 samples of a 512-sample slice the "local" rms
  // is the whole slice, the normalisation stops tracking anything, and the
  // hinge read 1.92 where its input's own kurtosis is 1.79.
  const env = localRms(centred, Math.min(220, Math.floor(n / 8)));
  const norm = centred.map((v, i) => (env[i] > 1e-9 ? v / env[i] : 0));
  let m2 = 0, m4 = 0;
  for (const v of norm) { m2 += v * v; m4 += v * v * v * v; }
  m2 /= n; m4 /= n;
  const kurt = m2 > 0 ? m4 / (m2 * m2) : 0;

  let flips = 0, steps = 0;
  for (let i = 2; i < n; i++) {
    const a = residual[i] - residual[i - 1], b = residual[i - 1] - residual[i - 2];
    if (a !== 0 && b !== 0) { steps++; if ((a > 0) !== (b > 0)) flips++; }
  }

  return { rms, step, rho1, kurt, flip: steps ? flips / steps : 0 };
}

const common = ' ' + DURATION + ' attack=' + ATTACK_MS;
const onset = ONSET_MS * SR;
// THE RATE DECAYS ACROSS ANY WINDOW WIDE ENOUGH TO POOL, so rho1 over 50 ms
// reads the average of a sweep, not the rate the chiff STARTS at -- which is
// the only rate AMOUNT sets. Take it over the first few ms as well.
const ONSET_RHO_SAMPLES = 512;  // 11 ms

const COLUMNS = ['step', 'rms', 'rho1@11ms', 'rho1@50ms', 'kurt@11ms', 'kurt', 'flip'];
// Per amount, per seed, the seven statistics. Averaged across seeds for the
// headline and ranged for the spread table -- the statistic is averaged, not
// the samples: kurtosis of pooled draws is not the mean of their kurtoses.
const perAmount = new Map();
for (const amount of AMOUNTS) {
  const rows = [];
  for (let seed = 0; seed < SEEDS; ++seed) {
    const off = run('basic 0' + common + ' seed=' + seed);
    const on = run('basic ' + amount + common + ' seed=' + seed);
    const residual = [];
    for (let i = 0; i < onset; i++) residual.push(on[i] - off[i]);
    const all = stats(residual);
    const early = stats(residual.slice(0, ONSET_RHO_SAMPLES));
    rows.push([all.step, all.rms, early.rho1, all.rho1, early.kurt, all.kurt, all.flip]);
  }
  perAmount.set(amount, rows);
}
const mean = (values) => values.reduce((a, b) => a + b, 0) / values.length;
const DECIMALS = [1, 1, 3, 3, 2, 2, 3];
const WIDTHS = [8, 9, 10, 11, 10, 6, 8];

console.log(`mean over ${SEEDS} seeds; ENV ATTACK ${ATTACK_MS} ms, `
  + `EXCITER DURATION ${DURATION}, onset ${ONSET_MS} ms`);
console.log(
  'AMOUNT  step      rms   rho1@11ms rho1@50ms kurt@11ms  kurt   flip   dB(step)');
let ref = 0;
for (const amount of AMOUNTS) {
  const rows = perAmount.get(amount);
  const avg = COLUMNS.map((_, c) => mean(rows.map((r) => r[c])));
  if (amount === 64) ref = avg[0];
  console.log(String(amount).padStart(6) +
    avg.map((v, c) => v.toFixed(DECIMALS[c]).padStart(WIDTHS[c])).join('') +
    (avg[0] > 0 && ref > 0
      ? (20 * Math.log10(avg[0] / ref)).toFixed(2).padStart(10) : ''.padStart(10)));
}
console.log('\ndB(step) is against AMOUNT 64, the hinge.');

console.log(`\nSEED SPREAD (max - min over ${SEEDS} seeds). A column whose spread`);
console.log('rivals its variation across AMOUNT is not measuring AMOUNT.');
console.log(
  'AMOUNT  step      rms   rho1@11ms rho1@50ms kurt@11ms  kurt   flip');
for (const amount of AMOUNTS) {
  const rows = perAmount.get(amount);
  console.log(String(amount).padStart(6) +
    COLUMNS.map((_, c) => {
      const values = rows.map((r) => r[c]);
      return (Math.max(...values) - Math.min(...values)).toFixed(DECIMALS[c]).padStart(WIDTHS[c]);
    }).join(''));
}
