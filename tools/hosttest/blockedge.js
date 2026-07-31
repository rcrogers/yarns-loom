// Does the output have PER-BLOCK discontinuities? An envelope can be smooth
// sample by sample and still be rough to hear, if something that feeds it is
// recomputed once per audio block: the samples inside a block are fine, and
// the SLOPE breaks where the blocks meet.
//
// The user asked exactly this question of a short attack -- "could it be
// per-block calculations where the blocks end up looking very different?" --
// and no check could answer it, because every check rendered with bias == 0.
// Bias is the one input that is both per-block AND value-dependent
// (Oscillator::Render samples tremolo from the envelope's own value at block
// start, then RenderSamples ramps toward that stale target), so it is the
// shape of thing this looks for. It is not limited to bias, though: any
// per-block recomputation shows up the same way.
//
// THE METRIC IS SELF-NORMALIZING, which is what makes it trustworthy across
// settings. A one-pole's slope changes every sample -- that is not roughness.
// So compare the second difference AT block boundaries against the second
// difference INSIDE blocks. Smooth motion gives a ratio near 1 however fast
// the envelope is moving; a per-block artifact spikes the boundaries only.
//
// Usage: node blockedge.js [amount] [tremolo]
'use strict';
const { execSync } = require('child_process');

const amount = +(process.argv[2] || 0);
const tremolo = +(process.argv[3] || 0);
const BLOCK = 64;              // kAudioBlockSize
const ATTACKS = [8, 16, 24, 40, 64];
// A boundary jump this many times the interior jump is a per-block artifact
// rather than ordinary curvature. Ratios seen on smooth material sit near 1.
const LIMIT = 4;

const run = args => execSync(`./test ${args}`, { maxBuffer: 1e9 })
  .toString().trim().split('\n').map(Number);

console.log(`AMOUNT ${amount}, tremolo ${tremolo}` +
  (tremolo ? '' : '  (bias static -- the historical blind spot)'));
console.log(' attack   boundary d2   interior d2    ratio');
let failures = 0;
for (const attack of ATTACKS) {
  const out = run(`basic ${amount} 64 attack_setting=${attack} decay_setting=64 ` +
    `sustain_setting=70 release_setting=64 gate=300 tail=200 tremolo=${tremolo}`);
  // Second difference: the change in slope from one sample to the next.
  let boundaryMax = 0, interiorMax = 0;
  for (let i = 2; i < out.length; i++) {
    const d2 = Math.abs((out[i] - out[i - 1]) - (out[i - 1] - out[i - 2]));
    // i-1 is the last sample of a block when (i-1) % BLOCK == BLOCK-1, so the
    // slope straddling the boundary is the one ending at i.
    if (i % BLOCK === 0 || i % BLOCK === 1) {
      if (d2 > boundaryMax) boundaryMax = d2;
    } else if (d2 > interiorMax) {
      interiorMax = d2;
    }
  }
  const ratio = interiorMax > 0 ? boundaryMax / interiorMax : (boundaryMax ? 999 : 1);
  const bad = ratio > LIMIT;
  if (bad) failures++;
  console.log(`${bad ? 'FAIL' : 'PASS'} ${String(attack).padStart(4)} ` +
    `${String(boundaryMax).padStart(12)} ${String(interiorMax).padStart(13)} ` +
    `${ratio.toFixed(2).padStart(8)}`);
}
console.log(failures ? `\n${failures} setting(s) show per-block discontinuity`
                     : '\nALL PASS');
process.exit(failures ? 1 : 0);
