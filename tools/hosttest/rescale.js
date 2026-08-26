// Envelope::Rescale must move the value the RENDER continues from.
//
// Oscillator::set_shape calls it on a held note "so held notes keep an
// ~equivalent timbre". It scaled the exposed value and the targets, but not
// nominal_value_ or the chiff state -- the two the next run rebuilds the output
// from. A doubled scale left the audio where it was and glided it to the new
// target over the rest of the stage, which is the opposite of keeping a timbre.
//
// It had ONE caller and NO test, which is why. This is the test.
'use strict';
const H = require('./harness');
const AT_BLOCK = 40, BLOCK = 64;
const base = `basic 0 64 attack_setting=60 decay_setting=64 sustain_setting=90 ` +
             `release_setting=64 gate=900 tail=50 rescale_block=${AT_BLOCK}`;
let failures = 0;
function check(name, ok, detail) {
  console.log(`${ok ? 'PASS' : 'FAIL'} ${name}  [${detail}]`);
  if (!ok) failures++;
}
const dry = H.runNumbers(base);
// One block after the rescale, so the ramp inside the block it lands in is past.
const at = (AT_BLOCK + 1) * BLOCK;
for (const factor of [2, 3]) {
  const wet = H.runNumbers(`${base} rescale=${factor}`);
  const before = dry[at - 2 * BLOCK], after = wet[at], plain = dry[at];
  const ratio = plain > 0 ? after / plain : 0;
  check(`rescale x${factor} scales the rendered value`,
        Math.abs(ratio - factor) / factor < 0.02,
        `${plain} -> ${after}, ratio ${ratio.toFixed(3)} want ${factor}`);
  // And it must be a STEP, not a glide: the value an instant later is still
  // scaled, rather than sliding back toward an unscaled trajectory.
  const later = wet[at + 8 * BLOCK], laterPlain = dry[at + 8 * BLOCK];
  const laterRatio = laterPlain > 0 ? later / laterPlain : 0;
  check(`rescale x${factor} holds, not glides`,
        Math.abs(laterRatio - factor) / factor < 0.05,
        `ratio ${laterRatio.toFixed(3)} eight blocks later`);
  void before;
}
console.log(failures ? `\n${failures} failure(s)` : '\nALL PASS');
process.exit(failures ? 1 : 0);
