// Search the SETTING space for envelope anomalies, with no human in the loop.
//
// Why this exists: the stage-phase-wrap bug was found only because the user
// happened to name attack=16 / duration=33. Nothing swept the grid.
//
// THE DETECTOR. Once the chiff's duration is up the burst is inert -- depth is
// zeroed and the adjusted targets collapse to the stage target -- so from then
// on the output is the plain classic slew. It must therefore converge onto the
// SAME settings rendered at amount 0. A persistent gap means the chiff left the
// value somewhere it had no business leaving it.
//
// This is design-independent, which earlier attempts were not: the chiff's
// loud onset and its near-rail sag are both intended and both large, so any
// detector keyed on deviation DURING the chiff flags correct behaviour. It is
// also robust to the noise in a short-chiff mean estimate, which sinks
// per-block step detectors at high amounts.
//
// The comparison is DIRECTIONAL. The gap's ideal value is zero -- the chiff
// leaving the value exactly on the classic envelope -- and both failure modes
// push |gap| UP: a bug displaces the value, and the rail-guard sag displaces it
// too. So a SMALLER |gap| than baseline is an improvement, never a regression.
// A design that sags less while still never breaching a rail is strictly
// better, and must not be reported as a deviation just because it moved. Only
// growth is flagged.
//
// Paired with a HARD invariant that sag may not trade away: RAIL DWELL. The
// render loop clamps the value into the note's range, so a design cannot
// overshoot a rail no matter what -- checking for that is vacuous (verified:
// deleting the guard entirely produces zero breaches). What sag actually buys
// is freedom from STICKING to a rail. Pinned samples are flat, the noise
// stops, and the plan records that as the visible-stripe failure. So the
// invariant is the longest run of consecutive samples sitting exactly on a
// rail, and it is checked in both directions regardless of sag.
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

// How far |gap| may GROW past baseline before it is a regression. Well under
// the 60-90 point swings the wrap bug produced, well over run-to-run jitter
// (there is none -- the render is deterministic -- so this is pure margin).
const TOLERANCE = 0.02;
const PEAK_PCT = 75;
// Longest tolerated run of consecutive samples pinned to a rail. The existing
// battery's worst legitimate case is ~22 samples; 64 (one block) is clear of
// that and still far below an audible flat spot.
const MAX_RAIL_DWELL = 64;
// Let the classic slew settle after the chiff's duration is up before comparing.
const SETTLE_BLOCKS = 10;

// The note's range, i.e. chiff_top_ - chiff_floor_ with these driver args.
// Normalising by the CLASSIC TRACE's max instead would divide by a tiny number
// for long attacks (which barely rise inside the rendered span) and inflate
// benign gaps into false positives.
const NOTE_RANGE = 32767;

const ATTACKS = [0, 8, 16, 24, 32, 40, 56, 72, 96, 127];
const DURATIONS = [0, 5, 10, 20, 21, 33, 50, 68, 80, 90, 100, 110, 120, 127];

// ASK THE ENGINE, do not parse a table. The chiff's length is orthogonal to
// the attack -- CHIFF DURATION and velocity are its only inputs -- so no attack
// is passed here.
function durationSamples(duration) {
  return require('./harness').chiffDurationSamples(`report 96 ${duration}`);
}

function run(attack, duration, amount) {
  const args = [
    'basic', amount, duration,
    `attack_setting=${attack}`, 'decay_setting=64', 'release_setting=64',
    'sustain_setting=70', `peak=${PEAK_PCT}`, 'gate=400', 'tail=300', 'range=32767',
  ].join(' ');
  return execSync(`./${BINARY} ${args}`, { cwd: HERE, maxBuffer: 1e9 })
    .toString().trim().split('\n').map(Number);
}

