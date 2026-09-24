// THE SIM'S COPY OF Part::VoiceNoteOn MUST STAY THE FIRMWARE'S.
//
// tools/simengine/note_on_mirror.h reproduces the panel chain because the page
// carries its engine inline and part.cc would drag in the arpeggiator, the
// looper, the just-intonation processor and the MIDI handler. Nothing held the
// copy to the original: `chiff_checks/simparity.js` pins the page to the host
// driver, and the host driver formed AMOUNT its own third way, so all three
// could disagree and every check stay green.
//
// They did. The firmware multiplies by a CEILED reciprocal of full scale, the
// copy divided exactly, and the two parted by up to 7557 in Q30 -- 7 ppm, and
// zero only at 0 and 127 where the clamp hides it.
//
// This drives the REAL Part::VoiceNoteOn -- part.cc, voice.cc, oscillator.cc,
// envelope.cc, arpeggiator.cc, looper.cc, just_intonation_processor.cc and
// settings.cc, host-compiled -- and compares every parameter that reaches the
// envelope with what the copy says. DURATION is compared by effect: it reaches
// the envelope transformed rather than stored, so a second envelope on the
// same rails is given the copy's parameters and must land in the same state.
'use strict';
const { execSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const DIR = __dirname;
const BIN = path.join(DIR, 'paneltest');
if (!fs.existsSync(BIN)) {
  console.error('tools/cvtest/paneltest is missing. Build it first:\n' +
                '  sh tools/cvtest/build.sh');
  process.exit(1);
}

// panel_driver.cc's column order: the setting, then each quantity as
// (what the firmware delivered, what the copy says).
const QUANTITIES = [
  'peak_u16',
  'sustain_u16',
  'attack_u32',
  'decay_u32',
  'release_u32',
  'chiff_amount_initial_q30',
  'chiff_slew_time_at_amount_zero_q5_27',
  'chiff_phase_step_q32',
];

// The axes the sim exposes. Velocity is in here because the peak is not a
// setting -- it falls out of AMPLITUDE MOD VELOCITY and the note's velocity --
// and -64 at velocity 127 is the corner that needs part.cc's peak floor.
const CASES = [];
for (const velocity of [1, 64, 126, 127]) {
  for (const mod of [0, -64, -17, 23, 63]) {
    for (const amplitudeModVelocity of [0, -64, 48]) {
      CASES.push({ velocity, mod, amplitudeModVelocity });
    }
  }
}

let failures = 0;
let compared = 0;
for (const { velocity, mod, amplitudeModVelocity } of CASES) {
  const out = execSync(
    `./paneltest ${mod} ${mod} ${mod} ${velocity} ${amplitudeModVelocity}`,
    { cwd: DIR, maxBuffer: 1e9, encoding: 'utf8' });
  for (const line of out.trim().split('\n')) {
    const values = line.split(' ').map(Number);
    const setting = values[0];
    for (let q = 0; q < QUANTITIES.length; ++q) {
      const delivered = values[1 + 2 * q], mirrored = values[2 + 2 * q];
      ++compared;
      if (delivered === mirrored) continue;
      if (failures < 8) {
        console.log(`FAIL setting ${setting} velocity ${velocity} mod ${mod} ` +
          `amp_mod_vel ${amplitudeModVelocity}: ${QUANTITIES[q]} is ` +
          `${delivered} in the firmware, ${mirrored} in the copy`);
      }
      failures++;
    }
  }
}

console.log(failures
  ? `\n${failures} of ${compared} comparisons differ`
  : `\nPASS ${CASES.length} cases x 128 settings x ${QUANTITIES.length} ` +
    `quantities: the sim's copy is the firmware's chain`);
process.exit(failures ? 1 : 0);
