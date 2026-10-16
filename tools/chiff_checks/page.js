// Shared loader: boots chiff_sim.html headlessly and hands back the page's own
// render()/fft() plus the compiled firmware engine.
//
// Check scripts must go through this rather than eval'ing a slice of the HTML.
// The page has no model of its own any more -- render() is the compiled
// yarns/envelope.cc -- so a script that reaches past this helper is either
// reimplementing firmware DSP (don't) or drawing (fine, use fft below).
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const ROOT = path.join(__dirname, '..', '..');

function makeSandbox(values) {
  const elements = {};
  const element = id => {
    if (elements[id]) return elements[id];
    const ctx2d = new Proxy({}, { get: (t, k) =>
      (k === 'canvas' ? { width: 800, height: 400 }
        : k === 'createLinearGradient' ? () => ({ addColorStop() {} })
        : k === 'getImageData' ? () => ({ data: new Uint8ClampedArray(4) })
        : k === 'createImageData' ? (w, h) =>
            ({ data: new Uint8ClampedArray(4 * w * h), width: w, height: h })
        : k === 'measureText' ? () => ({ width: 10 })
        : () => {}) });
    elements[id] = {
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
    return elements[id];
  };

  const sandbox = {
    document: {
      getElementById: element,
      documentElement: { style: {}, getAttribute: () => null, dataset: {} },
      createElement: () => element('_tmp' + Math.random()),
      addEventListener() {},
    },
    getComputedStyle: () => ({ getPropertyValue: () => '#888888' }),
    requestAnimationFrame: fn => { fn(); return 1; },
    cancelAnimationFrame() {},
    addEventListener() {},
    performance: { now: () => 0 },
    devicePixelRatio: 1,
    console,
    Math, Date, JSON, Object, Array, Number, String, Boolean, Error, RegExp,
    Float64Array, Float32Array, Int32Array, Int16Array, Uint8Array,
    Uint8ClampedArray, Uint32Array, Uint16Array, ArrayBuffer, DataView,
    Proxy, Reflect, Promise, Symbol, Map, Set, TextDecoder, TextEncoder,
    setTimeout, clearTimeout, setInterval, clearInterval,
    isNaN, parseInt, parseFloat,
    // No require/process/__dirname on purpose: the page runs in a browser, and
    // the compiled engine takes its node path if it sees them.
  };
  sandbox.window = sandbox;
  sandbox.globalThis = sandbox;
  return sandbox;
}

// Resolves to { render, nominalValue, fft, ENGINE, FS, values }.
// `values` is the live control map -- set values.atk etc. to move a slider.
//
// opts.strict runs both script blocks in STRICT mode, which is how the
// published artifact runs them. A sloppy-only check once shipped blank graphs,
// so strictmode.js uses this before every republish.
function loadPage(htmlPath, opts) {
  const strict = !!(opts && opts.strict);
  const html = fs.readFileSync(htmlPath || path.join(ROOT, 'chiff_sim.html'), 'utf8');
  const scripts = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m => m[1]);
  if (scripts.length !== 2) {
    return Promise.reject(new Error(
      `expected 2 script blocks (engine + sim), got ${scripts.length}`));
  }
  const values = {};
  const sandbox = makeSandbox(values);
  vm.createContext(sandbox);
  for (const s of scripts) {
    vm.runInContext(strict ? '"use strict";\n' + s : s, sandbox);
  }

  return new Promise((resolve, reject) => {
    const deadline = Date.now() + 20000;
    const poll = () => {
      // `let` bindings live in the realm's global lexical scope, not on
      // globalThis, so readiness has to be evaluated inside the context.
      if (vm.runInContext('typeof ENGINE !== "undefined" && ENGINE !== null', sandbox)) {
        vm.runInContext(
          'globalThis.__page = { render: render, nominalValue: nominalValue,' +
          ' fft: fft,' +
          ' get FS() { return FS; }, get ENGINE() { return ENGINE; },' +
          ' get PEAK() { return PEAK; } };', sandbox);
        const page = sandbox.__page;
        page.values = values;
        page.evalInPage = expr => vm.runInContext(expr, sandbox);
        return resolve(page);
      }
      if (Date.now() > deadline) return reject(new Error('engine never booted'));
      setTimeout(poll, 10);
    };
    poll();
  });
}

module.exports = { loadPage, ROOT };
