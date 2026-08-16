// Proves chiff_sim.html renders with FIRMWARE code: loads the real page
// scripts under DOM stubs, drives the controls as front-panel SETTINGS, and
// compares the samples to the natively-compiled harness.
//
// If this passes, the sim cannot have drifted from yarns/envelope.cc, because
// it is running yarns/envelope.cc.
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const ROOT = path.join(__dirname, '..', '..');
const html = fs.readFileSync(process.argv[2] || path.join(ROOT, 'chiff_sim.html'), 'utf8');
const scripts = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m => m[1]);
if (scripts.length !== 2) {
  console.error(`expected 2 script blocks (engine + sim), got ${scripts.length}`);
  process.exit(1);
}

// ---- DOM stubs: enough for the page to initialise and render ----
const values = {};
const elements = {};
function element(id) {
  if (elements[id]) return elements[id];
  const ctx2d = new Proxy({}, { get: (t, k) =>
    (k === 'canvas' ? { width: 800, height: 400 }
      : k === 'createLinearGradient' ? () => ({ addColorStop() {} })
      : k === 'getImageData' ? () => ({ data: new Uint8ClampedArray(4) })
      : k === 'createImageData' ? (w, h) => ({ data: new Uint8ClampedArray(4 * w * h), width: w, height: h })
      : k === 'measureText' ? () => ({ width: 10 })
      : () => {}) });
  const el = {
    id,
    get value() { return values[id] !== undefined ? values[id] : 0; },
    set value(v) { values[id] = v; },
    textContent: '', innerHTML: '', style: {}, dataset: {},
    width: 800, height: 400, clientWidth: 800, clientHeight: 400,
    addEventListener() {}, getContext: () => ctx2d,
    getBoundingClientRect: () => ({ left: 0, top: 0, width: 800, height: 400 }),
    setAttribute() {}, classList: { add() {}, remove() {}, toggle() {} },
    checked: false, appendChild() {}, remove() {},
  };
  elements[id] = el;
  return el;
}

const sandbox = {
  document: {
    getElementById: element,
    documentElement: { style: {}, getAttribute: () => null, dataset: {} },
    createElement: () => element('_tmp' + Math.random()),
    addEventListener() {},
  },
  window: {},
  getComputedStyle: () => ({ getPropertyValue: () => '#888888' }),
  requestAnimationFrame: fn => { fn(); return 1; },
  cancelAnimationFrame() {},
  performance: { now: () => 0 },
  devicePixelRatio: 1,
  console,
  Math, Date, JSON, Object, Array, Number, String, Boolean, Error, RegExp,
  Float64Array, Float32Array, Int32Array, Int16Array, Uint8Array,
  Uint8ClampedArray, Uint32Array, Uint16Array, ArrayBuffer, DataView,
  Proxy, Reflect, Promise, Symbol, Map, Set, TextDecoder, TextEncoder,
  setTimeout, clearTimeout, setInterval, clearInterval, isNaN, parseInt, parseFloat,
  // Deliberately NO require/process/__dirname: the page runs in a browser, and
  // the compiled engine picks its node path if it sees them.
};
sandbox.addEventListener = () => {};
sandbox.window = sandbox;
sandbox.globalThis = sandbox;
vm.createContext(sandbox);

vm.runInContext(scripts[0], sandbox, { filename: 'chiff_engine.js' });
vm.runInContext(scripts[1], sandbox, { filename: 'chiff_sim.html' });

let fails = 0;
const check = (name, ok, detail) => {
  console.log(`${ok ? 'PASS' : 'FAIL'} ${name}${detail ? '  [' + detail + ']' : ''}`);
  if (!ok) fails++;
};

// THE CASES, and how the native side is driven. These lived in a module shared
// with tools/simengine/parity.js, which proved the BUILT engine matched native
// while this proves the INLINED page does. That check was strictly weaker --
// build.sh always inlines, so a fresh engine and a stale page cannot persist,
// and every divergence that reached it reached this one too.
const H = require('../hosttest/harness');

const GATE_MS = 400, TAIL_MS = 400;
const LFO_BLOCKS = 32;   // blocks per half-cycle of the bias LFO
const SEED = 0xCAFEBABE;

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
  // BIAS AND A NEGATIVE RANGE. Without these, the bias arithmetic and the
  // negative-range path are never compared against the firmware at all -- the
  // two places hardest to reason about.
  { name: 'bias: independent LFO + tremolo',
    attack: 16, decay: 64, sustain: 70, release: 64, amount: 96, chiffDuration: 33,
    biasLfo: 20000, tremolo: 24000 },
  // A range BELOW zero (a negative TIMBRE MOD ENV). Needs a bias to be visible:
  // with none, the output saturate takes the whole thing to zero.
  { name: 'negative range (max < 0) under bias',
    attack: 40, decay: 64, sustain: 70, release: 64, amount: 96, chiffDuration: 90,
    maxTarget: -16383, biasLfo: 20000 },
];

const maxTargetOf = c => c.maxTarget === undefined ? 32767 : c.maxTarget;

// Stated on both sides rather than left to two defaults agreeing, which is
// what broke when the page's LFO period changed and the driver's did not.
function nativeArgs(c) {
  return [
    'basic', c.amount, c.chiffDuration,
    `attack_setting=${c.attack}`, `decay_setting=${c.decay}`,
    `release_setting=${c.release}`, `sustain_setting=${c.sustain}`,
    'peak=100', `gate=${GATE_MS}`, `tail=${TAIL_MS}`,
    `range=${maxTargetOf(c)}`,
    `bias_lfo=${c.biasLfo || 0}`, `tremolo=${c.tremolo || 0}`,
    `bias_lfo_blocks=${LFO_BLOCKS}`,
  ].join(' ');
}

