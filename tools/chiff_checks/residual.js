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
//           slow slew puts nearly all its energy in OFFSET, so reading
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
// time axis spans the whole note, not the chiff's duration: the note is what a
// player hears, and the chiff stopping inside it is the thing being judged.
//
// CAVEAT: OFFSET is only trustworthy once WANDER has fallen well below it. A
// 20 ms block cannot average out a wander slower than itself, so while the two
// are comparable -- roughly the first half of the chiff -- the offset column is
// confounded by the wander and should not be read as a standing error.
//
// ONE REALIZATION CANNOT CARRY ANY OF THIS. Offset, wander and total are all
// statistics of a noise process, and the columns are read to the tenth of a dB.
// Every figure below is pooled over SEEDS realizations; the RANGE column is the
// spread of `total` across them, which is what says whether a reading is a
// property of the engine or of one draw.
//
// Usage: node residual.js [amount] [attack] [chiffDuration] [gateMs] [tailMs]
//        SEEDS=n to change the count (default 8)
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
    amount, maxTarget: FULL,
    gateSamples: Math.round(gateMs * FS / 1000),
    tailSamples: Math.round(tailMs * FS / 1000),
  };
  // Distinct raw PRNG seeds; the engine takes the seed value directly.
  const SEEDS = Number(process.env.SEEDS || 8);
  const seedFor = (i) => (0xCAFEBABE + i * 0x9E3779B9) >>> 0;
  const wets = [], drys = [];
  let r = null;
  for (let i = 0; i < SEEDS; ++i) {
    const seeded = Object.assign({}, p, { seed: seedFor(i) });
    const run = E.render(seeded);
    if (!r) r = run;
    wets.push(run.out);
    drys.push(E.render(Object.assign({}, seeded, { amount: 0 })).out);
  }
  const wet = wets[0], dry = drys[0], W = r.meta.chiffWindowSamples;
  const n = Math.min(...wets.map((w) => w.length), ...drys.map((d) => d.length));

  const dbfs = v => v > 0 ? 20 * Math.log10(v / FULL) : -999;
  const BLOCK = Math.round(0.02 * FS);   // 20 ms, independent of the duration

  console.log(`AMOUNT ${amount}  ENV ATTACK ${attack}  EXCITER DURATION ${chiffDuration}`);
  console.log(`duration ${(W / FS * 1000).toFixed(0)} ms, gate ${gateMs} ms, ` +
              `full scale ${FULL}\n`);
  console.log(`pooled over ${SEEDS} seeds; RANGE is the spread of total across them\n`);
  console.log('    t(ms)   offset(LSB)  offset(dBFS)  wander(dBFS)   total(dBFS)  range(dB)   note');
  for (let b = 0; b * BLOCK < n; b++) {
    const lo = b * BLOCK, hi = Math.min(n, lo + BLOCK);
    const t = lo / FS * 1000;
    const nearEdge = Math.abs(lo - W) < 2 * BLOCK;
    const nearGate = Math.abs(lo - p.gateSamples) < 2 * BLOCK;
    if (b % 5 && !nearEdge && !nearGate) continue;
    // Per seed, then averaged: a mean of magnitudes, not a magnitude of means,
    // so cancellation between realizations cannot hide an offset.
    let meanSum = 0, sdSum = 0, totalSum = 0;
    let totalLo = Infinity, totalHi = -Infinity;
    for (let s = 0; s < SEEDS; ++s) {
      let sum = 0;
      for (let i = lo; i < hi; i++) sum += wets[s][i] - drys[s][i];
      const mean = sum / (hi - lo);
      let sq = 0;
      for (let i = lo; i < hi; i++) { const d = (wets[s][i] - drys[s][i]) - mean; sq += d * d; }
      const sd = Math.sqrt(sq / (hi - lo));
      // sqrt(offset^2 + wander^2): the excursion, which is neither column alone.
      const total = Math.sqrt(mean * mean + sd * sd);
      meanSum += Math.abs(mean); sdSum += sd; totalSum += total;
      const totalDb = dbfs(total);
      if (totalDb < totalLo) totalLo = totalDb;
      if (totalDb > totalHi) totalHi = totalDb;
    }
    const note = nearEdge ? '<- chiff end' : nearGate ? '<- gate off' : '';
    console.log(`  ${t.toFixed(0).padStart(6)}  ${(meanSum / SEEDS).toFixed(1).padStart(11)}  ` +
                `${dbfs(meanSum / SEEDS).toFixed(1).padStart(12)}  ` +
                `${dbfs(sdSum / SEEDS).toFixed(1).padStart(12)}  ` +
                `${dbfs(totalSum / SEEDS).toFixed(1).padStart(12)}  ` +
                `${(totalHi - totalLo).toFixed(1).padStart(9)}   ${note}`);
  }
}).catch(e => { console.error(e); process.exit(1); });
