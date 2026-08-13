// Shared loader for the compiled firmware engine. Used by the node check
// scripts; chiff_sim.html inlines chiff_engine.js and uses the same wrap()
// below, so both go through one interface.
//
// All timing inputs are SETTINGS or samples. Milliseconds appear only in the
// returned meta block, as output.
'use strict';

// Wraps a loaded Emscripten module into the engine API. Kept separate from the
// node-specific require() so the browser can reuse it verbatim.
function wrapChiffEngine(Module) {
  const render = Module.cwrap('chiff_render', 'number',
    Array(19).fill('number'));
  const metaCount = Module.cwrap('chiff_meta_count', 'number', [])();
  const frameHz = Module.cwrap('chiff_frame_hz', 'number', [])();
  const durationSamples = Module.cwrap('chiff_duration_samples', 'number', ['number', 'number', 'number']);
  const stageSamples = Module.cwrap('chiff_stage_samples', 'number', ['number']);

  const META = ['totalSamples', 'gateSamples', 'chiffWindowSamples',
                'attackSamples', 'decaySamples', 'releaseSamples',
                'peak_u16', 'sustain_u16', 'ceiling', 'floor'];

  let bufPtr = 0, bufCap = 0, metaPtr = 0;

  return {
    frameHz,
    durationSamples,
    stageSamples,
    // p: attack, decay, sustain, release, amplitudeModVelocity, velocity,
    //    envModAttack/Decay/Sustain/Release, amount, chiffDuration,
    //    gateSamples, tailSamples, maxTarget, seed
    render(p) {
      const gateSamples = p.gateSamples | 0;
      const tailSamples = p.tailSamples | 0;
      const need = gateSamples + tailSamples + 64;
      if (need > bufCap) {
        if (bufPtr) Module._free(bufPtr);
        bufPtr = Module._malloc(need * 2);
        bufCap = need;
      }
      if (!metaPtr) metaPtr = Module._malloc(metaCount * 4);

      const n = render(
        p.attack | 0, p.decay | 0, p.sustain | 0, p.release | 0,
        p.amplitudeModVelocity | 0, p.velocity === undefined ? 127 : p.velocity | 0,
        p.envModAttack | 0, p.envModDecay | 0,
        p.envModSustain | 0, p.envModRelease | 0,
        p.amount | 0, p.chiffDuration | 0,
        gateSamples, tailSamples,
        p.maxTarget === undefined ? 32767 : p.maxTarget | 0,
        p.seed >>> 0,
        bufPtr, bufCap, metaPtr);

      const out = Module.HEAP16.subarray(bufPtr >> 1, (bufPtr >> 1) + n).slice();
      const meta = {};
      for (let i = 0; i < metaCount; i++) {
        meta[META[i]] = Module.HEAP32[(metaPtr >> 2) + i];
      }
      meta.msOf = samples => samples / frameHz * 1000;
      return { out, meta };
    },
  };
}

if (typeof module !== 'undefined' && module.exports) {
  module.exports = {
    wrapChiffEngine,
    // Resolves to a ready engine. The compiled module is a factory returning a
    // promise, so callers await this once and reuse the result.
    load() {
      const factory = require('./chiff_engine.js');
      return factory().then(wrapChiffEngine);
    },
  };
}
