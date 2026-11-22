// DOES THE CHIFF DIE WHEN DURATION SAYS IT SHOULD? (L3.)
//
// Die-out is when the chiff term's level falls below the engine's own
// audibility line, as a multiple of the nominal duration. 1.00 means it dies
// exactly when DURATION says it will, which is what makes the setting read true.
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
// crossing -- the same defect one rung up. MEASURED: a fixed 24-block span
// over 8 seeds put AMOUNT 3 at 1.46 where a span scaled to the setting puts
// it at 0.66.
//
// THE GATE IS A CEILING ON THE ANSWER. Trigger() speeds the decay to finish by
// the end of the RELEASE STAGE, so a ratio measured under a gate of Nx the
// duration cannot exceed ~N. Cells that reach the gate are marked, not printed.
const H = require('./harness');
const FS = H.frameHz(), BLOCK = 64;
// The engine holds the chiff's AMPLITUDE to kChiffInaudibleDbFs, and that
// amplitude is kChiffAmplitudeSigmas of the noise's sigma -- so the sigma at
// that point is 2.121x lower. Compare like with like.
//   - Both figures are the firmware's, copied. envelope.cc is the source: they
//     are kChiffInaudibleDbFs and kChiffAmplitudeSigmas = 3/sqrt(2).
const INAUDIBLE_DBFS = -48.2;
const AMPLITUDE_SIGMAS = 3 / Math.SQRT2;
const LINE = 32767 * Math.pow(10, INAUDIBLE_DBFS / 20) / AMPLITUDE_SIGMAS;
const TAIL_DB = 10;                   // how far below the line the tail column asks for
const SMOOTH_FRACTION = 0.05;         // averaging half-width, as a fraction of the duration
const ATK = 40;
const GATE_DURATIONS = 9;
const SEEDS = 16;
const DURATIONS = [64, 110];
const AMOUNTS = [1, 2, 3, 4, 8, 16, 32, 127];

function durationMs(dur) {
  return H.chiffDurationSamples(`basic 32 ${dur} attack_setting=${ATK}`) / FS * 1000;
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
  for (let m = 0.02; m < GATE_DURATIONS; m += 0.02) {
    if (levelAt(runs, m * wBlk, half) < level) return m;
  }
  return GATE_DURATIONS;
}
function render(amt, dur, duration_ms) {
  const runs = [];
  for (let seed = 0; seed < SEEDS; ++seed) {
    runs.push(H.runNumbers(
      `basic ${amt} ${dur} attack_setting=${ATK} seed=${seed} ` +
      `gate=${Math.round(duration_ms * GATE_DURATIONS)} ` +
      `tail=${Math.round(duration_ms * 0.3)} chiff_state_trace=1`));
  }
  return runs;
}

console.log(`ENV ATTACK ${ATK}, ${SEEDS} seeds pooled, gate ${GATE_DURATIONS}x the duration`);
console.log('crossing: where the level leaves the audibility line, / nominal duration (1.00 is where it should land)');
console.log(`tail:     where it reaches ${TAIL_DB} dB below that line -- how cleanly it ends\n`);
console.log(['DUR', 'AMT', 'duration ms', 'crossing', 'tail'].map((s, i) =>
  s.padStart(i < 2 ? 6 : i === 2 ? 11 : 12)).join(''));
for (const dur of DURATIONS) {
  const w = durationMs(dur);
  const wBlk = w * FS / 1000 / BLOCK;
  for (const amt of AMOUNTS) {
    const runs = render(amt, dur, w);
    const cell = (level) => {
      const m = crossing(runs, wBlk, level);
      if (m === null) return 'never above';
      return m >= GATE_DURATIONS ? `>=${GATE_DURATIONS} GATED` : m.toFixed(2);
    };
    console.log(String(dur).padStart(6) + String(amt).padStart(6) +
      w.toFixed(1).padStart(11) + cell(LINE).padStart(12) +
      cell(LINE * Math.pow(10, -TAIL_DB / 20)).padStart(12));
  }
}
console.log('\n"never above" is the detector not firing, not silence: that setting\'s');
console.log('chiff sits below the audibility line for its whole life.');
