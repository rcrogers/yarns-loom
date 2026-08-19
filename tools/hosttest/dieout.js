// DOES THE CHIFF DIE WHEN DURATION SAYS IT SHOULD? (L3.)
//
// Die-out is when the chiff term's level falls below the engine's own
// audibility line, as a multiple of the nominal window. 1.00 is the law.
//
// DO NOT ASK "WHEN DID A RUN LAST EXCEED THE LINE". That is a max statistic on
// a noise process: it is biased late by construction, and worst exactly where
// the tail is shallow. MEASURED 2026-08-19, against the pooled crossing below:
// it inflates by 1.02x at AMOUNT 127 and 3.83x at AMOUNT 3, which is the whole
// of the "low-amount overrun" this project chased as an engine defect.
//
// POOL THE SEEDS FIRST, THEN CROSS. Averaging the level across seeds at each
// instant gives a smooth curve whose crossing is a property of the engine
// rather than of one realization.
//
// THE AVERAGING WINDOW MUST SCALE WITH THE CHIFF'S OWN TIME CONSTANT, or the
// pooled curve is still noisy enough that a late excursion reads as the
// crossing -- the same defect one rung up. MEASURED: a fixed 24-block window
// over 8 seeds put AMOUNT 3 at 1.46 where a window scaled to the setting puts
// it at 0.66.
//
// THE GATE IS A CEILING ON THE ANSWER. Trigger() speeds the walk to finish by
// the end of the RELEASE STAGE, so a ratio measured under a gate of Nx the
// window cannot exceed ~N. Cells that reach the gate are marked, not printed.
const H = require('./harness');
const FS = H.frameHz(), BLOCK = 64;
// The engine holds the SCALED rms (2.121 sigma) to kChiffInaudibleLevel, so the
// sigma at that point is 2.121x lower. Compare like with like.
const LINE = 32767 * Math.pow(10, -48.2 / 20) / 2.121;
const TAIL_DB = 10;                   // how far below the line the tail column asks for
const SMOOTH_FRACTION = 0.05;         // averaging half-width, as a fraction of the window
const ATK = 40;
const GATE_WINDOWS = 9;
const SEEDS = 16;
const DURATIONS = [64, 110];
const AMOUNTS = [1, 2, 3, 4, 8, 16, 32, 127];

function windowMs(dur) {
  return H.chiffWindowSamples(`basic 32 ${dur} attack_setting=${ATK}`) / FS * 1000;
}
// The pooled level at one instant, over all seeds, averaged over a half-width
// that scales with the setting.
function levelAt(runs, centreBlock, halfBlocks) {
  const lo = Math.max(0, Math.round(centreBlock - halfBlocks));
  const hi = Math.round(centreBlock + halfBlocks);
  let acc = 0, count = 0;
  for (const v of runs) for (let i = lo; i < hi && i < v.length; ++i) { acc += v[i] * v[i]; ++count; }
  return count ? Math.sqrt(acc / count) : 0;
}
// First instant at which the pooled level is below `level`. The curve is smooth
// at this half-width, so first-below and last-above agree.
function crossing(runs, wBlk, level) {
  const half = Math.max(12, SMOOTH_FRACTION * wBlk);
  if (levelAt(runs, half, half) <= level) return null;   // never above
  for (let m = 0.02; m < GATE_WINDOWS; m += 0.02) {
    if (levelAt(runs, m * wBlk, half) < level) return m;
  }
  return GATE_WINDOWS;
}
function render(amt, dur, windowMs_) {
  const runs = [];
  for (let seed = 0; seed < SEEDS; ++seed) {
    runs.push(H.runNumbers(
      `basic ${amt} ${dur} attack_setting=${ATK} seed=${seed} ` +
      `gate=${Math.round(windowMs_ * GATE_WINDOWS)} ` +
      `tail=${Math.round(windowMs_ * 0.3)} chiff_state_trace=1`));
  }
  return runs;
}

console.log(`ENV ATTACK ${ATK}, ${SEEDS} seeds pooled, gate ${GATE_WINDOWS}x the window`);
console.log('crossing: where the level leaves the audibility line, / nominal window (1.00 is the law)');
console.log(`tail:     where it reaches ${TAIL_DB} dB below that line -- how cleanly it ends\n`);
console.log(['DUR', 'AMT', 'window ms', 'crossing', 'tail'].map((s, i) =>
  s.padStart(i < 2 ? 6 : i === 2 ? 11 : 12)).join(''));
for (const dur of DURATIONS) {
  const w = windowMs(dur);
  const wBlk = w * FS / 1000 / BLOCK;
  for (const amt of AMOUNTS) {
    const runs = render(amt, dur, w);
    const cell = (level) => {
      const m = crossing(runs, wBlk, level);
      if (m === null) return 'never above';
      return m >= GATE_WINDOWS ? `>=${GATE_WINDOWS} GATED` : m.toFixed(2);
    };
    console.log(String(dur).padStart(6) + String(amt).padStart(6) +
      w.toFixed(1).padStart(11) + cell(LINE).padStart(12) +
      cell(LINE * Math.pow(10, -TAIL_DB / 20)).padStart(12));
  }
}
console.log('\n"never above" is the detector not firing, not silence: that setting\'s');
console.log('chiff sits below the audibility line for its whole life.');
