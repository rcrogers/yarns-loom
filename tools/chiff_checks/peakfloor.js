// Guards the AMPLITUDE MOD VELOCITY -64 / velocity 127 corner.
//
// peak_u16 falls to EXACTLY 0 only at amplitude_mod_velocity == -64, velocity
// 127. A zero peak puts the attack's target on the release level, and the
// envelope's "nothing to do this stage" early-out (envelope.cc Trigger) then
// drops the attack outright -- the stage's DURATION is lost, so the note slews
// to sustain at the DECAY rate and velocity 127 snaps to a wholly different
// trajectory than 126. part.cc floors the peak at 1 to keep that from
// happening; this check is what proves the floor is still there.
//
// It lives with the SIM checks, not the host battery, because the host harness
// compiles only yarns/envelope.cc -- it sets adsr.peak_u16 directly and so
// never runs the velocity chain. tools/simengine/engine.cc mirrors
// Part::VoiceNoteOn, which makes the sim the only automated place this corner
// is reachable.
'use strict';
const { loadPage } = require('./page');

let fails = 0;
const check = (name, ok, detail) => {
  console.log(`${ok ? 'PASS' : 'FAIL'} ${name}${detail ? '  [' + detail + ']' : ''}`);
  if (!ok) fails++;
};

// Chiff off, so the trajectory is the pure envelope and the comparison across
// velocities is exact rather than noise-dominated. A long attack makes a lost
// attack stage unmissable.
const BASE = {
  attack: 100, decay: 64, sustain: 70, release: 64,
  amplitudeModVelocity: -64, amount: 0, chiffDuration: 90,
  seed: 0xCAFEBABE,
};
const VELOCITIES = [125, 126, 127];
const GATE_MS = 2000, TAIL_MS = 400;

loadPage().then(page => {
  // Straight to ENGINE.render rather than the page's render(): peak_u16 is the
  // quantity under test and the page's wrapper does not forward it. Same
  // compiled firmware either way.
  const FS = page.FS;
  const renderAt = velocity => page.ENGINE.render(Object.assign({}, BASE, {
    velocity,
    gateSamples: Math.round(GATE_MS * FS / 1000),
    tailSamples: Math.round(TAIL_MS * FS / 1000),
    maxTarget: page.PEAK,
  }));

  const runs = VELOCITIES.map(renderAt);
  const peaks = runs.map(r => r.meta.peak_u16);

  check('peak never reaches 0 (attack would be skipped)',
    peaks.every(peak => peak >= 1), `peaks ${peaks.join(', ')}`);
  check('velocity 127 at AMP MOD VEL -64 sits on the floor',
    peaks[VELOCITIES.indexOf(127)] === 1, `peak_u16 ${peaks[2]}`);

  // Sample a quarter of the way into the attack: far enough in that a skipped
  // attack has visibly raced toward sustain, early enough that every velocity
  // is still in its attack.
  const probe = Math.floor(runs[0].meta.attackSamples / 4);
  if (!(probe > 0)) throw new Error('attack too short to probe');
  const levels = runs.map(r => r.out[probe]);
  const step126 = Math.abs(levels[1] - levels[0]);
  const step127 = Math.abs(levels[2] - levels[1]);

  // The trajectory must vary smoothly with velocity: the last step is a
  // neighbour of the one before it, not an outlier. Skipping the attack made
  // step127 orders of magnitude larger.
  check('velocity 126 -> 127 is in family with 125 -> 126',
    step127 <= 4 * step126 + 2,
    `levels ${levels.join(', ')} at sample ${probe}; ` +
    `steps ${step126} then ${step127}`);

  console.log(fails ? `\n${fails} FAILURES` : '\nALL PASS');
  process.exit(fails ? 1 : 0);
}).catch(err => { console.error(err); process.exit(1); });
