// Noise band vs CHIFF DURATION, in front-panel settings.
//
// CAVEAT, read before trusting a number here: peak-to-peak over a slice cannot
// separate chiff noise from ordinary envelope motion. A slice containing the
// attack reads near 100% with no noise at all. This metric misled once already
// (it produced a "wall of noise" reading that contradicted what the user
// heard). Use it to spot a TREND across settings, never to characterise what
// something sounds like -- render an image for that (specimg.js, plot.js).
//
// There is no firmware-vs-sim column any more: the page runs the firmware.
'use strict';
const { loadPage } = require('./page.js');

const SPAN_MS = 250, N_SPANS = 12;
const KNOBS = {
  attack: 40, decay: 64, sustain: 70, release: 64,
  amplitudeModVelocity: 0, velocity: 127,
  amount: 96, gateMs: 575, tailMs: 600, seed: 0xCAFEBABE,
};
const DURATIONS = [20, 40, 60, 80, 90, 100, 110, 120, 127];

loadPage().then(page => {
  const FS = page.FS;
  const width = Math.round(SPAN_MS * FS / 1000);

  // Depth is a fraction of the note's RANGE, so normalise by that, not by
  // full scale -- mixing the two produced a bogus firmware/sim discrepancy.
  const probe = page.render(Object.assign({}, KNOBS, { amount: 0, chiffDuration: 0 }));
  let noteRange = 1;
  for (const v of probe.out) if (v > noteRange) noteRange = v;

  const header = Array.from({ length: N_SPANS },
    (_, i) => String(i * SPAN_MS).padStart(6)).join('');
  console.log(`note range ${noteRange} of ${page.PEAK} full scale;` +
    ` attack setting ${KNOBS.attack} = ${probe.attackSamples} smp`);
  console.log('peak-to-peak per 250ms slice, % of note range (SEE CAVEAT)');
  console.log('dur  duration(smp)  ' + header);

  for (const chiffDuration of DURATIONS) {
    const res = page.render(Object.assign({}, KNOBS, { chiffDuration }));
    const cells = [];
    for (let s = 0; s < N_SPANS; s++) {
      const a = s * width, b = Math.min((s + 1) * width, res.out.length);
      if (a >= res.out.length) { cells.push('     -'); continue; }
      let lo = Infinity, hi = -Infinity;
      for (let i = a; i < b; i++) {
        const v = res.out[i];
        if (v < lo) lo = v;
        if (v > hi) hi = v;
      }
      cells.push(((hi - lo) / noteRange * 100).toFixed(1).padStart(6));
    }
    console.log(String(chiffDuration).padStart(3) +
      String(res.windowN).padStart(13) + '  ' + cells.join(''));
  }
}).catch(e => { console.error(e); process.exit(1); });
