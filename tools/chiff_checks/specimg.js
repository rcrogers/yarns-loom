// Render the page's OWN spectrogram to a PNG and look at it.
//
// THIS FILE MUST NOT CONTAIN ANY ANALYSIS. It used to carry its own copy of the
// filterbank, the log axis, the colour ramp and the pixel mapping, and that
// copy drifted from the page's: the offline images came out clean while the
// published page banded, and several rounds were spent debugging the wrong
// implementation. Everything below the loader now runs INSIDE the page, so what
// this writes is by construction what the page draws.
//
// Usage: node specimg.js out.png [key=value ...]
//   keys are the page's own control ids: amt, chiffDur, atk, dec, sus, rel,
//   vel, gate, seed, and the image size as w= and h=.
'use strict';
const fs = require('fs');
const { execSync } = require('child_process');
const { loadPage } = require('./page.js');

const out = process.argv[2] || '/tmp/specimg.png';
const overrides = {};
for (const arg of process.argv.slice(3)) {
  const [k, v] = arg.split('=');
  if (v !== undefined) overrides[k] = Number(v);
}
const W = overrides.w || 1366, H = overrides.h || 394;
delete overrides.w; delete overrides.h;

// THE PAGE'S OWN DEFAULTS, taken from its markup. The headless loader models
// element.value but not the HTML `value=` attribute, so every control reads 0
// there -- which silently rendered a 1 ms note. Parsed rather than restated so
// this file still has no independent idea of what the sim's defaults are.
const path = require('path');
const HTML = fs.readFileSync(path.join(__dirname, '..', '..', 'chiff_sim.html'), 'utf8');
const defaults = {};
for (const m of HTML.matchAll(/<input[^>]*\bid="([^"]+)"[^>]*\bvalue="(-?\d+)"/g)) {
  defaults[m[1]] = Number(m[2]);
}
if (defaults.gate === undefined) throw new Error('could not read the page defaults');

loadPage().then(page => {
  Object.assign(page.values, defaults, overrides);

  // draw() renders the note and the chiff-free reference; computeSpectrogram
  // then builds the cell grid at the size asked for. Both are the page's.
  // Overlay checkboxes are not sliders; the loader's element mock defaults them
  // to false, so anything driven by one has to be set explicitly here.
  if (process.env.REASSIGN) page.evalInPage("$('reassign').checked = true");
  if (process.env.PERCEPTUAL) page.evalInPage("$('perceptual').checked = true");
  page.evalInPage('draw()');
  page.evalInPage(`computeSpectrogram(${W}, ${H})`);

  // The colour mapping is the page's too, so the loop runs in there and only
  // bytes come back.
  const rgb = page.evalInPage(`(() => {
    const { cells, cols, rows } = specData;
    const px = new Array(cols * rows * 3);
    for (let i = 0; i < cols * rows; i++) {
      const db = 20 * Math.log10(cells[i] / SPEC_REF + 1e-12);
      const c = rampColor((db + SPEC_RANGE_DB) / SPEC_RANGE_DB);
      px[i*3] = c[0]; px[i*3+1] = c[1]; px[i*3+2] = c[2];
    }
    return px;
  })()`);
  const meta = JSON.parse(page.evalInPage(
    'JSON.stringify({cols:specData.cols, rows:specData.rows, tMax:specData.tMax,' +
    ' fMax:specData.fMax, fMin:specData.fMin, Q:CQ_Q})'));

  fs.writeFileSync('/tmp/_spec.ppm', Buffer.concat([
    Buffer.from(`P6\n${meta.cols} ${meta.rows}\n255\n`), Buffer.from(rgb)]));
  execSync(`sips -s format png /tmp/_spec.ppm --out ${out} >/dev/null 2>&1`);
  const shown = Object.entries(overrides).map(([k, v]) => `${k}=${v}`).join(' ');
  console.log(`wrote ${out}  ${meta.cols}x${meta.rows}` +
    `  (x: 0..${meta.tMax.toFixed(0)}ms, y: log ${meta.fMin.toFixed(1)}..${(meta.fMax/1000).toFixed(1)}kHz, Q=${meta.Q})` +
    (shown ? `\n  overrides: ${shown}` : ''));
}).catch(e => { console.error(e); process.exit(1); });