const results = [];
const excursions = [];
for (const attack of ATTACKS) {
  const classic = run(attack, 0, 0);

  for (const duration of DURATIONS) {
    const chiff = run(attack, duration, AMOUNT);

    // Hard invariant: the chiff never DWELLS on a rail. Independent of sag,
    // and not something a redesign may trade away.
    let top = -Infinity;
    for (const v of chiff) if (v > top) top = v;
    let dwell = 0, longest = 0, prev = null;
    for (const v of chiff) {
      if ((v === top || v === 0) && v === prev) { dwell++; }
      else { dwell = (v === top || v === 0) ? 1 : 0; }
      if (dwell > longest) longest = dwell;
      prev = v;
    }
    if (longest > MAX_RAIL_DWELL) {
      excursions.push({ attack, duration, dwell: longest, rail: top });
    }

    const from = durationSamples(duration) + SETTLE_BLOCKS * BLOCK;
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
console.log('metric: once the chiff duration is up the burst is inert, so the');
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
const worse = [], better = [];
for (const r of results) {
  const was = baseline[key(r)];
  if (was === undefined) { worse.push({ ...r, was: null, growth: Infinity }); continue; }
  // Directional: only GROWTH in displacement counts against us.
  const growth = Math.abs(r.dev) - Math.abs(was);
  if (growth > TOLERANCE) worse.push({ ...r, was, growth });
  else if (growth < -TOLERANCE) better.push({ ...r, was, growth });
}

if (better.length) {
  console.log(`${better.length} case(s) IMPROVED (less displacement, not a failure):`);
  better.sort((a, b) => a.growth - b.growth);
  for (const b of better.slice(0, 8)) {
    console.log(`  attack ${String(b.attack).padStart(3)}` +
      ` duration ${String(b.duration).padStart(3)}` +
      `  ${(b.was * 100).toFixed(1)}% -> ${(b.dev * 100).toFixed(1)}%` +
      `  (${(b.growth * 100).toFixed(1)} points closer to the envelope)`);
  }
  console.log('  If this is a deliberate design change, re-record with --update.\n');
}

if (excursions.length) {
  console.log(`FAIL ${excursions.length} case(s) DWELL ON A RAIL` +
    ` (limit ${MAX_RAIL_DWELL} consecutive samples):`);
  excursions.sort((a, b) => b.dwell - a.dwell);
  for (const e of excursions.slice(0, 10)) {
    console.log(`  attack ${String(e.attack).padStart(3)}` +
      ` duration ${String(e.duration).padStart(3)}` +
      `  pinned for ${e.dwell} samples (${(e.dwell / 45).toFixed(1)}ms)`);
  }
}

if (!worse.length && !excursions.length) {
  console.log(`PASS ${results.length} cases: no case displaced more than baseline` +
    ` by over ${(TOLERANCE * 100).toFixed(0)} points, no rail dwell`);
  process.exit(0);
}
if (!worse.length) process.exit(1);

console.log(`FAIL ${worse.length} of ${results.length} cases displaced MORE than baseline:`);
worse.sort((a, b) => b.growth - a.growth);
for (const m of worse.slice(0, 20)) {
  console.log(`  attack ${String(m.attack).padStart(3)}` +
    ` duration ${String(m.duration).padStart(3)}` +
    `  ${m.was === null ? 'NEW' : (m.was * 100).toFixed(1) + '%'}` +
    ` -> ${(m.dev * 100).toFixed(1)}%` +
    `  (+${(m.growth * 100).toFixed(1)} points further off)`);
}
const r = worse[0];
console.log('\nLook at the worst one:');
console.log(`  node plot.js /tmp/anomaly.png 200 \\\n` +
  `    "classic:basic 0 ${r.duration} attack_setting=${r.attack} decay_setting=64 ` +
  `release_setting=64 sustain_setting=70 gate=400" \\\n` +
  `    "chiff:basic ${AMOUNT} ${r.duration} attack_setting=${r.attack} decay_setting=64 ` +
  `release_setting=64 sustain_setting=70 gate=400"`);
process.exit(1);
