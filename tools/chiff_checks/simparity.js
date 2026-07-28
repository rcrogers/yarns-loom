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
const { execSync } = require('child_process');

const ROOT = path.join(__dirname, '..', '..');
const HOSTTEST = path.join(ROOT, 'tools', 'hosttest');
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

const CASES = [
  { name: 'user case (atk 16, dur 33, amt 127)',
    atk: 16, dec: 64, sus: 70, rel: 64, amt: 127, chiffDur: 33 },
  { name: 'long window (atk 40, dur 120)',
    atk: 40, dec: 64, sus: 70, rel: 64, amt: 127, chiffDur: 120 },
  { name: 'chiff off (amt 0)',
    atk: 40, dec: 64, sus: 70, rel: 64, amt: 0, chiffDur: 90 },
];

const GATE_MS = 400, TAIL_MS = 400;

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
      atk: c.atk, dec: c.dec, sus: c.sus, rel: c.rel,
      ampmod: 0, vel: 127, amt: c.amt, chiffDur: c.chiffDur,
    });
    // Drive render() directly with the page's own params shape.
    const p = {
      attack: c.atk, decay: c.dec, sustain: c.sus, release: c.rel,
      amplitudeModVelocity: 0, velocity: 127,
      amount: c.amt, chiffDuration: c.chiffDur,
      gateMs: GATE_MS, tailMs: TAIL_MS, seed: 0xCAFEBABE,
    };
    const res = page.render(p);

    const args = [
      'basic', c.amt, c.chiffDur,
      `attack_setting=${c.atk}`, `decay_setting=${c.dec}`,
      `release_setting=${c.rel}`, `sustain_setting=${c.sus}`,
      'peak=100', `gate=${GATE_MS}`, `tail=${TAIL_MS}`, 'range=32767',
    ].join(' ');
    const native = execSync(`./test ${args}`, { cwd: HOSTTEST, maxBuffer: 1e9 })
      .toString().trim().split('\n').map(Number);

    const n = Math.min(native.length, res.out.length);
    let diffs = 0, first = -1;
    for (let i = 0; i < n; i++) {
      if (native[i] !== res.out[i]) { diffs++; if (first < 0) first = i; }
    }
    check(`sim page == native build: ${c.name}`, diffs === 0 && n > 0,
      diffs === 0 ? `${n} samples identical`
        : `${diffs} differ, first at ${first}`);
  }

  // CHIFF DURATION is attack-relative now (no table): setting 64 == the attack,
  // 0 == 1/8x, 127 == ~7.7x, via the firmware ChiffWindowSamples. Resolve
  // through the engine (firmware code), not a JS reimplementation.
  const atk = 40, atkSmp = page.ENGINE.stageSamples(atk);
  const durAt = (d) => page.ENGINE.durationSamples(d, atk, 0, 0);
  check('CHIFF DURATION 64 == attack duration', Math.abs(durAt(64) - atkSmp) <= 1,
    `${durAt(64)} vs attack ${atkSmp}`);
  check('CHIFF DURATION 0 == 1/8 attack', Math.abs(durAt(0) - Math.round(atkSmp / 8)) <= 2,
    `${durAt(0)} vs ${Math.round(atkSmp / 8)}`);
  check('settings 20 and 21 are distinguishable',
    durAt(20) !== durAt(21), `${durAt(20)} vs ${durAt(21)} samples`);
  check('ENV ATTACK setting 16 resolves to the firmware stage length',
    page.ENGINE.stageSamples(16) === 409, `${page.ENGINE.stageSamples(16)} samples`);

  console.log(fails ? `\n${fails} FAILURES` : '\nALL PASS');
  process.exit(fails ? 1 : 0);
}).catch(e => { console.error(e); process.exit(1); });
