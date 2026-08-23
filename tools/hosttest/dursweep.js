// Sweep the CHIFF DURATION setting 0..127 and report per-duration chiff
// metrics. Every metric is sampled inside the setting's OWN window: the length
// spans three orders of magnitude across the sweep, so a fixed span would
// measure the onset at one end and silence at the other.
const { execSync } = require('child_process');
const H = require('./harness');

// ASK THE ENGINE, do not parse a table: one derivation of the length, and it
// is the firmware's.
function windowSamples(dur) {
  return H.chiffWindowSamples(`report ${amount} ${dur} ${opts}`);
}

// argv: amount, then any KEY=VALUE driver overrides (attack/decay/release/
// gate/peak/sustain), e.g. `node dursweep.js 96 attack=130 peak=75`.
const amount = process.argv[2] || '96';
const opts = process.argv.slice(3).filter(a => a.includes('=')).join(' ');

function run(dur) {
  const out = H.run(`basic ${amount} ${dur} ${opts}`);
  return out.toString().trim().split('\n').map(Number);
}

// Mean absolute per-sample step over [a, b) samples -- the noise level.
function noise(s, a, b) {
  a = Math.max(1, a | 0); b = Math.min(b | 0, s.length);
  let sum = 0, n = 0;
  for (let i = a; i < b; i++) { sum += Math.abs(s[i] - s[i - 1]); n++; }
  return n ? sum / n : 0;
}
function mean(s, a, b) {
  a = Math.max(0, a | 0); b = Math.min(b | 0, s.length);
  let sum = 0, n = 0;
  for (let i = a; i < b; i++) { sum += s[i]; n++; }
  return n ? sum / n : 0;
}

console.log('dur  samples   onsetNoise   winNoise   totalStep   winMean   postMean');
const rows = [];
for (let d = 0; d <= 127; d++) {
  const s = run(d);
  const w = windowSamples(d);
  // Onset = first 64 samples (one audio block); window = the whole burst.
  const onset = noise(s, 1, Math.min(64, w));
  const win = noise(s, 1, w);
  // Total absolute motion injected by the burst -- the energy-like measure.
  let total = 0;
  for (let i = 1; i < Math.min(w, s.length); i++) total += Math.abs(s[i] - s[i - 1]);
  const wm = mean(s, 0, w);
  const post = mean(s, w, w + 2048);
  rows.push({ d, w, onset, win, total, wm, post });
  console.log(
    String(d).padStart(3) + String(w).padStart(9) +
    onset.toFixed(1).padStart(13) + win.toFixed(1).padStart(11) +
    total.toFixed(0).padStart(12) + wm.toFixed(0).padStart(10) +
    post.toFixed(0).padStart(10));
}

// Flag jumps: ratio of adjacent settings' onset noise well away from 1.
console.log('\nadjacent-setting onset-noise ratio outliers (|log2 ratio| > 0.5):');
let flagged = 0;
for (let i = 1; i < rows.length; i++) {
  const a = rows[i - 1].onset, b = rows[i].onset;
  if (a < 1 && b < 1) continue;
  const r = Math.log2((b + 1e-9) / (a + 1e-9));
  if (Math.abs(r) > 0.5) {
    flagged++;
    console.log(`  ${rows[i - 1].d} -> ${rows[i].d}: ${a.toFixed(1)} -> ${b.toFixed(1)}` +
      ` (${(Math.pow(2, r)).toFixed(2)}x, samples ${rows[i - 1].w} -> ${rows[i].w})`);
  }
}
if (!flagged) console.log('  none');
