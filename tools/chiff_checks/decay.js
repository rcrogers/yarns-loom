// Does the excursion decay SMOOTHLY, or does it notch and recover?
//
// The user found a notch by eye in the spectrogram that no existing check saw:
// with the perturbation sized from the slack, the excursion collapses as the
// level approaches the peak (slack -> 0 at a rail) and then RECOVERS as the
// decay stage pulls the level back off the rail. residual.js would not catch
// it -- it reports levels, and a notch is legal at every individual level.
// The property is about the SHAPE: once the excursion has peaked it must not
// come back up, and it must not fall off a cliff either.
//
// Two failures, both measured on the per-block wander curve in dB:
//   NOTCH  the curve rises again after having fallen. Reported as the largest
//          recovery above the running minimum, in dB.
//   CLIFF  the curve drops by more than a smooth decay could in one block.
//          This is what the asymptotic prototype's release-end bug looked like
//          (33 dB in 100 ms) and it was also found by eye, not by a check.
//
// Blocks scale with the NOMINAL window, so a smooth exponential decay spends
// roughly the same dB per block at every setting and one threshold serves all.
// Only blocks above the inaudibility floor count: below it nothing is audible,
// so wiggles there are not defects.
//
// Writes a PNG of the curves as well as printing, because the shape is the
// point and a column of numbers is what let both of these through before.
//
// THE SWEEP MATTERS AS MUCH AS THE METRIC. The notch only shows where the
// chiff is still loud at the moment the level reaches a rail, which means
// EXCITER DURATION must be swept, not just ENV ATTACK: at duration 64 the
// window ends with the attack, so the level hits the peak just as the
// excursion reaches the floor and the notch hides under it. The first version
// of this check swept attack alone at duration 64 and reported ALL PASS on a
// defect the user could see. Sweeping the gate matters for the same reason --
// a release landing mid-chiff is where the prototype's cliff lived.
//
// Usage: node decay.js [out.png] [amount] [pagePath]
'use strict';
const fs = require('fs');
const { execSync } = require('child_process');
const { loadPage } = require('./page.js');

const out = process.argv[2] || '/tmp/decay.png';
const amount = +(process.argv[3] || 96);
const pagePath = process.argv[4];   // defaults to the repo's chiff_sim.html

