// WHAT THE CHIFF'S RATE SCHEDULE COSTS, against the truth rather than against
// the previous schedule.
//
// `160272de` moved the rate from per-sample to per-run and swept DURATION
// 0/33/67/90/127, which steps over the band where it hurts. The harm lives
// between 8 and 32, so this sweeps 4..32 densely and reports each schedule as
// an ERROR AGAINST `exact` -- the rate recomputed from the slew time every
// sample -- not against whatever shipped before it.
//
// TWO MECHANISMS, and the point of this script is to separate them:
//   (a) the run's constant rate is wrong on average. Re-centring fixes it.
//   (b) the rate STEPS at the boundary. Only a shorter segment fixes it.
// So the peak's position is reported as well as its size: a peak that lands on
// a boundary is (b), one spread through the run is (a).
//
// Build the variants first: sh tools/hosttest/build_rate_variants.sh
'use strict';
const { execSync } = require('child_process');
const path = require('path');
const DIR = __dirname;

const VARIANTS = ['run', 'wordmid', 'persample', 'taylor'];
const REFERENCE = 'exact';
const DURATIONS = [4, 6, 8, 10, 12, 16, 20, 24, 32, 48, 90];
const SEEDS = [0, 1, 2];
const AMOUNT = 127;
const ATTACK = 40;
const FULL_SCALE = 32767;
// The render's own run boundary, and the word the asm loop consumes at a time.
const BLOCK = 64;
const WORD = 8;

function render(variant, args) {
  const out = execSync(`./test_rate_${variant} ${args}`,
    { cwd: DIR, maxBuffer: 1e9, encoding: 'utf8' });
  return out.trim().split('\n').map(Number);
}

function db(x) { return x > 0 ? 20 * Math.log10(x / FULL_SCALE) : -Infinity; }

console.log(`EXCITER ${AMOUNT}, ATTACK ${ATTACK}, ${SEEDS.length} seeds, ` +
            `error against '${REFERENCE}' (rate recomputed every sample)`);
console.log('peak@ is the peak sample\'s offset in its 64-sample run: 0 is a ' +
            'run boundary.');
console.log('');
const head = ['DUR', 'chiff smp'].concat(
  VARIANTS.map(v => v.padStart(8))).join(' ');
console.log(head + '        (max dB / rms dB / peak@)');

let worst = {};
for (const duration of DURATIONS) {
  const args = `basic ${AMOUNT} ${duration} attack_setting=${ATTACK}`;
  const rows = {};
  let chiffSamples = 0;
  for (const variant of [REFERENCE].concat(VARIANTS)) rows[variant] = [];
  for (const seed of SEEDS) {
    const ref = render(REFERENCE, `${args} seed=${seed}`);
    for (const variant of VARIANTS) {
      const got = render(variant, `${args} seed=${seed}`);
      if (got.length !== ref.length) throw new Error('length mismatch');
      let peak = 0, peakAt = -1, sumSquares = 0;
      for (let i = 0; i < ref.length; ++i) {
        const d = Math.abs(got[i] - ref[i]);
        sumSquares += d * d;
        if (d > peak) { peak = d; peakAt = i; }
      }
      rows[variant].push({
        peak, peakAt, rms: Math.sqrt(sumSquares / ref.length) });
    }
  }
  const out = [String(duration).padStart(3)];
  const report = execSync(`./test_rate_${REFERENCE} ${args} report=1 2>&1`,
    { cwd: DIR, encoding: 'utf8' });
  chiffSamples = +/chiff (\d+) smp/.exec(report)[1];
  out.push(String(chiffSamples).padStart(9));
  for (const variant of VARIANTS) {
    const r = rows[variant];
    // The worst seed, not the mean of them: one realization is not a result,
    // and a schedule is only as good as its worst draw.
    const w = r.reduce((a, b) => (b.peak > a.peak ? b : a));
    const rms = Math.max.apply(null, r.map(x => x.rms));
    const at = w.peak ? (w.peakAt % BLOCK) : 0;
    out.push(`${db(w.peak).toFixed(1)}/${db(rms).toFixed(0)}/${at}`.padStart(8));
    const key = variant;
    if (!worst[key] || w.peak > worst[key].peak) {
      worst[key] = { peak: w.peak, duration, at, word: w.peakAt % WORD };
    }
  }
  console.log(out.join(' '));
}
console.log('');
for (const variant of VARIANTS) {
  const w = worst[variant];
  console.log(`${variant.padStart(8)}  worst ${db(w.peak).toFixed(1)} dBFS at ` +
              `DURATION ${w.duration}, offset ${w.at} in its run ` +
              `(${w.word} into its word)`);
}
