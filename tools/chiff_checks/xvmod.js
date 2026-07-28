// EXCITER AMT VEL MOD (the "XV" setting): velocity modulation of EXCITER
// AMOUNT, built like every other *_mod_velocity setting --
//   modulate_7_13(amount, mod, velocity) >> 6
// clamped into 0..127 by modulate_7_13's own 13-bit CONSTRAIN.
//
// Sim-side for the same reason as peakfloor.js: the host harness compiles only
// yarns/envelope.cc and takes an amount directly, so Part::VoiceNoteOn's
// modulation chain is only reachable through tools/simengine/engine.cc, which
// mirrors it. The engine reports the resolved amount as meta.chiffAmount.
'use strict';
const { loadPage } = require('./page');

let fails = 0;
const check = (name, ok, detail) => {
  console.log(`${ok ? 'PASS' : 'FAIL'} ${name}${detail ? '  [' + detail + ']' : ''}`);
  if (!ok) fails++;
};

const BASE = {
  attack: 40, decay: 64, sustain: 70, release: 64,
  amplitudeModVelocity: 0, chiffDuration: 90, seed: 0xCAFEBABE,
};
const GATE_MS = 400, TAIL_MS = 400;

loadPage().then(page => {
  const FS = page.FS;
  const resolve = (amount, amountModVelocity, velocity) =>
    page.ENGINE.render(Object.assign({}, BASE, {
      amount, amountModVelocity, velocity,
      gateSamples: Math.round(GATE_MS * FS / 1000),
      tailSamples: Math.round(TAIL_MS * FS / 1000),
      maxTarget: page.PEAK,
    })).meta.chiffAmount;

  // Identity: a zero mod must leave the setting exactly as dialed, at every
  // velocity. This is what keeps existing patches sounding unchanged.
  const velocities = [0, 1, 63, 64, 126, 127];
  const amounts = [0, 1, 32, 96, 127];
  let worst = null;
  for (const amount of amounts) {
    for (const velocity of velocities) {
      const got = resolve(amount, 0, velocity);
      if (got !== amount && !worst) worst = `amount ${amount} vel ${velocity} -> ${got}`;
    }
  }
  check('mod 0 is the identity at every velocity', !worst,
    worst || `${amounts.length * velocities.length} combinations exact`);

  // Direction: positive mod opens the exciter as velocity rises, negative
  // closes it. Monotone, not merely different.
  const rising = velocities.map(v => resolve(32, 63, v));
  const falling = velocities.map(v => resolve(32, -64, v));
  const monotone = (values, sign) => values.every((v, i) =>
    i === 0 || (sign > 0 ? v >= values[i - 1] : v <= values[i - 1]));
  check('positive mod rises with velocity', monotone(rising, 1),
    `${rising.join(' -> ')}`);
  check('negative mod falls with velocity', monotone(falling, -1),
    `${falling.join(' -> ')}`);

  // Clamping: modulate_7_13 CONSTRAINs to 13 bits, so the resolved amount can
  // never leave the 0..127 the amount setting is defined over.
  const extremes = [];
  for (const amount of amounts) {
    for (const mod of [-64, 63]) {
      for (const velocity of [0, 127]) extremes.push(resolve(amount, mod, velocity));
    }
  }
  check('resolved amount stays within 0..127',
    extremes.every(v => v >= 0 && v <= 127),
    `min ${Math.min(...extremes)}, max ${Math.max(...extremes)}`);

  // A dialed amount of 0 is not a hard off: mod*velocity alone can open it,
  // exactly as timbre/env mods lift a zeroed init. Worth pinning, because the
  // sim's chiff-free "dialed" trace has to zero the mod as well as the amount.
  check('amount 0 with positive mod still opens at high velocity',
    resolve(0, 63, 127) > 0 && resolve(0, 63, 0) === 0,
    `vel 0 -> ${resolve(0, 63, 0)}, vel 127 -> ${resolve(0, 63, 127)}`);

  console.log(fails ? `\n${fails} FAILURES` : '\nALL PASS');
  process.exit(fails ? 1 : 0);
}).catch(err => { console.error(err); process.exit(1); });
