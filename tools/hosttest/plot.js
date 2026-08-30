// Plot host-harness traces (the REAL envelope.cc) as a PNG so the SHAPE can be
// looked at instead of summarised. Scalar stats have repeatedly failed to see
// what the user hears; this exists to put the waveform on screen.
//
// Usage: node plot.js <out.png> <spanMs> "<label>:<driver args>" ...
const { execSync } = require('child_process');
const fs = require('fs');

const outPng = process.argv[2];
const spanMs = parseFloat(process.argv[3]);
// Without this, no arguments wrote a file literally named "undefined".
if (!outPng || !(spanMs > 0) || process.argv.length < 5) {
  console.error('usage: plot.js <out.png> <spanMs> "<driver args>" [more series...]');
  process.exit(1);
}
const series = process.argv.slice(4).map(s => {
  const i = s.indexOf(':');
  return { label: s.slice(0, i), args: s.slice(i + 1) };
});

const FS = require('./harness').frameHz();
const W = 1400, H = 620, PAD_L = 70, PAD_B = 40, PAD_T = 30, PAD_R = 20;
const N = Math.round(spanMs * FS / 1000);

// Distinguishable in both light and dark, and colour-blind safe.
const COLORS = [[80, 170, 255], [255, 150, 60], [120, 220, 120], [230, 100, 200]];

const traces = series.map(s => {
  const raw = require('./harness').run(s.args)
    .toString().trim().split('\n').map(Number);
  return raw.slice(0, N);
});

let hi = 1;
for (const t of traces) for (const v of t) if (v > hi) hi = v;
hi = Math.ceil(hi / 1000) * 1000;

const px = Buffer.alloc(W * H * 3, 22);   // dark background
const put = (x, y, c) => {
  if (x < 0 || y < 0 || x >= W || y >= H) return;
  const o = (y * W + x) * 3;
  px[o] = c[0]; px[o + 1] = c[1]; px[o + 2] = c[2];
};
const plotW = W - PAD_L - PAD_R, plotH = H - PAD_T - PAD_B;
const xOf = i => PAD_L + Math.round(i / (N - 1) * (plotW - 1));
const yOf = v => PAD_T + Math.round((1 - v / hi) * (plotH - 1));

// axes + gridlines every 5ms and every 25% of range
const GRID = [55, 55, 60], AXIS = [150, 150, 155];
for (let ms = 0; ms <= spanMs; ms += 5) {
  const x = xOf(Math.round(ms * FS / 1000));
  for (let y = PAD_T; y < PAD_T + plotH; y++) put(x, y, GRID);
}
for (let f = 0; f <= 4; f++) {
  const y = yOf(hi * f / 4);
  for (let x = PAD_L; x < PAD_L + plotW; x++) put(x, y, GRID);
}
for (let y = PAD_T; y < PAD_T + plotH; y++) put(PAD_L - 1, y, AXIS);
for (let x = PAD_L - 1; x < PAD_L + plotW; x++) put(x, PAD_T + plotH, AXIS);

// min/max decimation per column, so fast noise shows as a band not aliasing
traces.forEach((t, s) => {
  const c = COLORS[s % COLORS.length];
  for (let x = 0; x < plotW; x++) {
    const i0 = Math.floor(x / plotW * t.length);
    const i1 = Math.max(i0 + 1, Math.floor((x + 1) / plotW * t.length));
    let lo = Infinity, up = -Infinity;
    for (let i = i0; i < i1 && i < t.length; i++) {
      if (t[i] < lo) lo = t[i];
      if (t[i] > up) up = t[i];
    }
    if (lo === Infinity) continue;
    for (let y = yOf(up); y <= yOf(lo); y++) put(PAD_L + x, y, c);
  }
  // legend swatch
  for (let dy = 0; dy < 10; dy++) for (let dx = 0; dx < 24; dx++)
    put(PAD_L + 12 + dx, 8 + dy + s * 14, c);
});

const ppm = `P6\n${W} ${H}\n255\n`;
fs.writeFileSync('/tmp/_plot.ppm', Buffer.concat([Buffer.from(ppm), px]));
execSync(`sips -s format png /tmp/_plot.ppm --out ${outPng} >/dev/null 2>&1`);
console.log(`wrote ${outPng}  span ${spanMs}ms (${N} samples), y max ${hi}`);
series.forEach((s, i) => console.log(
  `  swatch ${i + 1} (top to bottom): ${s.label}  [${s.args}]`));
