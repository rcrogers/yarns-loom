// Search the SETTING space for envelope anomalies, with no human in the loop.
//
// Why this exists: the stage-phase-wrap bug was found only because the user
// happened to name attack=16 / duration=33. Nothing swept the grid.
//
// THE DETECTOR. Once the chiff window closes the burst is inert -- depth is
// zeroed and the aims collapse to the stage target -- so from then on the
// output is the plain classic slew. It must therefore converge onto the SAME
// settings rendered at amount 0. A persistent gap means the chiff left the
// value somewhere it had no business leaving it.
//
// This is design-independent, which earlier attempts were not: the chiff's
// loud onset and its near-rail sag are both intended and both large, so any
// detector keyed on deviation DURING the window flags correct behaviour. It is
// also robust to the noise in a short-window mean estimate, which sinks
// per-block step detectors at high amounts.
//
// It compares against a RECORDED BASELINE rather than an absolute threshold.
// An absolute limit cannot work here: the designed rail-guard sag is itself
// huge in some corners (a 4-sample attack under a max-amount chiff leaves the
// mean ~58% low, entirely by design), so any threshold loose enough to admit
// that also admits real bugs. The baseline sidesteps the problem -- it was
// captured from firmware the user flash-tested and approved by ear, so
// "unchanged from baseline" is a meaningful statement about audible behaviour
// and needs no model of design intent.
//
// Sanity check on the approach: re-run with the stage-phase-wrap bug present
// and 27 of 140 combinations move, several by 60-90 points.
//
//   node anomaly.js                 compare against the baseline
//   node anomaly.js --update        re-record it (say why in the commit)
//   node anomaly.js --amount 96     sweep at another chiff amount
'use strict';
const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');

const HERE = __dirname;
const BLOCK = 64;
const args = process.argv.slice(2);
const UPDATE = args.includes('--update');
const argOf = (name, fallback) => {
  const i = args.indexOf(name);
  return i >= 0 && args[i + 1] !== undefined ? args[i + 1] : fallback;
};
const AMOUNT = +argOf('--amount', 127);
const BINARY = argOf('--binary', 'test');
const BASELINE = path.join(HERE, `anomaly_baseline_amt${AMOUNT}.json`);

// How far a case may move from baseline before it is worth a look. Well under
// the 60-90 point swings the wrap bug produced, well over run-to-run jitter
// (there is none -- the render is deterministic -- so this is pure margin).
const TOLERANCE = 0.02;
// Let the classic slew settle after the window closes before comparing.
const SETTLE_BLOCKS = 10;

// The note's range, i.e. chiff_top_ - chiff_floor_ with these driver args.
// Normalising by the CLASSIC TRACE's max instead would divide by a tiny number
// for long attacks (which barely rise inside the render window) and inflate
// benign gaps into false positives.
const NOTE_RANGE = 32767;

const ATTACKS = [0, 8, 16, 24, 32, 40, 56, 72, 96, 127];
const DURATIONS = [0, 5, 10, 20, 21, 33, 50, 68, 80, 90, 100, 110, 120, 127];

const durationLut = (() => {
  const src = fs.readFileSync(path.join(HERE, '..', '..', 'yarns', 'resources.cc'), 'utf8');
  const start = src.indexOf('const uint32_t lut_chiff_duration_samples[] = {');
  return src.slice(src.indexOf('{', start) + 1, src.indexOf('};', start))
    .split(',').map(x => +x.trim()).filter(x => !isNaN(x));
})();

function run(attack, duration, amount) {
  const args = [
    'basic', amount, duration,
    `attack_setting=${attack}`, 'decay_setting=64', 'release_setting=64',
    'sustain_setting=70', 'peak=100', 'gate=400', 'tail=300', 'range=32767',
  ].join(' ');
  return execSync(`./${BINARY} ${args}`, { cwd: HERE, maxBuffer: 1e9 })
    .toString().trim().split('\n').map(Number);
}

const results = [];
for (const attack of ATTACKS) {
  const classic = run(attack, 0, 0);

  for (const duration of DURATIONS) {
    const chiff = run(attack, duration, AMOUNT);
    const from = durationLut[duration] + SETTLE_BLOCKS * BLOCK;
    const end = Math.min(classic.length, chiff.length);
    let worst = 0, worstAt = 0;
    for (let i = from; i + BLOCK <= end; i += BLOCK) {
      let a = 0, b = 0;
      for (let j = i; j < i + BLOCK; j++) { a += chiff[j]; b += classic[j]; }
      const dev = (a - b) / BLOCK / NOTE_RANGE;
      if (Math.abs(dev) > Math.abs(worst)) { worst = dev; worstAt = i; }
    }
    results.push({ attack, duration, dev: worst, at: worstAt });
  }
}

const key = r => `a${r.attack}d${r.duration}`;

console.log(`swept ${results.length} setting combinations at amount ${AMOUNT} (${BINARY})`);
console.log('metric: after the chiff window closes the burst is inert, so the');
console.log('        output must converge onto the amount-0 render; this is the');
console.log('        residual gap, as a fraction of the note range\n');

if (UPDATE) {
  const out = {};
  for (const r of results) out[key(r)] = +r.dev.toFixed(6);
  fs.writeFileSync(BASELINE, JSON.stringify(out, null, 0) + '\n');
  const w = results.reduce((a, b) => Math.abs(b.dev) > Math.abs(a.dev) ? b : a);
  console.log(`recorded ${results.length} cases to ${path.basename(BASELINE)}`);
  console.log(`  largest gap ${(w.dev * 100).toFixed(1)}%` +
    ` at attack ${w.attack} / duration ${w.duration} (design, not a fault)`);
  process.exit(0);
}

if (!fs.existsSync(BASELINE)) {
  console.log(`no baseline at ${path.basename(BASELINE)} -- run with --update first`);
  process.exit(1);
}
const baseline = JSON.parse(fs.readFileSync(BASELINE, 'utf8'));
const moved = [];
for (const r of results) {
  const was = baseline[key(r)];
  if (was === undefined) { moved.push({ ...r, was: null, delta: Infinity }); continue; }
  const delta = r.dev - was;
  if (Math.abs(delta) > TOLERANCE) moved.push({ ...r, was, delta });
}

if (!moved.length) {
  console.log(`PASS all ${results.length} cases within ${(TOLERANCE * 100).toFixed(0)} points of baseline`);
  process.exit(0);
}

console.log(`FAIL ${moved.length} of ${results.length} cases moved from baseline:`);
moved.sort((a, b) => Math.abs(b.delta) - Math.abs(a.delta));
for (const m of moved.slice(0, 20)) {
  console.log(`  attack ${String(m.attack).padStart(3)}` +
    ` duration ${String(m.duration).padStart(3)}` +
    `  ${m.was === null ? 'NEW' : (m.was * 100).toFixed(1) + '%'}` +
    ` -> ${(m.dev * 100).toFixed(1)}%` +
    `  (${m.delta > 0 ? '+' : ''}${(m.delta * 100).toFixed(1)} points)`);
}
const r = moved[0];
console.log('\nLook at the worst one:');
console.log(`  node plot.js /tmp/anomaly.png 200 \\\n` +
  `    "classic:basic 0 ${r.duration} attack_setting=${r.attack} decay_setting=64 ` +
  `release_setting=64 sustain_setting=70 gate=400" \\\n` +
  `    "chiff:basic ${AMOUNT} ${r.duration} attack_setting=${r.attack} decay_setting=64 ` +
  `release_setting=64 sustain_setting=70 gate=400"`);
process.exit(1);
