// THE HOST BATTERY, OVER MANY REALIZATIONS.
//
// analyze.js renders ONE noise sequence. Most of its checks are structural and
// do not care -- but the statistical ones are a single draw against a fixed
// limit, and a limit calibrated on one draw is calibrated on luck. MEASURED
// 2026-08-19: `peak overshoot is bounded` sat at 15%, seed 0 reads 9.8%, and 15
// of 128 seeds exceed 15%. The battery had been green on that check for its
// whole life while failing an eighth of the time.
//
// So: run the file once per seed, and report per check
//   - how many seeds passed,
//   - the seed-0 figure and the range across seeds.
// A check whose range is a single value is seed-independent, which is worth
// seeing: it says the limit is not a statistical one and needs no seeds.
//
// EIGHT IS THE FLOOR. Per-seed spread at short duration is ~2x, the same size
// as the effects being measured.
'use strict';
const { spawnSync } = require('child_process');
const path = require('path');

const SEEDS = Number(process.env.SEEDS || process.argv[2] || 8);
const ANALYZE = path.join(__dirname, 'analyze.js');

// name -> { verdicts: [], values: [] }; insertion order is the file's order.
const checks = new Map();
const seedFailures = [];

for (let seed = 0; seed < SEEDS; ++seed) {
  const r = spawnSync(process.execPath, [ANALYZE], {
    env: Object.assign({}, process.env, { CHIFF_SEED: String(seed) }),
    encoding: 'utf8', maxBuffer: 1e9,
  });
  if (r.error) throw r.error;
  const lines = (r.stdout || '').split('\n');
  let sawAny = false;
  for (const line of lines) {
    const m = /^(PASS|FAIL) (.*?)(?:  \[(.*)\])?$/.exec(line);
    if (!m) continue;
    sawAny = true;
    const [, verdict, name, detail] = m;
    if (!checks.has(name)) checks.set(name, { verdicts: [], values: [] });
    const c = checks.get(name);
    c.verdicts.push(verdict);
    // The leading number of the detail is the figure the limit is about.
    const num = detail && /-?\d+\.?\d*(e[-+]?\d+)?/.exec(detail);
    c.values.push(num ? Number(num[0]) : null);
  }
  if (!sawAny) {
    console.error(`seed ${seed}: analyze.js produced no checks\n${r.stdout}\n${r.stderr}`);
    process.exit(2);
  }
  if (r.status !== 0) seedFailures.push(seed);
}

function fmt(v) {
  if (v === null) return '';
  return Math.abs(v) >= 1000 || (v !== 0 && Math.abs(v) < 0.01)
    ? v.toPrecision(3) : String(Number(v.toFixed(3)));
}

let broken = 0;
const pad = (s, n) => String(s).padStart(n);
console.log(`host battery over ${SEEDS} seeds\n`);
console.log('  seeds  check'.padEnd(65) + 'seed 0   range across seeds');
for (const [name, c] of checks) {
  const passes = c.verdicts.filter((v) => v === 'PASS').length;
  const ok = passes === c.verdicts.length;
  if (!ok) broken++;
  const nums = c.values.filter((v) => v !== null);
  const lo = nums.length ? Math.min(...nums) : null;
  const hi = nums.length ? Math.max(...nums) : null;
  const spread = !nums.length ? ''
    : lo === hi ? 'seed-independent'
    : `${fmt(lo)} .. ${fmt(hi)}`;
  console.log(`${ok ? '  ' : '! '}${pad(passes + '/' + c.verdicts.length, 5)}  ` +
    name.padEnd(56) + pad(nums.length ? fmt(c.values[0]) : '', 8) + '   ' + spread);
}

console.log();
if (broken) {
  console.log(`${broken} CHECK(S) FAILED ON AT LEAST ONE SEED (seeds ${seedFailures.join(', ')})`);
  console.log('Re-run one of them with:  CHIFF_SEED=<n> node tools/hosttest/analyze.js');
  process.exit(1);
}
console.log(`ALL PASS on all ${SEEDS} seeds`);
