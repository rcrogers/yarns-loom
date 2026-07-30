# chiff check scripts

`chiff_sim.html` no longer contains a model. It runs the compiled
`yarns/envelope.cc` (see `tools/simengine/`), so the sim and the firmware
cannot disagree — there is one implementation.

## Live

- `page.js` — shared headless loader. Boots the real page and returns its
  `render()`, `nominalValue()`, `fft()`, and the engine. **Use this**; do not eval a
  slice of the HTML.
- `simparity.js` — proves the page renders bit-identically to the natively
  compiled harness, and that settings resolve through the firmware LUTs.
  Run after any change to the sim or the engine.
- `specimg.js` — spectrogram to PNG. Image inspection has matched the user's
  ears where scalar stats have not.
- `highdur.js` — noise band vs CHIFF DURATION. NB the band metric cannot
  separate noise from ordinary envelope motion; see the plan.
- `peakfloor.js` — the AMPLITUDE MOD VELOCITY -64 / velocity 127 corner, where
  peak_u16 reaches exactly 0. Lives here rather than in the host battery because
  that battery compiles only `yarns/envelope.cc`: it sets `peak_u16` directly
  and so never runs the velocity chain. `tools/simengine/engine.cc` mirrors
  `Part::VoiceNoteOn`, which makes the sim the only place this is reachable.
- `xvmod.js` — EXCITER AMT VEL MOD semantics: identity at mod 0, monotone in
  both directions, clamped to 0..127, and that an amount of 0 is not a hard off.
  Sim-side for the same reason as `peakfloor.js`.
- `strictmode.js` — pre-publish gate. The artifact runs the page's scripts
  STRICT while these checks load them sloppy; that gap once shipped an artifact
  with blank graphs. Renders as well as boots, because booting alone would not
  have caught it.
- `decay.js` — is the excursion's decay SMOOTH? Sweeps ENV ATTACK x EXCITER
  DURATION x gate and reports two shape failures on the per-block wander
  curve: a NOTCH (the curve falls, then recovers) and a CLIFF (it falls faster
  than a decay could). Both had been found by eye, never by a check — a notch
  is legal at every individual level, so `residual.js`, which reports levels,
  cannot see it. **Sweeping EXCITER DURATION is what makes it work:** at
  duration 64 the window ends with the attack, so a rail-driven notch hides
  under the noise floor; the first version of this check swept ENV ATTACK alone
  and reported ALL PASS on a defect that was plainly visible in the sim.
  Settings too short to judge (a 1.7 ms chiff is two blocks long) report SKIP,
  never PASS. Takes an optional page path, so a prototype branch's
  `chiff_sim.html` can be measured against the current one.
  NOT in `make check`: it fails today (see the plan's OPEN ITEM 3), and a
  suite that is expected to be red stops being read. Wire it in once it passes.
- `residual.js` — splits the chiff's effect into OFFSET (per-block mean of
  chiff minus nominal: the value sitting off where it should be) and WANDER
  (per-block standard deviation: what is audible), in absolute dBFS. RMS of the
  residual is the two in quadrature and cannot distinguish them, which is how a
  "clean landing" claim once went unchallenged. Offset is only readable once
  wander is well below it. Give `tailMs` room to reach past the release.

Also useful, outside this directory:
- `tools/hosttest/build.sh` — 22-check battery on the native build.
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

## History

~64 earlier scripts lived here. They predated the compiled engine and eval'd a
JS model that no longer exists, so they were deleted rather than left as traps.
`git log --diff-filter=D -- tools/chiff_checks/` recovers them if ever needed.
