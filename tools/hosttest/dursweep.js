// Sweep the CHIFF DURATION setting 0..127 and report per-duration chiff
// metrics. The user's decisive hardware finding is an overflow-like cliff at
// duration 20 -> 21 (185 -> 199 samples), so the metrics are sampled inside
// each setting's OWN window, not a fixed time span.
const { execSync } = require('child_process');
const fs = require('fs');

// Firmware LUT, parsed from resources.cc so the sweep and the firmware agree
// on how many samples each setting means.
function durationLut() {
  const src = fs.readFileSync('../../yarns/resources.cc', 'utf8');
  const start = src.indexOf('const uint32_t lut_chiff_duration_samples[] = {');
  const body = src.slice(src.indexOf('{', start) + 1, src.indexOf('};', start));
  return body.split(',').map(s => s.trim()).filter(Boolean).map(Number);
}
const LUT = durationLut();

// argv: amount, then any KEY=VALUE driver overrides (attack/decay/release/
// gate/peak/sustain), e.g. `node dursweep.js 96 attack=130 peak=75`.
const amount = process.argv[2] || '96';
const opts = process.argv.slice(3).filter(a => a.includes('=')).join(' ');

function run(dur) {
  const out = execSync(`./test basic ${amount} ${dur} ${opts}`, { maxBuffer: 1e9 });
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
  const w = LUT[d];
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
