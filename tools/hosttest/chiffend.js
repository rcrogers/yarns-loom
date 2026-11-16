// WHERE THE CHIFF ENDS, per AMOUNT: the block at which the engine's own amount
// reaches zero.
//
// Two checks, over AMOUNT 1..127 x note range x DURATION:
//   - DURATION reads true: the amount is zero by the duration, plus the block
//     the last run lands in.
//   - No cliff: neighbouring AMOUNTs end within MAX_NEIGHBOUR_RATIO of each
//     other. An absolute inaudibility threshold put one between two AMOUNTs,
//     at a position set by the note's range, with a 67x step in decay speed.
//
// The range is the sweep's other axis because the threshold moved with it:
// one voice's full span, four voices', and the WIND share that put the step
// between AMOUNT 7 and 8. Reads state, so it is seed-independent.
'use strict';
const H = require('./harness');

const BLOCK = 64;
const RANGES = [32767, 25665, 6416, 4300, 1000];
const DURATIONS = [16, 32, 64];
// Past this many durations a chiff is reported as unfinished, not timed.
const RENDER_DURATIONS = 8;
const MAX_NEIGHBOUR_RATIO = 1.5;

function endSample(amount, duration, range, durationSamples) {
  const gateMs = Math.ceil(RENDER_DURATIONS * durationSamples * 1000 / H.frameHz());
  const lines = H.run(`basic ${amount} ${duration} range=${range} ` +
                      `gate=${gateMs} tail=0 chiff_trace=1`).trim().split('\n');
  for (let b = 0; b < lines.length; ++b) {
    if (+lines[b].split(/\s+/)[0] === 0) return (b + 1) * BLOCK;
  }
  return Infinity;
}

const describe = (end) => end === Infinity ? 'never' : `${end} smp`;

let fails = 0;
for (const duration of DURATIONS) {
  const durationSamples = H.chiffDurationSamples(`basic 1 ${duration}`);
  const deadline = durationSamples + BLOCK;
  for (const range of RANGES) {
    const ends = [];
    for (let amount = 1; amount <= 127; ++amount) {
      ends[amount] = endSample(amount, duration, range, durationSamples);
    }
    const late = [], cliffs = [];
    for (let amount = 1; amount <= 127; ++amount) {
      if (ends[amount] > deadline) late.push(amount);
      if (amount > 1) {
        const ratio = Math.max(ends[amount], ends[amount - 1]) /
                      Math.min(ends[amount], ends[amount - 1]);
        if (ratio > MAX_NEIGHBOUR_RATIO) cliffs.push(`${amount - 1}->${amount}`);
      }
    }
    const where = `DURATION ${duration} (${durationSamples} smp), range ${range}`;
    if (late.length) {
      ++fails;
      const a = late[0];
      console.log(`FAIL ${where}: ${late.length} AMOUNTs end after the duration, ` +
                  `first ${a} at ${describe(ends[a])}`);
    }
    if (cliffs.length) {
      ++fails;
      const [lo, hi] = cliffs[0].split('->').map(Number);
      console.log(`FAIL ${where}: cliff at AMOUNT ${cliffs[0]} ` +
                  `(${describe(ends[lo])} -> ${describe(ends[hi])})` +
                  (cliffs.length > 1 ? `, ${cliffs.length - 1} more` : ''));
    }
    if (!late.length && !cliffs.length) console.log(`PASS ${where}`);
  }
}
console.log(fails ? `${fails} FAILURES` : 'ALL PASS');
process.exit(fails ? 1 : 0);
