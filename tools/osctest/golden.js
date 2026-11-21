// EVERY OSCILLATOR SHAPE, SAMPLE FOR SAMPLE.
//
// Nothing rendered an oscillator sample off target before this. warptest calls
// WarpTimbre -- the parameter map -- and stops; the envelope suites stop at the
// envelope; cvtest drives Voice and Part but only shape 0. So the 42 shapes,
// which are the most audible code in the module, had no check at all.
//
// WHAT IT IS FOR. The per-sample timbre value is 15 bits, and widening it to 16
// means rescaling ten sites that read it with ten different meanings --
// a multiplier, a cutoff, a phase increment, a zone index, a 7/8 scaler with a
// 0x0fff bias. Each is a blind edit without this. With it the claim is
// checkable: feed the proportionally larger value at the wider width and every
// shape must render byte-identical audio.
//
// So the driver hands the shape functions a timbre buffer DIRECTLY rather than
// going through Envelope::RenderSamples. What is pinned is the shape's
// arithmetic, with nothing else moving underneath it.
//
// Its first recording caught Oscillator::Init leaving modulator_phase_ and
// pd_square_ unreset, which made the CZ pulse shapes' output depend on the run
// before them. Fixed in the driver, then in Init.
'use strict';
const { execSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const DIR = __dirname;
const BIN = path.join(DIR, 'osctest');
const VECTORS = path.join(DIR, 'golden_shapes.json');
const UPDATE = process.argv.includes('--update');
// Full scale of the per-sample timbre. If that width ever changes this moves
// with it, and the hashes must not.
const TIMBRE_MAX = 32767;

if (!fs.existsSync(BIN)) {
  console.error('tools/osctest/osctest is missing. Build it first:\n' +
                '  sh tools/osctest/build.sh');
  process.exit(1);
}

const rows = execSync(`./osctest hash timbre_max=${TIMBRE_MAX}`,
                      { cwd: DIR, encoding: 'utf8' })
  .trim().split('\n').map((l) => l.split(' '));
const current = {};
for (const [shape, hash] of rows) current[shape] = hash;

if (UPDATE || !fs.existsSync(VECTORS)) {
  fs.writeFileSync(VECTORS,
    JSON.stringify({ timbre_max: TIMBRE_MAX, shapes: current }, null, 2) + '\n');
  console.log(`recorded ${rows.length} shapes to ${path.basename(VECTORS)}`);
  process.exit(0);
}

const recorded = JSON.parse(fs.readFileSync(VECTORS, 'utf8'));
let failures = 0;
for (const shape of Object.keys(recorded.shapes)) {
  if (current[shape] === recorded.shapes[shape]) continue;
  console.log(`FAIL shape ${shape}: ${recorded.shapes[shape]} -> ${current[shape]}`);
  failures++;
}
const missing = Object.keys(current).length - Object.keys(recorded.shapes).length;
if (missing) {
  console.log(`FAIL shape count moved by ${missing}`);
  failures++;
}

console.log(failures
  ? `\n${failures} shape(s) changed -- re-record with --update only if the ` +
    `output SHOULD move`
  : `\nPASS ${rows.length} shapes render sample for sample as recorded ` +
    `(timbre full scale ${recorded.timbre_max})`);
process.exit(failures ? 1 : 0);
