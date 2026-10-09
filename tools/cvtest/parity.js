// EVERY ENVELOPE DESTINATION GETS THE SAME NOTE.
//
// Voice::NoteOn hands the same chiff to four envelopes -- the oscillator's gain
// and timbre, and one per aux CV output -- down three separate calls. A
// parameter mangled on one of them is silent: the part still sounds right
// through the oscillator and is wrong only where the wrong call went.
//
// That is what `acb66287` shipped. CVOutput::NoteOn still declared
// `uint8_t chiff_duration` after the duration became an absolute sample count,
// so the aux CV outputs got the increment's low byte and every DURATION setting
// landed between 374 s and 6363 s, not monotonically. It was reported from
// hardware and caught by flashing, because nothing off target reaches this
// path: the sim mirrors Part::VoiceNoteOn and stops one call short of it.
//
// WHAT IS COMPARED. The chiff parameters that do not depend on the note's
// rails, and the stage parameters, which come from one ADSR. Deliberately NOT
// compared: chiff_phase_step_q32_, which carries the inaudibility speedup and
// so is a function of each destination's own range, and the slew input's
// maximum, which is half that range by definition.
'use strict';
const { execSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const DIR = __dirname;
const BIN = path.join(DIR, 'cvtest');
if (!fs.existsSync(BIN)) {
  console.error('tools/cvtest/cvtest is missing. Build it first:\n' +
                '  sh tools/cvtest/build.sh');
  process.exit(1);
}
const run = (args) =>
  execSync(`./cvtest ${args}`, { cwd: DIR, maxBuffer: 1e9, encoding: 'utf8' });

// driver.cc's own order, and the reason each one is here.
const FIELDS = [
  'chiff_amount_initial_q30',              // AMOUNT after velocity modulation
  'chiff_amount_q30',                      // where the decay starts from
  'chiff_phase_step_q32',                  // RANGE-DEPENDENT: reported, not compared
  'chiff_slew_time_at_amount_zero_q5_27',  // the whole of DURATION
  'chiff_slew_time_log2_q5_27',            // the slew the burst opens at
  'chiff_slew_input_fraction_q30',         // dimensionless, so it must match
  'stage_phase_increment_u32',
  'stage_samples_left',
];
const RANGE_DEPENDENT = new Set(['chiff_phase_step_q32']);
const DESTINATIONS = ['gain', 'timbre', 'aux1', 'aux2'];

let failures = 0;
let cases = 0;
const settings = [0, 1, 8, 24, 40, 64, 90, 110, 127];

for (const duration of settings) {
  for (const amount of settings) {
    for (const velocity of [1, 64, 127]) {
      const out = run(`chiff duration=${duration} amount=${amount} velocity=${velocity}`);
      const rows = {};
      for (const line of out.trim().split('\n')) {
        const parts = line.split(' ');
        rows[parts[0]] = parts.slice(1).map(Number);
      }
      ++cases;
      const reference = rows[DESTINATIONS[0]];
      for (const destination of DESTINATIONS.slice(1)) {
        for (let i = 0; i < FIELDS.length; ++i) {
          if (RANGE_DEPENDENT.has(FIELDS[i])) continue;
          if (rows[destination][i] === reference[i]) continue;
          if (failures < 8) {
            console.log(`FAIL duration ${duration} amount ${amount} ` +
              `velocity ${velocity}: ${FIELDS[i]} is ${rows[destination][i]} at ` +
              `${destination}, ${reference[i]} at ${DESTINATIONS[0]}`);
          }
          failures++;
        }
      }
    }
  }
}

// The aux CV channel's own last step: Q15 to Q16 by shifting two samples at
// once inside one uint32. Exact only while both halves are under 0x8000 --
// a sample at or above it carries into its neighbour, which shows up as an
// ODD word where every doubled sample is even. The envelope's USAT is what
// makes that true, and this is where it is held.
{
  const words = run('dac blocks=48 amount=127 duration=64')
    .trim().split('\n').join(' ').split(' ').map(Number);
  const odd = words.filter((w) => w & 1).length;
  const outOfRange = words.filter((w) => w < 0 || w > 65534).length;
  console.log(`${odd || outOfRange ? 'FAIL' : 'PASS'} the aux CV pack does not ` +
    `carry across halves  [${words.length} words, ${odd} odd, ` +
    `${outOfRange} outside 0..65534]`);
  if (odd || outOfRange) failures++;
}

console.log(failures
  ? `\n${failures} failure(s)`
  : `\nPASS ${cases} notes x ${DESTINATIONS.length} destinations: every ` +
    `envelope got the same note`);
process.exit(failures ? 1 : 0);
