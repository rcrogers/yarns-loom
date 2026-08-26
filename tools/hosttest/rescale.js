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
// AdjustBias steps the bias without the per-block slew, so a pitch-driven
// timbre change is not glided. Same shape of gap as Rescale: one caller, no
// test, so UBSan never executed the addition.
{
  const step = 1 << 24;
  const plain = H.runNumbers(`${base} adjust_bias_block=${AT_BLOCK}`);
  const bumped = H.runNumbers(`${base} adjust_bias_block=${AT_BLOCK} adjust_bias=${step}`);
  // Read the FIRST sample of the block the step lands in. RenderSamples ramps
  // the bias toward its target every block, and this scenario's target is 0, so
  // the step is pulled back out across that same block -- by the next one it is
  // gone. In the firmware the target moves with it, because AdjustBias is
  // called at NoteOn alongside the new warped timbre.
  const at0 = AT_BLOCK * BLOCK;
  const moved = bumped[at0] - plain[at0];
  // Not exact: the first sample already carries one step of the ramp pulling
  // the bias back toward the target, which is 2^17 in Q30 here, or 4 at the
  // output. Within a ramp step is the assertion.
  const want = step >> 16;
  check('AdjustBias steps the output', Math.abs(moved - want) <= want * 0.03,
        `moved ${moved}, want ~${want}`);
  // NOT CHECKED HERE: that the addition saturates rather than wrapping. It
  // cannot be. The mean's rail correction pulls a saturated bias back toward
  // the rail limit exactly as it pulls a wrapped one, and the DAC clamp sits
  // under both -- MEASURED, a saturating step onto a high bias renders a mean
  // of 1303 against a steady 16127, which is indistinguishable from the wrap.
  // The undefinedness is the whole defect, so UBSan is the guard: build.sh runs
  // `adjust_bias=2000000000` on a railed bias through it.
}

console.log(failures ? `\n${failures} failure(s)` : '\nALL PASS');
process.exit(failures ? 1 : 0);
