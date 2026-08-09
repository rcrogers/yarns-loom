const { execSync } = require('child_process');
const T = '/Users/rcrogers/Repos/mutable-instruments/mutable-dev-environment/eurorack-modules/tools/hosttest/test';
const FS = 45000, BLOCK = 64;
// The engine holds the SCALED rms (2.121 sigma) to kChiffInaudibleLevel, so the
// sigma at that point is 2.121x lower. Compare like with like.
const SIGMA_THRESH = 32767 * Math.pow(10, -48.2 / 20) / 2.121;
const WIN = 24;                       // blocks per rms estimate
function windowMs(dur, atk) {
  const o = execSync(`${T} basic 32 ${dur} attack_setting=${atk} report=1 2>&1`, { encoding: 'utf8' });
  return Number(o.match(/chiff (\d+) smp/)[1]) / FS * 1000;
}
function dieoutMs(amt, dur, atk, gateMs) {
  const v = execSync(
    `${T} basic ${amt} ${dur} attack_setting=${atk} gate=${Math.round(gateMs)} tail=${Math.round(gateMs*2)} chiff_state_trace=1`,
    { encoding: 'utf8', maxBuffer: 1 << 28 }).trim().split('\n').map(Number);
  let last = 0;
  for (let i = 0; i + WIN <= v.length; ++i) {
    let s = 0; for (let j = i; j < i + WIN; ++j) s += v[j] * v[j];
    if (Math.sqrt(s / WIN) > SIGMA_THRESH) last = i + WIN;
  }
  return last * BLOCK / FS * 1000;
}
console.log(['ATK','DUR','AMT','window ms','ratio'].map(s=>s.padStart(10)).join(''));
let sum=0,n=0;
for (const atk of [40, 72]) for (const dur of [64, 110]) for (const amt of [8, 32, 127]) {
  const w = windowMs(dur, atk);
  const r = dieoutMs(amt, dur, atk, w * 3) / w;
  sum+=r; n++;
  console.log([atk,dur,amt,w.toFixed(1),r.toFixed(2)].map(s=>String(s).padStart(10)).join(''));
}
console.log('\nmean %s   (plan records 0.96-1.07 for this build)', (sum/n).toFixed(2));
