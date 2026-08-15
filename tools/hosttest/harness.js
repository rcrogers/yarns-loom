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

module.exports = { DIR, TEST, run, runNumbers, requireBuilt };
