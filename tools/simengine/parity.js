// Proves the Emscripten engine and the natively-compiled harness produce
// IDENTICAL samples from identical settings. Both are built from
// yarns/envelope.cc, so any mismatch means a build/transform problem, not a
// model difference -- there is only one model now.
'use strict';
const { execSync } = require('child_process');
const path = require('path');
const { load } = require('./loader.js');

const HOSTTEST = path.join(__dirname, '..', 'hosttest');

// Settings-only. Velocity 127 with amplitude mod 0 gives peak_u16 == UINT16_MAX,
// which is what the native driver's `peak=100` produces.
const CASES = [
  { name: 'user case (atk 16, dur 33, amt 127)',
    attack: 16, decay: 64, sustain: 70, release: 64, amount: 127, chiffDuration: 33 },
  { name: 'default-ish (atk 40, dur 90, amt 96)',
    attack: 40, decay: 64, sustain: 70, release: 64, amount: 96, chiffDuration: 90 },
  { name: 'chiff off (amt 0)',
    attack: 40, decay: 64, sustain: 70, release: 64, amount: 0, chiffDuration: 90 },
  { name: 'long window (dur 120)',
    attack: 40, decay: 64, sustain: 70, release: 64, amount: 127, chiffDuration: 120 },
  { name: 'short window (dur 5)',
    attack: 20, decay: 50, sustain: 90, release: 40, amount: 64, chiffDuration: 5 },
];

const GATE_MS = 400, TAIL_MS = 400, RANGE = 32767;

load().then(engine => {
  const FS = engine.frameHz;
  // The native driver renders whole blocks per RenderMs call; mirror that so
  // NoteOff lands on the same sample in both.
  const blocksFor = ms => Math.ceil(Math.round(ms * FS / 1000) / 64);
  const gateSamples = blocksFor(GATE_MS) * 64;
  const tailSamples = blocksFor(TAIL_MS) * 64;

  let fails = 0;
  for (const c of CASES) {
    const args = [
      'basic', c.amount, c.chiffDuration,
      `attack_setting=${c.attack}`, `decay_setting=${c.decay}`,
      `release_setting=${c.release}`, `sustain_setting=${c.sustain}`,
      'peak=100', `gate=${GATE_MS}`, `tail=${TAIL_MS}`, `range=${RANGE}`,
    ].join(' ');
    const native = execSync(`./test ${args}`, { cwd: HOSTTEST, maxBuffer: 1e9 })
      .toString().trim().split('\n').map(Number);

    const { out, meta } = engine.render(Object.assign({
      amplitudeModVelocity: 0, velocity: 127,
      gateSamples, tailSamples, maxTarget: RANGE, seed: 0xCAFEBABE,
    }, c));

    const n = Math.min(native.length, out.length);
    let firstDiff = -1, diffs = 0;
    for (let i = 0; i < n; i++) {
      if (native[i] !== out[i]) { diffs++; if (firstDiff < 0) firstDiff = i; }
    }
    const ok = diffs === 0 && n > 0;
    if (!ok) fails++;
    console.log(`${ok ? 'PASS' : 'FAIL'} ${c.name}  [${n} samples compared` +
      (ok ? ', identical' :
        `, ${diffs} differ, first at ${firstDiff}: native ${native[firstDiff]} vs engine ${out[firstDiff]}`) +
      `]`);
    if (ok) {
      console.log(`       attack ${meta.attackSamples} smp` +
        ` (${meta.msOf(meta.attackSamples).toFixed(1)}ms), chiff window ` +
        `${meta.chiffWindowSamples} smp (${meta.msOf(meta.chiffWindowSamples).toFixed(1)}ms)`);
    }
  }
  console.log(fails ? `\n${fails} FAILURES` : '\nALL PASS -- engine == native build');
  process.exit(fails ? 1 : 0);
}).catch(e => { console.error(e); process.exit(1); });