function nativeSamples(c) {
  return H.runNumbers(nativeArgs(c));
}

function compare(native, out) {
  const n = Math.min(native.length, out.length);
  let diffs = 0, first = -1;
  for (let i = 0; i < n; i++) {
    if (native[i] !== out[i]) { diffs++; if (first < 0) first = i; }
  }
  return { n, diffs, first, ok: diffs === 0 && n > 0 };
}


// `let` at a script's top level lands in the realm's global LEXICAL scope, not
// on globalThis, so the page's state is reachable only by evaluating in the
// same context.
const evalInPage = expr => vm.runInContext(expr, sandbox);

new Promise((resolve, reject) => {
  const deadline = Date.now() + 20000;
  const poll = () => {
    if (evalInPage('typeof ENGINE !== "undefined" && ENGINE !== null')) return resolve();
    if (Date.now() > deadline) return reject(new Error('engine never booted'));
    setTimeout(poll, 10);
  };
  poll();
}).then(() => {
  evalInPage('globalThis.__page = { render: render, get FS() { return FS; },' +
             ' get ENGINE() { return ENGINE; } };');
  const page = sandbox.__page;
  const FS = page.FS;
  check('sim booted with the compiled engine', !!page.ENGINE && FS === 45000,
    `kFrameHz ${FS} (from the firmware, not hardcoded in the page)`);

  check('page defines no JS model',
    !evalInPage('typeof alphaQ31 !== "undefined" || typeof rQ31 !== "undefined"'),
    'old JS port symbols are gone');

  const blocks = ms => Math.ceil(Math.round(ms * FS / 1000) / 64) * 64;

  for (const c of CASES) {
    Object.assign(values, {
      atk: c.attack, dec: c.decay, sus: c.sustain, rel: c.release,
      ampmod: 0, vel: 127, amt: c.amount, chiffDur: c.chiffDuration,
    });
    // Drive render() directly with the page's own params shape.
    const res = page.render(Object.assign({}, c, {
      amplitudeModVelocity: 0, velocity: 127,
      gateMs: GATE_MS, tailMs: TAIL_MS, seed: SEED,
      biasLfo: c.biasLfo || 0, tremolo: c.tremolo || 0,
      biasLfoBlocks: LFO_BLOCKS,
      maxTarget: maxTargetOf(c), minTarget: c.minTarget || 0,
    }));

    const r = compare(nativeSamples(c), res.out);
    check(`sim page == native build: ${c.name}`, r.ok,
      r.ok ? `${r.n} samples identical`
           : `${r.diffs} differ, first at ${r.first}`);
  }

  // THE NOTE'S SPAN MUST STAY INSIDE int16. NoteOn forms
  // `int16_t scale_s16 = max - min`, so a wider span wraps and then
  // min_target_q31 + scale * peak overflows int32 on top. No firmware caller
  // can ask for that, but the sim exposes both rails as free controls, so it
  // can -- MEASURED before the clamp: min -16384 / max 16384 reported a ceiling
  // of 31232 instead of 16384, and min -16384 / max 32767 a floor of -25345
  // instead of -16384. Both are garbage, and nothing else here would see it.
  for (const [mn, mx, wantCeil, wantFloor] of [
        [0, 32767, 32766, 0],
        [-16384, 16384, 16382, -16384],   // span 32768, one past int16
        [-16384, 32767, 16382, -16384],   // span 49151, clamped to 32767
        [16384, -16384, 16384, -16384],   // span -32768, the negative limit
      ]) {
    const r = page.render(Object.assign({}, {
      attack: 40, decay: 64, sustain: 70, release: 64,
      amplitudeModVelocity: 0, velocity: 127, amount: 96, chiffDuration: 90,
      gateMs: 600, seed: 0xCAFEBABE }, { minTarget: mn, maxTarget: mx }));
    check(`span stays inside int16: min ${mn} max ${mx}`,
      r.ceiling === wantCeil && r.floorLevel === wantFloor,
      `rails ${r.floorLevel}..${r.ceiling}, want ${wantFloor}..${wantCeil}`);
  }

  // CHIFF DURATION is an absolute time on its own table: the envelope's own
  // minimum of four samples up to twice its maximum, 20 s. Resolve through the
  // engine (firmware code), not a JS reimplementation.
  const durAt = (d) => page.ENGINE.durationSamples(d, 0, 0);
  check('CHIFF DURATION 0 is the table minimum', durAt(0) <= 8, `${durAt(0)} samples`);
  check('CHIFF DURATION 127 reaches ~20 s', durAt(127) > 800000, `${durAt(127)} samples`);
  check('CHIFF DURATION rises with the setting',
    durAt(0) < durAt(64) && durAt(64) < durAt(127),
    `${durAt(0)} / ${durAt(64)} / ${durAt(127)}`);
  check('settings 20 and 21 are distinguishable',
    durAt(20) !== durAt(21), `${durAt(20)} vs ${durAt(21)} samples`);
  check('ENV ATTACK setting 16 resolves to the firmware stage length',
    page.ENGINE.stageSamples(16) === 409, `${page.ENGINE.stageSamples(16)} samples`);

  console.log(fails ? `\n${fails} FAILURES` : '\nALL PASS');
  process.exit(fails ? 1 : 0);
}).catch(e => { console.error(e); process.exit(1); });