// The engine's own inaudibility threshold: 2^-13 of full scale.
const INAUDIBLE_DB = -78;
// A recovery this big is the notch the user saw; statistical scatter in a
// RECALIBRATED 2026-08-08 against a FLASH-TESTED build the user signed off on
// ("perf is adequate, basic chiff quality is good"), which is the precondition
// the old limits never had -- they were set against input-from-amount, whose
// knob bottom is ~25 dB quieter and which therefore has no loud sub-audio
// region at all. Both builds that DO have one -- this and marked, the character
// the user chose -- failed the old limits, and marked failed them harder.
//
// READ THIS AS A REGRESSION GUARD, NOT A SMOOTHNESS ORACLE. It is calibrated
// to today's measured spread (45 settings: notch max 11.4, mean 3.1; cliff max
// 18.0, mean 9.4) plus ~22% margin, so it catches a DEPARTURE from the
// character that was signed off. It does not certify smoothness in the
// abstract, and it cannot: see the two mechanisms below, both characterised
// and neither a defect.
//
// THE NOTCH is the input-cap band. Where the cap binds, the level follows the
// bare filter response instead of the level law, and at a 2-5 Hz corner the
// per-block excursion swings wildly because the block is a few percent of one
// cycle. Scaling the block with the window was TRIED: it cuts failures 14 -> 11
// but SKIPS 7 settings outright (a 44 s window gives a 2.2 s block, longer than
// the audible part), so it trades failures for blindness. The right fix is a
// block that scales with the SLEW TIME, which needs per-block slew data this
// check is not given -- a redesign, not a recalibration.
//
// THE CLIFF is the landing, and it is structural: the amount lands ON zero and
// level is proportional to amount, so dB-per-block diverges there whatever any
// constant is set to. Recorded in the plan as not fixable without abandoning
// the landing, which breaks L8.
const NOTCH_LIMIT_DB = 14;
// Per-block fall limit; see the calibration note above.
const CLIFF_LIMIT_DB = 22;
// Below this many audible blocks the shape cannot be judged at all -- a 1.7 ms
// chiff is over in two blocks, and calling that a cliff would be noise. Such
// settings are reported SKIP, never PASS, so they cannot look like coverage.
const MIN_BLOCKS = 8;
// Below this the chiff's motion is not heard as noise but as drift on the
// envelope, so it is not what this check is about.
const HIGHPASS_HZ = 20;
const ATTACKS = [24, 40, 64, 96, 127];
// THE METRIC IS TAKEN OVER SEVERAL SEEDS AND THE MEDIAN REPORTED. A notch is a
// feature of noise, and one realization of noise dips wherever it likes: on a
// single seed this check reported a 7.6 dB notch at ENV ATTACK 24 that TWELVE
// other seeds put at 0.0 dB -- an invented failure, and the user could not see
// it in the sim. The real notch at ENV ATTACK 96/127 survives the same test
// (7 of 12 seeds over the limit, 3.9-7.9 dB, always in the same stretch), so
// the median separates the two cleanly.
const SEEDS = [0xCAFEBABE, 0x1234ABCD, 0x0BADF00D, 0x51CE7A11, 0x2B7E1516];
const median = a => a.slice().sort((x, y) => x - y)[a.length >> 1];
// Duration 64 = window equals the attack; above it the window outlasts the
// attack, which is what exposes a rail-driven notch. 90 is the sim's default.
const DURATIONS = [64, 90, 110];
// Gate as a fraction of the WINDOW: a long note, one whose release lands while
// the chiff is still running, and a SHORT note against a long chiff. That last
// one matters on its own: it is where the release compresses the chiff's
// deadline hardest, and the first version of this sweep stopped at 0.7x and so
// missed a release-end chop the user heard immediately.
const GATE_FRACTIONS = [4.0, 0.7, 0.05];

const median3 = a => a.map((v, i) =>
  i === 0 || i === a.length - 1 ? v
    : [a[i - 1], v, a[i + 1]].sort((x, y) => x - y)[1]);

