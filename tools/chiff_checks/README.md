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

## Removed

The 62 scripts that predated the engine were deleted. They eval'd the JS
model that `chiff_sim.html` no longer contains, so none of them ran, and a
check script that cannot run is worse than no script — it has to be
re-evaluated every time someone reads this directory. Recover any of them
from the commit that added them (`git log --diff-filter=A -- tools/chiff_checks`)
and port it via `page.js` before trusting its output.

## What this directory is for

An agent iterating on the envelope without waiting for someone to listen to
hardware. Judge anything added here by that: does it let the loop close
alone? Rendering an image and looking at it counts. Printing numbers that
only a human can interpret usually does not.
