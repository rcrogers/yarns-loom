// A timed stage lands ON its target when its countdown expires.
//
// That is the whole job of the overshoot in RenderStage. A timed stage runs
// four time constants and a slew covers 1 - e^-4 of its span in that time, so
// the adjusted target sits 1/(1 - e^-4) past the stage target and the value
// arrives as the countdown runs out. Chase the stage target itself instead and
// every stage stops e^-4 = 1.83% of its span short: an attack that never
// reaches its peak, a release that hands off above its floor.
//
// Nothing failed when the overshoot was removed. Ten mutations were run on
// 2026-08-26 and this was one of two the behavioural suite could not see --
// battery on 8 seeds, anomaly's 140 cases and rescale all stayed green, golden
// aside, which catches everything by construction. This is the check that
// closes it.
//
// AMOUNT 0, because the output IS nominal there and nothing has to be
// subtracted out of it. Re-measured at AMOUNT 127 / DURATION 0: for every
// setting whose stage outlasts the burst the samples are identical to these, so
// a live chiff reads nothing new here. What a chiff does change is the anchor --
// Trigger takes its closed form from lut_env_expo rather than from nominal --
// and the battery already fails on a broken anchor.
'use strict';
const H = require('./harness');

const BLOCK = 64;
const RANGE = 16383;
const PEAK_PCT = 100, SUSTAIN_PCT = 50;

// What a stage covers in its own time, and so what the overshoot is worth.
const LANDING_SHORTFALL = Math.exp(-4);           // 1.832% of the span
// Short stages land PAST their target: SlewRateFromSlewTime_q31 caps the rate
// at 1 - e^-1, so a stage of 5..15 samples runs more than four time constants
// while the overshoot stays sized for four. Measured worst 1.432% of span, at
// setting 1. The far side is bounded, not zero.
const MAX_OVERRUN = LANDING_SHORTFALL;
// Half of what the overshoot is worth. Measured over every setting and all
// three stages: 0.570% with it, 1.752% without -- the limit sits in the gap.
const MAX_SHORTFALL = LANDING_SHORTFALL / 2;

// The note's lower end, which the release's adjusted target sits below. Left at
// zero that target is under the DAC floor, mean_min pins the mean back up to
// the floor, and the output carries that ramp instead of nominal. Ten times the
// deepest the adjusted target goes, so the correction cannot reach.
const FLOOR = Math.ceil(RANGE * (SUSTAIN_PCT / 100) * LANDING_SHORTFALL * 10 / 100) * 100;

const peak_u16 = Math.floor(65535 * PEAK_PCT / 100);
const sustain_u16 = Math.floor(65535 * SUSTAIN_PCT / 100);
const scale = RANGE - FLOOR;
// Envelope::NoteOn's own arithmetic, in the s15 domain the driver prints.
const target = {
  attack: FLOOR + ((scale * peak_u16) >>> 16),
  decay: FLOOR + ((scale * sustain_u16) >>> 16),
  release: FLOOR,
};

let failures = 0;
const worst = { attack: null, decay: null, release: null };

for (let setting = 0; setting < 128; ++setting) {
  const base = `basic 0 64 attack_setting=${setting} decay_setting=${setting} ` +
    `release_setting=${setting} peak=${PEAK_PCT} sustain=${SUSTAIN_PCT} ` +
    `range=${RANGE} floor=${FLOOR}`;
  const report = H.run(`${base} report=1 2>&1`);
  const stageSamples = (name) => +new RegExp(`${name} (\\d+) smp`).exec(report)[1];
  const attack = stageSamples('attack');
  const decay = stageSamples('decay');
  const release = stageSamples('release');

  // Both stages plus a block either side, so the gate ends past the decay and
  // the release starts from a settled sustain.
  const gateMs = Math.ceil((attack + decay + 2 * BLOCK) / H.frameHz() * 1000) + 1;
  // RenderMs renders whole blocks, and NoteOff lands on the boundary after them.
  const noteOff = Math.ceil(gateMs * H.frameHz() / 1000 / BLOCK) * BLOCK;
  const tailMs = Math.ceil((release + 2 * BLOCK) / H.frameHz() * 1000) + 1;
  const out = H.runNumbers(`${base} gate=${gateMs} tail=${tailMs}`);

  // The countdown expires ON the last sample the stage renders.
  const arrivals = [
    ['attack', out[attack - 1], target.attack, target.attack - FLOOR],
    ['decay', out[attack + decay - 1], target.decay, target.attack - target.decay],
    ['release', out[noteOff + release - 1], target.release, target.decay - FLOOR],
  ];
  for (const [stage, landed, want, span] of arrivals) {
    // Signed toward the stage's own start, so a shortfall is positive whichever
    // way the stage travels.
    const shortfall = (stage === 'attack' ? want - landed : landed - want) / span;
    if (!worst[stage] || shortfall > worst[stage].shortfall) {
      worst[stage] = { setting, shortfall, landed, want, span };
    }
    if (shortfall > MAX_SHORTFALL || shortfall < -MAX_OVERRUN) {
      if (failures < 8) {
        console.log(`FAIL ${stage} setting ${setting}: landed ${landed}, ` +
          `target ${want}, ${(shortfall * 100).toFixed(3)}% of span short`);
      }
      failures++;
    }
  }
}

for (const stage of ['attack', 'decay', 'release']) {
  const w = worst[stage];
  console.log(`  ${stage.padEnd(8)} worst ${(w.shortfall * 100).toFixed(3)}% ` +
    `of span short at setting ${w.setting} (${w.landed} vs ${w.want})`);
}
console.log(failures
  ? `\n${failures} arrival(s) outside ` +
    `[-${(MAX_OVERRUN * 100).toFixed(3)}%, ${(MAX_SHORTFALL * 100).toFixed(3)}%] of span`
  : `\nPASS 128 settings x attack, decay, release: every timed stage arrives ` +
    `within ${(MAX_SHORTFALL * 100).toFixed(3)}% of its target`);
process.exit(failures ? 1 : 0);
