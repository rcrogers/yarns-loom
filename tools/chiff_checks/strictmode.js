// Pre-publish gate: the page must boot and RENDER under strict mode.
//
// The published artifact runs the page's scripts strict. The other sim checks
// load them sloppy, which once let a strict-only error ship -- the artifact
// came up with blank graphs while every local check passed. Run this before
// republishing.
'use strict';
const { loadPage } = require('./page');

let fails = 0;
const check = (name, ok, detail) => {
  console.log(`${ok ? 'PASS' : 'FAIL'} ${name}${detail ? '  [' + detail + ']' : ''}`);
  if (!ok) fails++;
};

const PARAMS = {
  attack: 40, decay: 64, sustain: 70, release: 64,
  amplitudeModVelocity: 0, velocity: 127,
  amount: 127, chiffDuration: 90,
  gateMs: 400, tailMs: 400, seed: 0xCAFEBABE,
};

loadPage(process.argv[2], { strict: true }).then(page => {
  check('page boots under strict mode', !!page.ENGINE && page.FS === 45000,
    `kFrameHz ${page.FS}`);

  // Booting is not enough: a strict-only failure inside render() is exactly
  // what produced blank graphs, so demand real samples out the other end.
  const res = page.render(PARAMS);
  const nonZero = res.out.reduce((n, v) => n + (v !== 0 ? 1 : 0), 0);
  check('render() produces samples under strict mode',
    res.out.length > 0 && nonZero > res.out.length / 10,
    `${nonZero} non-zero of ${res.out.length}`);

  console.log(fails ? `\n${fails} FAILURES` : '\nALL PASS');
  process.exit(fails ? 1 : 0);
}).catch(err => {
  console.error('strict-mode load/render threw:');
  console.error(err);
  process.exit(1);
});
