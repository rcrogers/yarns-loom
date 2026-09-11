// Compares warpgolden's per-shape hashes against the recorded map.
// `--update` re-records; do that only when a map SHOULD have moved.
const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');
const dir = __dirname;
const FILE = path.join(dir, 'warp_golden.json');
const UPDATE = process.argv.includes('--update');

const out = execFileSync(path.join(dir, 'warpgolden'), { encoding: 'utf8' });
const now = {};
for (const line of out.trim().split('\n')) {
  const [shape, hash] = line.trim().split(/\s+/);
  now[shape] = hash;
}
if (UPDATE) {
  fs.writeFileSync(FILE, JSON.stringify({ shapes: now }, null, 2) + '\n');
  console.log(`recorded ${Object.keys(now).length} warps to warp_golden.json`);
  process.exit(0);
}
if (!fs.existsSync(FILE)) {
  console.error('no warp_golden.json; run with --update to record one');
  process.exit(1);
}
const was = JSON.parse(fs.readFileSync(FILE, 'utf8')).shapes;
let failures = 0;
for (const shape of Object.keys(now)) {
  if (was[shape] !== now[shape]) {
    console.log(`FAIL shape ${shape}: ${was[shape]} -> ${now[shape]}`);
    ++failures;
  }
}
console.log(failures
  ? `\n${failures} warp(s) changed -- re-record with --update only if the map ` +
    `SHOULD have moved. The oscillator golden cannot see this: it renders with ` +
    `warp=0.`
  : `PASS ${Object.keys(now).length} warps answer as recorded`);
process.exit(failures ? 1 : 0);