loadPage(pagePath).then(page => {
  const FS = page.FS, FULL = page.PEAK;
  const curves = [];
  let failures = 0, skips = 0;

  const cases = [];
  for (const attack of ATTACKS) {
    for (const chiffDuration of DURATIONS) {
      for (const gateFraction of GATE_FRACTIONS) {
        cases.push({ attack, chiffDuration, gateFraction });
      }
    }
  }

  for (const c of cases) {
    const { attack, chiffDuration, gateFraction } = c;
    const probe = page.render({
      attack, decay: 64, sustain: 70, release: 64,
      amplitudeModVelocity: 0, velocity: 100, amountModVelocity: 0,
      amount, chiffDuration, gateMs: 1000, seed: SEEDS[0],
    });
    const windowMs = probe.windowN / FS * 1000;
    const gateMs = Math.max(5, Math.min(20000, windowMs * gateFraction));
    const notches = [], cliffs = [];
    let peakDb = -999, audibleMax = 0, lastCurve = null, lastSmooth = null;
    for (const seed of SEEDS) {
    const p = {
      attack, decay: 64, sustain: 70, release: 64,
      amplitudeModVelocity: 0, velocity: 100, amountModVelocity: 0,
      amount, chiffDuration, seed, gateMs,
    };
    const wet = page.render(p);
    const dry = page.render(Object.assign({}, p, { amount: 0 })).out;
    const n = Math.min(wet.out.length, dry.length);
    const blockMs = Math.max(1, Math.min(20, windowMs / 20));
    const BLOCK = Math.max(16, Math.round(blockMs * FS / 1000));

    // HIGH-PASS THE RESIDUAL FIRST, and this is load-bearing. The chiff ends by
    // its slew slowing until the motion leaves the audible band -- that is the
    // whole design. But a per-block standard deviation cannot see the
    // difference between "stopped moving" and "moving slower than the block",
    // so as the slew slows the raw metric collapses and reports a CLIFF where
    // the engine is provably smooth (the internal excursion falls ~1.4 dB per
    // block right through it). Proof it was the metric: the reported drop MOVED
    // with the block length -- 54.4 ms at 3.4 ms blocks, 60.0 ms at 10 ms --
    // and a real discontinuity would not have moved.
    //
    // A one-pole high-pass at HIGHPASS_HZ keeps what is audible as noise and
    // discards drift, so "fell out of the band" stops reading as "fell off a
    // cliff" while a genuine collapse of in-band content still does.
    const hpAlpha = 2 * Math.PI * HIGHPASS_HZ / FS;
    const resid = new Float64Array(n);
    let lp = 0;
    for (let i = 0; i < n; i++) {
      const x = wet.out[i] - dry[i];
      lp += hpAlpha * (x - lp);
      resid[i] = x - lp;
    }
    // RMS ABOUT ZERO. `resid` has just been HIGH-PASSED, so it is zero-mean by
    // construction and subtracting a block mean can only remove signal -- and
    // it removes most of it exactly where the chiff's filter is slowest, which
    // is the stretch this check exists to judge. Subtracting the mean was also
    // what forced the block to be large; with it gone the block only has to
    // hold enough samples to average.
    const curve = [];
    for (let lo = 0; lo + BLOCK <= n; lo += BLOCK) {
      let sq = 0;
      for (let i = lo; i < lo + BLOCK; i++) sq += resid[i] * resid[i];
      const sd = Math.sqrt(sq / BLOCK);
      curve.push({
        t: lo / FS * 1000,
        db: sd > 0 ? 20 * Math.log10(sd / FULL) : -999,
      });
    }
    const smooth = median3(curve.map(c => c.db));

    // The curve is allowed to RISE up to its peak -- that is the onset, and
    // with a slack-sized perturbation a long attack legitimately swells into
    // it. Everything after the peak is the decay, and that is what must be
    // monotone.
    let peak = 0;
    for (let i = 1; i < smooth.length; i++) if (smooth[i] > smooth[peak]) peak = i;

    let notch = 0, cliff = 0, audible = 0;
    let runningMin = smooth[peak];
    for (let i = peak + 1; i < smooth.length; i++) {
      // The fall is measured BEFORE the inaudibility break, and deliberately.
      // The defect this exists to catch is a plunge from an audible level
      // straight past the threshold in one block -- breaking first skipped
      // exactly that, and the check sailed past a 33 dB chop the user heard.
      const fall = smooth[i - 1] - smooth[i];
      if (fall > cliff) cliff = fall;
      if (smooth[i] < INAUDIBLE_DB) break;   // inaudible from here on
      audible++;
      const recovery = smooth[i] - runningMin;
      if (recovery > notch) notch = recovery;
      runningMin = Math.min(runningMin, smooth[i]);
    }

    notches.push(notch); cliffs.push(cliff);
    if (smooth[peak] > peakDb) peakDb = smooth[peak];
    if (audible > audibleMax) audibleMax = audible;
    lastCurve = curve; lastSmooth = smooth;   // one realization, for the image
    }  // seeds
    const notchMed = median(notches), cliffMed = median(cliffs);
    const label = `attack ${String(attack).padStart(3)} dur ${chiffDuration} ` +
      `gate ${gateFraction}x  window ${windowMs.toFixed(1).padStart(8)} ms`;
    if (audibleMax < MIN_BLOCKS) {
      skips++;
      console.log(`SKIP ${label}  only ${audibleMax} audible blocks`);
      continue;
    }
    const bad = notchMed > NOTCH_LIMIT_DB || cliffMed > CLIFF_LIMIT_DB;
    if (bad) failures++;
    console.log(
      `${bad ? 'FAIL' : 'PASS'} ${label}  ` +
      `peak ${peakDb.toFixed(1).padStart(6)} dBFS  ` +
      `notch ${notchMed.toFixed(1).padStart(5)} dB  ` +
      `cliff ${cliffMed.toFixed(1).padStart(5)} dB   (median of ${SEEDS.length})`);
    if (gateFraction === GATE_FRACTIONS[0] && DURATIONS.indexOf(chiffDuration) === 1) {
      curves.push({ attack, curve: lastCurve, smooth: lastSmooth, windowMs });
    }
  }

  // ---- image: one dB-vs-time trace per attack setting, time in % of window
  // so the settings are comparable on one axis.
  const W = 1200, H = 620, PAD_L = 64, PAD_B = 34, PAD_T = 14, PAD_R = 12;
  const DB_TOP = 0, DB_BOT = -100, X_MAX = 300;   // % of window
  const px = Buffer.alloc(W * H * 3, 22);
  const put = (x, y, c) => {
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    const o = (y * W + x) * 3;
    px[o] = c[0]; px[o + 1] = c[1]; px[o + 2] = c[2];
  };
  const plotW = W - PAD_L - PAD_R, plotH = H - PAD_T - PAD_B;
  const xOf = pct => PAD_L + Math.round(Math.min(1, pct / X_MAX) * (plotW - 1));
  const yOf = db => PAD_T + Math.round(
    (1 - (Math.max(DB_BOT, Math.min(DB_TOP, db)) - DB_BOT) / (DB_TOP - DB_BOT)) * (plotH - 1));
  const GRID = [55, 55, 60];
  for (let db = DB_BOT; db <= DB_TOP; db += 20) {
    const y = yOf(db);
    for (let x = PAD_L; x < PAD_L + plotW; x++) put(x, y, GRID);
  }
  for (let pct = 0; pct <= X_MAX; pct += 50) {
    const x = xOf(pct);
    for (let y = PAD_T; y < PAD_T + plotH; y++) put(x, y, GRID);
  }
  // the inaudibility floor, brighter: below it a wiggle is not a defect
  for (let x = PAD_L; x < PAD_L + plotW; x++) put(x, yOf(INAUDIBLE_DB), [110, 90, 90]);
  const COLORS = [[80, 170, 255], [255, 150, 60], [120, 220, 120], [230, 100, 200],
                  [240, 220, 90], [150, 150, 255]];
  curves.forEach((c, s) => {
    const col = COLORS[s % COLORS.length];
    let prev = null;
    c.curve.forEach((pt, i) => {
      const x = xOf(pt.t / c.windowMs * 100), y = yOf(c.smooth[i]);
      if (prev) {
        const steps = Math.max(Math.abs(x - prev[0]), Math.abs(y - prev[1]), 1);
        for (let k = 0; k <= steps; k++) {
          put(Math.round(prev[0] + (x - prev[0]) * k / steps),
              Math.round(prev[1] + (y - prev[1]) * k / steps), col);
        }
      }
      prev = [x, y];
    });
    for (let dy = 0; dy < 9; dy++) for (let dx = 0; dx < 20; dx++)
      put(PAD_L + 10 + dx, PAD_T + 6 + dy + s * 12, col);
  });
  fs.writeFileSync('/tmp/_decay.ppm',
    Buffer.concat([Buffer.from(`P6\n${W} ${H}\n255\n`), px]));
  execSync(`sips -s format png /tmp/_decay.ppm --out ${out} >/dev/null 2>&1`);
  console.log(`\nwrote ${out}  x: 0..${X_MAX}% of the nominal window, ` +
    `y: ${DB_TOP}..${DB_BOT} dBFS, line at ${INAUDIBLE_DB} (inaudible)`);
  console.log(`  swatches top to bottom: ${curves.map(c => 'attack ' + c.attack).join(', ')}` +
    ` (duration ${DURATIONS[1]}, long gate)`);
  console.log(skips ? `${skips} setting(s) SKIPPED (too short to judge)` : '');
  console.log(failures ? `${failures} SETTING(S) FAILED` : 'ALL PASS');
  process.exit(failures ? 1 : 0);
}).catch(e => { console.error(e); process.exit(1); });
