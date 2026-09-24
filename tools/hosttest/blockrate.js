// WHAT THE TREMOLO BIAS'S ONCE-A-BLOCK RAMP COSTS, in dBFS.
//
// blockedge.js says the breaks exist and scale with tremolo depth. It cannot
// say whether they matter, because a second difference has no units anyone
// hears in. This does: it puts a level on them.
//
// ISOLATING THE BIAS. Difference a tremolo run against a tremolo-free one. The
// envelope is bias-independent -- the battery pins that at 0 for four bias
// settings -- so the difference IS the bias and nothing else. Measuring the
// output directly does not work: a fast attack's own motion has more energy
// above 300 Hz than the artifact does, and reads as the artifact.
//
// ISOLATING THE BREAKS. The bias the design intends is slow, because it tracks
// the envelope. The breaks are at the block rate and above. High-pass the
// difference between the two and what is left is the artifact.
//
//   node blockrate.js        report, and fail above LIMIT_DBFS
'use strict';
const H = require('./harness');
const FS = H.frameHz(), BLOCK = 64, FB = FS / BLOCK;
// Above the envelope's own motion, below the block rate.
const HP_HZ = 300;
// The breaks are inherent to sampling the bias once a block, so this is a
// REGRESSION GUARD, not a smoothness oracle: it catches the ramp getting
// coarser, not the ramp existing. Measured worst case is -48.5 dBFS, so this
// sits ~8 dB above it.
const LIMIT_DBFS = -40;
const ATTACKS = [8, 16, 24, 40];
const TREMOLOS = [32767, 65535];

function highPassRms(x, from, to) {
  const alpha = 2 * Math.PI * HP_HZ / FS;
  let lowPass = x[from], sum = 0, n = 0;
  for (let i = from; i < to; i++) {
    lowPass += alpha * (x[i] - lowPass);
    const residual = x[i] - lowPass;
    sum += residual * residual;
    n++;
  }
  return Math.sqrt(sum / n);
}

const args = (attack, tremolo) =>
  `basic 0 64 attack_setting=${attack} decay_setting=64 sustain_setting=70 ` +
  `release_setting=64 gate=300 tail=200 tremolo=${tremolo}`;

console.log(`block rate ${FB.toFixed(1)} Hz, high-pass ${HP_HZ} Hz, ` +
  `limit ${LIMIT_DBFS} dBFS`);
console.log('       attack  tremolo   bias peak   break rms    vs bias      dBFS');
let worst = -Infinity, failures = 0;
for (const attack of ATTACKS) {
  const dry = H.runNumbers(args(attack, 0));
  for (const tremolo of TREMOLOS) {
    const wet = H.runNumbers(args(attack, tremolo));
    const n = Math.min(dry.length, wet.length, Math.round(0.25 * FS));
    const bias = new Float64Array(n);
    let biasPeak = 0;
    for (let i = 0; i < n; i++) {
      bias[i] = wet[i] - dry[i];
      biasPeak = Math.max(biasPeak, Math.abs(bias[i]));
    }
    const rms = highPassRms(bias, 2 * BLOCK, n);
    const dbfs = 20 * Math.log10(rms / 32767);
    const vsBias = 20 * Math.log10(rms / Math.max(biasPeak, 1));
    if (dbfs > worst) worst = dbfs;
    const bad = dbfs > LIMIT_DBFS;
    if (bad) failures++;
    console.log(`${bad ? 'FAIL' : 'PASS'} ${String(attack).padStart(9)} ` +
      `${String(tremolo).padStart(8)} ${biasPeak.toFixed(0).padStart(11)} ` +
      `${rms.toFixed(2).padStart(11)} ${vsBias.toFixed(1).padStart(9)} dB ` +
      `${dbfs.toFixed(1).padStart(9)}`);
  }
}
console.log(`\nworst ${worst.toFixed(1)} dBFS. For scale, the chiff's own ` +
  `inaudibility threshold\nis kChiffInaudibleDbFs = -48.2 dBFS -- though that ` +
  `was set for noise, and a\nperiodic component at one frequency is easier to ` +
  `hear than noise at the same level.`);
console.log(failures ? `${failures} case(s) over the limit` : 'ALL PASS');
process.exit(failures ? 1 : 0);
