# chiff check scripts

`chiff_sim.html` no longer contains a model. It runs the compiled
`yarns/envelope.cc` (see `tools/simengine/`), so the sim and the firmware
cannot disagree — there is one implementation.

## Live

- `page.js` — shared headless loader. Boots the real page and returns its
  `render()`, `dialed()`, `fft()`, and the engine. **Use this**; do not eval a
  slice of the HTML.
- `simparity.js` — proves the page renders bit-identically to the natively
  compiled harness, and that settings resolve through the firmware LUTs.
  Run after any change to the sim or the engine.
- `specimg.js` — spectrogram to PNG. Image inspection has matched the user's
  ears where scalar stats have not.
- `highdur.js` — noise band vs CHIFF DURATION. NB the band metric cannot
  separate noise from ordinary envelope motion; see the plan.

Also useful, outside this directory:
- `tools/hosttest/build.sh` — 23-check battery on the native build.
- `tools/hosttest/dursweep.js` — all 128 CHIFF DURATION settings.
- `tools/hosttest/plot.js` — trace to PNG.
- `tools/hosttest/probe.cc` — per-block internal chiff state.
- `tools/simengine/parity.js` — engine vs native build.

## Stale

Every other script here predates the engine. They eval'd the JS model that
used to live in `chiff_sim.html` and no longer runs. They are kept because
several encode findings that were expensive to obtain, but they do **not**
execute as-is. Port one via `page.js` if you need it; do not trust its output
until you have.
