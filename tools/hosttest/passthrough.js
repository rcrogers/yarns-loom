// THE MODEL, AS A CHECK. "A note started at amount A progresses downward,
// passing through all the other smaller amounts with the same timbre / spectrum
// / loudness they would have if used as a starting point."
//
// The chiff's state is exactly three numbers: DRIVE (how hard it is clipped),
// SLEW TIME (its slew), and INPUT (what the slew chases). So for every amount B
// below A there must be an instant where A's triple equals B's ONSET triple.
//
// ANCHOR ON WHICHEVER OF THE THREE THE KNOB MOVES THERE, then report the other
// two:
//   B above the hinge -- the knob moves only the DRIVE there, so anchor on it
//   B below the hinge -- the knob moves only the RATE there, so anchor on the
//                        slew time
// A summary statistic of the output cannot do this job: level and centroid
// match for a clipped quiet signal and an unclipped loud one alike.
const { execSync } = require('child_process');
const HINGE = 64;
const ARGS = ' 64 attack=900 chiff_trace=1';
function trace(dir, amount) {
  return require('./harness').run('basic ' + amount + ARGS)
    .toString().trim().split('\n').map((l) => {
      const [d, s, i] = l.split(' ').map(Number);
      return {
        driveOct: d > 0 ? Math.log2(d / Math.pow(2, 26)) : -99,
        slewOct: s / Math.pow(2, 27),
        inputDb: i > 0 ? 20 * Math.log10(i / Math.pow(2, 30)) : -200,
      };
    });
}
const [dir, label] = process.argv.slice(2);
const TOP = 127;
const top = trace(dir, TOP);
console.log('\n' + label + '   (start ' + TOP + ' must pass through each onset)');
console.log('   B   anchor        drive err     input err     slew err');
for (const B of [96, 80, 64, 48, 32, 16]) {
  const o = trace(dir, B)[0];
  // At and above the hinge every amount shares the fastest slew time, so only
  // the drive tells them apart; below it the drive is 1 and only the rate does.
  const useDrive = B >= HINGE;
  let best = null, bestd = Infinity;
  for (const p of top) {
    const d = useDrive ? Math.abs(p.driveOct - o.driveOct)
                       : Math.abs(p.slewOct - o.slewOct);
    if (d < bestd) { bestd = d; best = p; }
  }
  if (!best) { console.log(String(B).padStart(4) + "   no trace"); continue; }
  const anchorMiss = bestd > (useDrive ? 0.05 : 0.02);
  console.log(String(B).padStart(4) + '   ' +
    (useDrive ? 'drive ' : 'slew  ') +
    (anchorMiss ? 'NEVER REACHED' : '             ') +
    (best.driveOct - o.driveOct).toFixed(2).padStart(8) + ' oct' +
    (best.inputDb - o.inputDb).toFixed(1).padStart(9) + ' dB' +
    (best.slewOct - o.slewOct).toFixed(2).padStart(9) + ' oct');
}
