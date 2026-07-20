// Bit-exact regression: pins the envelope's output sample-for-sample.
//
// This is the tool the PERF work needs. State reduction and a hand-ASM render
// loop are both supposed to be output-identical refactors, and "sounds the
// same to me" cannot establish that. Any sample that moves fails here, with
// the case and the first differing index.
//
// Distinct from anomaly.js, which tracks one behavioural number per case and
// tolerates small movement -- that is for changes MEANT to alter behaviour.
// Use golden for refactors, anomaly for redesigns.
//
//   node golden.js              verify against the recorded vectors
//   node golden.js --update     re-record (only when output SHOULD change;
//                               say why, and what you listened to, in the commit)
'use strict';
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { execSync } = require('child_process');

const HERE = __dirname;
const VECTORS = path.join(HERE, 'golden_vectors.json');
const UPDATE = process.argv.includes('--update');

// Chosen to cover the paths that have actually broken: a chiff that outlives
// its stage, a window shorter than a block, the compressed release, hold
// stages, inverted ranges, and both ends of the amount range.
const CASES = [
  ['chiff outlives attack',   'basic 127 33 attack_setting=16'],
  ['default-ish',             'basic 96 90 attack_setting=40'],
  ['chiff off',               'basic 0 90 attack_setting=40'],
  ['amount 1 (floor case)',   'basic 1 90 attack_setting=40'],
  ['window under one block',  'basic 127 0 attack_setting=40'],
  ['longest window',          'basic 127 127 attack_setting=40'],
  ['fastest attack',          'basic 127 50 attack_setting=0'],
  ['slowest attack',          'basic 127 50 attack_setting=127'],
  ['early release',           'early_release 96 127'],
  ['retrigger',               'retrigger 96 90'],
  ['inverted range',          'inverted 96 90'],
  ['late-window release',     'latehang 96 127'],
  ['held into sustain',       'held 96 127'],
];
const COMMON = 'decay_setting=64 release_setting=64 sustain_setting=70 ' +
               'peak=100 gate=400 tail=400 range=32767';

function render(args) {
  const full = args.startsWith('basic') ? `${args} ${COMMON}` : args;
  return execSync(`./test ${full}`, { cwd: HERE, maxBuffer: 1e9 })
    .toString().trim().split('\n').map(Number);
}

// Full-trace digest catches any change; the sparse probe localises it without
// storing every sample.
const PROBE_STRIDE = 256;
function fingerprint(trace) {
  const buf = Buffer.alloc(trace.length * 2);
  for (let i = 0; i < trace.length; i++) buf.writeInt16LE(trace[i], i * 2);
  const probe = [];
  for (let i = 0; i < trace.length; i += PROBE_STRIDE) probe.push(trace[i]);
  return {
    n: trace.length,
    sha: crypto.createHash('sha1').update(buf).digest('hex').slice(0, 16),
    probe,
  };
}

const current = {};
for (const [name, args] of CASES) current[name] = fingerprint(render(args));

if (UPDATE) {
  fs.writeFileSync(VECTORS, JSON.stringify(current, null, 1) + '\n');
  console.log(`recorded ${CASES.length} vectors to ${path.basename(VECTORS)}`);
  process.exit(0);
}

if (!fs.existsSync(VECTORS)) {
  console.log(`no vectors at ${path.basename(VECTORS)} -- run with --update first`);
  process.exit(1);
}
const golden = JSON.parse(fs.readFileSync(VECTORS, 'utf8'));

let fails = 0;
for (const [name] of CASES) {
  const got = current[name], want = golden[name];
  if (!want) { console.log(`FAIL ${name}  [no recorded vector]`); fails++; continue; }
  if (got.sha === want.sha && got.n === want.n) { console.log(`PASS ${name}`); continue; }
  fails++;
  if (got.n !== want.n) {
    console.log(`FAIL ${name}  [length ${want.n} -> ${got.n}]`);
    continue;
  }
  let at = -1;
  for (let i = 0; i < Math.min(got.probe.length, want.probe.length); i++) {
    if (got.probe[i] !== want.probe[i]) { at = i * PROBE_STRIDE; break; }
  }
  console.log(`FAIL ${name}  [output changed` +
    (at >= 0 ? `, first near sample ${at} (${(at / 45).toFixed(0)}ms):` +
      ` ${want.probe[at / PROBE_STRIDE]} -> ${got.probe[at / PROBE_STRIDE]}`
      : ', within a probe stride') + ']');
}
console.log(fails ? `\n${fails} FAILURES -- output is not bit-identical` : '\nALL PASS');
process.exit(fails ? 1 : 0);
