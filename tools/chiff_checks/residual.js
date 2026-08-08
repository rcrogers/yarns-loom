// Splits the chiff's effect into the two things that must not be confused:
//
//   OFFSET  the per-block mean of (chiff - nominal value). A standing error:
//           the value sitting off its nominal value. This is the failure the
//           slew-rate floor exists to prevent, and it is what a frozen value
//           looks like.
//   WANDER  the per-block STANDARD DEVIATION of the same. The excursion, i.e.
//           what is actually audible as chiff.
//   TOTAL   RMS about zero, which is sqrt(offset^2 + wander^2). Printed
//           because NEITHER of the other two is the excursion on its own: a
//           slow filter puts nearly all its energy in OFFSET, so reading
//           WANDER alone reports a chiff that is plainly there as gone. That
//           mistake, made with a std in place of an RMS, produced a 30 dB
//           error at the slowest setting measured and sent a whole session
//           chasing a phantom in ChiffScaledRmsPerInput.
//
// RMS of the residual -- what the earlier probes reported -- is the two added
// in quadrature and cannot distinguish them: a frozen offset and an equal
// wander give the same number. Any claim about whether the chiff LANDS, as
// opposed to merely getting quiet, needs this split.
//
// Levels are ABSOLUTE (dB relative to full scale), not normalized to the
// onset, so "inaudible" is a fixed threshold rather than a relative one. The
// time axis spans the whole note, not the chiff window, because the window is
// an implementation detail that a proposed design removes.
//
// CAVEAT: OFFSET is only trustworthy once WANDER has fallen well below it. A
// 20 ms block cannot average out a wander slower than itself, so while the two
// are comparable -- roughly the first half of the chiff -- the offset column is
// confounded by the wander and should not be read as a standing error.
//
// Usage: node residual.js [amount] [attack] [chiffDuration] [gateMs] [tailMs]
'use strict';
const { loadPage } = require('./page');

const amount = +(process.argv[2] || 96);
const attack = +(process.argv[3] || 64);
const chiffDuration = +(process.argv[4] || 64);
const gateMs = +(process.argv[5] || 3000);
const tailMs = +(process.argv[6] || 600);

loadPage().then(page => {
  const E = page.ENGINE, FS = page.FS, FULL = page.PEAK;
  const p = {
    attack, decay: 64, sustain: 70, release: 64,
    amplitudeModVelocity: 0, velocity: 100, chiffDuration,
    envModAttack: 0, envModDecay: 0, envModSustain: 0, envModRelease: 0,
    amount, maxTarget: FULL, seed: 0xCAFEBABE,
    gateSamples: Math.round(gateMs * FS / 1000),
    tailSamples: Math.round(tailMs * FS / 1000),
  };
  const r = E.render(p);
  const dry = E.render(Object.assign({}, p, { amount: 0 })).out;
  const wet = r.out, W = r.meta.chiffWindowSamples;
  const n = Math.min(wet.length, dry.length);

  const dbfs = v => v > 0 ? 20 * Math.log10(v / FULL) : -999;
  const BLOCK = Math.round(0.02 * FS);   // 20 ms, independent of the window

  console.log(`AMOUNT ${amount}  ENV ATTACK ${attack}  EXCITER DURATION ${chiffDuration}`);
  console.log(`window ${(W / FS * 1000).toFixed(0)} ms, gate ${gateMs} ms, ` +
              `full scale ${FULL}\n`);
  console.log('    t(ms)   offset(LSB)  offset(dBFS)  wander(dBFS)   total(dBFS)   note');
  for (let b = 0; b * BLOCK < n; b++) {
    const lo = b * BLOCK, hi = Math.min(n, lo + BLOCK);
    let sum = 0;
    for (let i = lo; i < hi; i++) sum += wet[i] - dry[i];
    const mean = sum / (hi - lo);
    let sq = 0;
    for (let i = lo; i < hi; i++) { const d = (wet[i] - dry[i]) - mean; sq += d * d; }
    const sd = Math.sqrt(sq / (hi - lo));
    // sqrt(offset^2 + wander^2): the excursion, which is neither column alone.
    const total = Math.sqrt(mean * mean + sd * sd);
    const t = lo / FS * 1000;
    // Only print a readable subset: every 5th block, plus around the window
    // edge and the gate release, where the interesting transitions are.
    const nearEdge = Math.abs(lo - W) < 2 * BLOCK;
    const nearGate = Math.abs(lo - p.gateSamples) < 2 * BLOCK;
    if (b % 5 && !nearEdge && !nearGate) continue;
    const note = nearEdge ? '<- window edge' : nearGate ? '<- gate off' : '';
    console.log(`  ${t.toFixed(0).padStart(6)}  ${mean.toFixed(1).padStart(11)}  ` +
                `${dbfs(Math.abs(mean)).toFixed(1).padStart(12)}  ` +
                `${dbfs(sd).toFixed(1).padStart(12)}  ` +
                `${dbfs(total).toFixed(1).padStart(12)}   ${note}`);
  }
}).catch(e => { console.error(e); process.exit(1); });
