// DOES THE CHIFF DIE WHEN DURATION SAYS IT SHOULD? (L3, and L8 behind it.)
//
// Die-out is the last instant the chiff term's rms is still above the engine's
// own audibility level, as a multiple of the nominal window. 1.00 is the law.
//
// THE GATE IS A CEILING ON THE ANSWER, and it does not announce itself.
// Trigger() speeds the walk to finish by the end of the RELEASE STAGE, so a
// ratio measured under a gate of Nx the window CANNOT EXCEED ~N -- the note
// ends and takes the chiff with it. Every low-AMOUNT figure the plan carried
// was censored that way: what read ">= 3.00, render ceiling" was in fact a
// chiff that does not die at all inside a held note. Any cell whose die-out
// lands near the gate is printed as >=, never as a number.
//
// ONE SEED IS NOT A RESULT: the amounts near the audibility threshold are
// exactly where one realization lies worst, so this runs eight and prints the
// range.
//
// ONE ATTACK, NOT TWO. The window has been an absolute time off its own table
// since 028d10d9, and at this gate the release deadline never binds, so ENV
// ATTACK 40 and 72 were MEASURED identical in every cell -- six duplicate rows.
const H = require('./harness');
const FS = H.frameHz(), BLOCK = 64;
// The engine holds the SCALED rms (2.121 sigma) to kChiffInaudibleLevel, so the
// sigma at that point is 2.121x lower. Compare like with like.
const SIGMA_THRESH = 32767 * Math.pow(10, -48.2 / 20) / 2.121;
const WIN = 24;                       // blocks per rms estimate
const ATK = 40;
const GATE_WINDOWS = 3;               // the ceiling this tool can see past
const CENSORED_AT = 0.95 * GATE_WINDOWS;
const SEEDS = 8;
const DURATIONS = [64, 110];
// The low end is the point: the defect lives between AMOUNT 1 (below the
// audibility line at its onset, so it never registers) and AMOUNT 8.
const AMOUNTS = [1, 2, 3, 4, 8, 16, 32, 127];

function windowMs(dur) {
  return H.chiffWindowSamples(`basic 32 ${dur} attack_setting=${ATK}`) / FS * 1000;
}
function dieoutRatio(amt, dur, seed, windowMs_) {
  const gateMs = windowMs_ * GATE_WINDOWS;
  const v = H.runNumbers(
    `basic ${amt} ${dur} attack_setting=${ATK} seed=${seed} ` +
    `gate=${Math.round(gateMs)} tail=${Math.round(windowMs_ * 0.25)} chiff_state_trace=1`);
  let last = 0;
  for (let i = 0; i + WIN <= v.length; ++i) {
    let s = 0; for (let j = i; j < i + WIN; ++j) s += v[j] * v[j];
    if (Math.sqrt(s / WIN) > SIGMA_THRESH) last = i + WIN;
  }
  return last * BLOCK / FS * 1000 / windowMs_;
}

console.log(`ENV ATTACK ${ATK}, ${SEEDS} seeds, gate ${GATE_WINDOWS}x the window`);
console.log('die-out / nominal window; 1.00 is the law, >= means the gate cut it off\n');
console.log(['DUR', 'AMT', 'window ms', 'mean', 'range'].map((s, i) =>
  s.padStart(i < 2 ? 6 : i === 2 ? 11 : 9)).join(''));
for (const dur of DURATIONS) {
  const w = windowMs(dur);
  for (const amt of AMOUNTS) {
    const rs = [];
    for (let seed = 0; seed < SEEDS; ++seed) rs.push(dieoutRatio(amt, dur, seed, w));
    const mean = rs.reduce((a, b) => a + b, 0) / rs.length;
    const lo = Math.min(...rs), hi = Math.max(...rs);
    const censored = hi >= CENSORED_AT;
    console.log(
      String(dur).padStart(6) + String(amt).padStart(6) + w.toFixed(1).padStart(11) +
      ((censored ? '>=' : '') + mean.toFixed(2)).padStart(9) +
      `${lo.toFixed(2)}..${hi.toFixed(2)}`.padStart(15) +
      (censored ? '   GATE-CENSORED' : ''));
  }
}
console.log('\nAMOUNT 1 reads 0.00 because its onset sits BELOW the audibility');
console.log('line, so the detector never fires. That is not silence.');
