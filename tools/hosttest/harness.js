// Locating and running the native harness.
//
// Every script here shells out to the same binary. Before this they each did it
// their own way: most assumed the cwd was this directory, so they failed from
// the repo root; one hardcoded an absolute path to one machine.
'use strict';
const { execSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const DIR = __dirname;
const TEST = path.join(DIR, 'test');

function requireBuilt() {
  if (!fs.existsSync(TEST)) {
    console.error('tools/hosttest/test is missing. Build it first:\n' +
                  '  make host        # builds and runs the battery\n' +
                  '  sh tools/hosttest/build.sh');
    process.exit(1);
  }
}

// Raw stdout, as a string. opts is merged last so a caller can still pass
// encoding, stdio, a bigger maxBuffer, etc.
function run(args, opts) {
  requireBuilt();
  return execSync(`./test ${args}`,
    Object.assign({ cwd: DIR, maxBuffer: 1e9, encoding: 'utf8' }, opts));
}

// The common case: one number per line.
function runNumbers(args, opts) {
  return run(args, opts).trim().split('\n').map(Number);
}

// The chiff's duration in samples, straight from the driver's own report.
//
// report=1 prints to STDERR and returns early, so fold stderr in: capturing it
// via stdio returns stdout, which is null. Two of the three copies of this
// function shipped with that bug, which is why there is now one.
function chiffDurationSamples(args) {
  const out = run(`${args} report=1 2>&1`);
  const m = /chiff (\d+) smp/.exec(out);
  if (!m) throw new Error(`no chiff duration in report for: ${args}\n${out}`);
  return +m[1];
}

// Ask, do not hardcode. Cached: it costs a process.
let frameHzCache = 0;
function frameHz() {
  if (!frameHzCache) {
    const out = run('basic 32 64 report=1 2>&1');
    const m = /rate (\d+) Hz/.exec(out);
    if (!m) throw new Error(`driver reported no rate:\n${out}`);
    frameHzCache = +m[1];
  }
  return frameHzCache;
}

module.exports = {
  DIR, TEST, run, runNumbers, requireBuilt, chiffDurationSamples, frameHz,
};
