// L16 AS A CHECK: does every position of AMOUNT change the chiff's CHARACTER?
//
// Level alone does not count. The user's framing: "I don't need filter dynamic
// over whole range. Just like I don't need drive dynamic over the whole range.
// But we need SOMETHING dynamic over whole range." The two character
// mechanisms are the filter's corner (the slew time) and the drive; a stretch
// where neither moves is a stretch where the knob only gets louder, and a knob
// that only gets louder reads as broken.
//
// Reads the ENGINE's own state at the note's first block via chiff_trace, so
// this measures what the firmware actually set up, not a model of it. That
// matters: the constants say where a sweep SATURATES, which is not where it
// stops being audible -- the corner can run past Nyquist well before the
// nominal end, and did, by sixteen knob positions in one variant.
const { execSync } = require('child_process');
const path = require('path');

const TEST = path.join(__dirname, '..', 'hosttest', 'test');
const DURATION = 90;
const FS = 45000;

// THE CORNER, NOT THE SLEW TIME. The slew time keeps moving after the corner
// has left the audio band, and a change nobody can hear is not a change. This
// check read the raw slew time first and PASSED a build with a ten-position
// stretch that only gets louder -- the same mistake, in the check written to
// catch it: measure the effect, not the parameter.
function cornerHz(slewQ5_27) {
  const r = Math.pow(2, -slewQ5_27 / 134217728);   // rate = 2^-t
  const a = 1 - r;
  if (a <= 0) return FS / 2;
  const c = (1 + a * a - 2 * (1 - a) * (1 - a)) / (2 * a);
  if (c <= -1) return FS / 2;                      // never falls 3 dB: transparent
  if (c >= 1) return 0;
  return Math.acos(c) * FS / (2 * Math.PI);
}
// Below a corner this close to Nyquist the filter is doing nothing audible, so
// further movement of it does not count as the knob changing.
const OPEN_HZ = FS / 2 - 1;

function firstBlock(amount) {
  const out = execSync(
    `${TEST} basic ${amount} ${DURATION} gate=40 tail=0 chiff_trace=1`,
    { encoding: 'utf8', maxBuffer: 1 << 26 });
  const line = out.trim().split('\n')[0];
  if (!line) return null;
  const [drive, slew] = line.trim().split(/\s+/).map(Number);
  return { drive, corner: cornerHz(slew) };
}

let fails = 0;
const rows = [];
let prev = null;
// AMOUNT 0 is silence by design and 1 is the first live setting, so the sweep
// starts where a chiff actually exists.
for (let amt = 1; amt <= 127; ++amt) {
  const now = firstBlock(amt);
  if (!now) { console.log(`FAIL AMOUNT ${amt}: engine produced no trace`); fails++; continue; }
  if (prev) {
    // A corner parked outside the band is not moving, whatever the slew time does.
    const bothOpen = now.corner >= OPEN_HZ && prev.corner >= OPEN_HZ;
    rows.push({ amt,
                dSlew: bothOpen ? 0 : now.corner - prev.corner,
                dDrive: now.drive - prev.drive });
  }
  prev = now;
}

// A run of consecutive positions where neither character mechanism moved.
let run = [], worst = [];
for (const r of rows) {
  if (r.dSlew === 0 && r.dDrive === 0) run.push(r.amt);
  else { if (run.length > worst.length) worst = run; run = []; }
}
if (run.length > worst.length) worst = run;

if (worst.length) {
  fails++;
  console.log(`FAIL L16: AMOUNT ${worst[0]}..${worst[worst.length - 1]} ` +
              `(${worst.length} positions) change neither the filter nor the drive`);
  console.log('     the knob only gets louder across that stretch');
} else {
  console.log('PASS L16: every AMOUNT step moves the filter, the drive, or both');
}

// Where each mechanism is doing the work, for reading alongside a failure.
const movedSlew = rows.filter(r => r.dSlew !== 0);
const movedDrive = rows.filter(r => r.dDrive !== 0);
const span = a => a.length ? `${a[0].amt}..${a[a.length - 1].amt}` : 'never';
console.log(`     filter moves over AMOUNT ${span(movedSlew)}`);
console.log(`     drive  moves over AMOUNT ${span(movedDrive)}`);

console.log(fails ? `\n${fails} FAILURE(S)` : '\nALL PASS');
process.exit(fails ? 1 : 0);
