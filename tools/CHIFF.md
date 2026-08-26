# Chiff tooling

The chiff is the noise burst `yarns/envelope.cc` adds to an envelope's attack.
It is a slew driven by a PRNG, whose slew time, drive and input are all read
off a decaying AMOUNT. EXCITER AMOUNT and EXCITER DURATION dial it.

Everything here exists because the chiff is a *stochastic* process inside a
realtime integer DSP path: you cannot eyeball it, and most obvious measurements
of it are wrong in ways that look right.

## Entry points

The root `Makefile` documents its own targets. In short:

    make sim        rebuild + re-inline the sim engine (always rebuilds)
    make host       host C reference: UBSan, then golden, battery, anomaly
    make qemu       differential: render-loop asm == C reference, 10 scenarios
    make check      host + qemu + simparity + peakfloor + xvmod + strictmode
                    + blockedge + blockrate + decay
    make firmware   flashable .syx
    make cycles     what the envelope costs per block

`make sim` needs Docker/emscripten. `make check` deliberately does **not**
rebuild the sim, so a stale committed page shows up as a simparity failure
instead of being silently refreshed.

## What proves what

There is one implementation. `chiff_sim.html` inlines a compiled
`yarns/envelope.cc`, so the sim and the firmware cannot disagree — but they run
different *code paths* within it, and that is the whole point of the table.

| tool | proves | cannot see |
|---|---|---|
| `make qemu` | the shipped ARM asm matches the C reference, bit for bit | anything above the render loop |
| `hosttest/golden.js` | the output is unchanged, sample for sample, over 14 cases | whether a *change* is correct |
| `make host` | 45 invariants over 8 seeds: DAC range, bias independence, monotonicity, stage handoff, clamp-with-chiff | anything perceptual |
| `hosttest` UBSan build | signed overflow and bad shifts, which render *something* and pass every other check | anything it does not execute |
| `hosttest/anomaly.js` | 140 cases against a recorded baseline, tolerant of small movement | whether the baseline was right |
| `hosttest/arrival.js` | every timed stage lands on its target, over 128 settings x attack/decay/release | anything the chiff adds on top |
| `make cv` | the CV OUTPUT PATH: one Voice::NoteOn reaches four envelopes with the same note, and the aux CV pack does not carry across halves | Part::VoiceNoteOn, which is still ui.h-bound |
| `chiff_checks/simparity.js` | the published page renders identically to the native build | whether either is right |
| `make cycles` | worst-case cost via the longest path through the CFG, loops weighted by their trip count | anything the linker pulls in — watch `flash free` |
| `hosttest/blockrate.js` | a dBFS level on the tremolo bias's once-a-block breaks | whether that level is audible to you |
| hardware | the display, the CV outputs, and how it sounds | — |

**Everything except `make qemu` runs the C twin, not the shipped asm.** Run
qemu in the background the moment it could be relevant; its only cost is
latency.

**Nothing here covers the display.** The CV output path is covered from
`Voice::NoteOn` down by `make cv`, which host-compiles `yarns/voice.cc` and
`yarns/oscillator.cc` against a DAC that records instead of writing. What is
still uncovered is `Part::VoiceNoteOn` itself -- `part.cc` includes `ui.h`,
which includes the encoder driver and its GPIO reads, so it has no host build;
`simengine/engine.cc` mirrors that chain rather than running it. Three defects
in this gap shipped and were caught only by flashing.

## Reading the chiff, not its output

Output statistics cannot see the model. Level and centroid match equally for a
clipped quiet signal and an unclipped loud one. Read the engine's own state:

    test basic <amount> <duration> chiff_trace=1        amount, slew time, input
    test basic <amount> <duration> chiff_state_trace=1  the chiff term alone
    test basic <amount> <duration> slew_trace=1         slew time per block
    test basic <amount> <duration> seed=<n>             select a realization

`hosttest/passthrough.js` is the model as a check. `chiff_checks/deadzone.js`
reads state rather than sound, so it is seed-independent.

Scripts get the binary from `hosttest/harness.js`, which resolves it relative to
itself — so they run from anywhere, and say what to build when it is missing.
Use it rather than writing `./test`.

**`report=1` prints to stderr.** Fold it in with `2>&1`; capturing it via
`stdio` returns stdout, which is null. Two scripts shipped broken on this.

## Traps

Each of these produced a confident wrong conclusion, more than once.

**One seed is not a result.** Until `seed=N` existed, every sweep in
`tools/hosttest` rendered the same noise. Per-seed spread at short durations is
~2x -- the same size as the effects being measured. `battery.js` now runs
`analyze.js` once per seed and reports, per check, how many seeds passed and the
range of its figure; a check whose range is a single value is seed-independent
and needs none. MEASURED 2026-08-19: `peak overshoot is bounded` had been sized
at 15% against seed 0's 9.8%, and 15 of 128 seeds exceed it. Most scripts outside
the battery still hardcode one seed; `dieout.js` does not.

**A per-block trace cannot see inside a block.** Every trace prints once per
rendered block. Anything wrong at a run's start and right at its end is
invisible to all of them; difference the output against an AMOUNT 0 run instead.

**A verdict line in the wrong place is a green build.** `analyze.js` printed
its PASS/FAIL summary two thirds of the way up the file and never called
`process.exit`, so the 22 checks below it -- the whole bias path, the clip path,
both bias-independence invariants -- could print FAIL *after* the word "ALL PASS"
while the script exited 0 and `make host` stayed green. MEASURED by mutating a
clip-path check to a limit it cannot meet. Fixed 2026-08-19; the lesson is to
mutation-test the harness itself, not only the engine.

**A stale binary answers the wrong question silently.** An unknown driver option
is ignored, not rejected. Check the row count against the time you asked for.
Rebuild the native harness after switching branches — `simparity` compares the
page against `hosttest/test`, and will "fail" if that binary is from elsewhere.

**Measure the effect, not the parameter.** The slew time keeps moving after the
corner has left the audio band. Convert to the corner, then compare.

**A normalised statistic hides what a spectrogram shows.** A constant fraction
of a falling total still means the high end disappearing — a display shows
absolute level against a fixed floor, and HF starts ~20 dB down. Quote absolute
band levels. A spectral centroid is a particularly bad summary here: the chiff's
residual at low AMOUNT is mostly sub-100 Hz, so the centroid tracks something
inaudible, and it cannot distinguish a level change from a colour change.

**Comparing spectra across durations needs matched resolution.** An analysis
window that scales with the setting scales the frequency resolution with it. A
6.8 ms event cannot hold a 40 Hz cycle. Use a fixed-length onset window and
include only settings whose chiff is at least that long.

**A window function is not free.** Hann tapers both ends to zero, which on a
decaying signal discards the onset — the loudest, brightest part, and the part
that defines the timbre. Two scripts disagreed by 50% on one build over this.

**Watch `flash free`.** A 64-bit divide by a runtime value costs ~1.4 kB of
library code that no check notices, because the image stays bit-identical. Use
`DivU64ByU32` and grep the disassembly for `__aeabi_uldivmod` after any change
that divides. It has been reintroduced three times.

**A cost model that weights by ADDRESS RANGE prices the wrong code.**
`cycles.py` zeroed its head-and-tail sample loops by the address span between a
back edge's target and its source. GCC scatters a loop's blocks, so that span
swept up whatever landed between them -- and a `bl` in the per-run setup, moved
inside one, priced at zero. It read 169 cycles for a path costing 927, and
reported the per-run path as a quarter of its real size for as long as the tool
has existed. Weight by the CFG's natural loop, one range per block.

**A loop the tool cuts is a loop it prices once.** `longest_path` cuts back
edges, which is right for finding a path and wrong for costing one. A sixteen
iteration table build in the per-run path priced as one iteration, turning a 38
cycle win into a reported 143. Anything with a trip count needs a weight, and
the trip count has to come from the source, not a guess.

**Isolate the artifact, not the signal it rides on.** Three measurements of the
tremolo bias's block-rate breaks were wrong before one was right. A sustained
note has no breaks at all, because the bias target does not move between blocks.
A slow attack has tiny ones. High-passing the OUTPUT reads the envelope's own
attack transient as the artifact, because a fast attack has more energy above
300 Hz than the artifact does. Only differencing against a tremolo-free run
isolates the bias -- which is sound only because the envelope is
bias-independent, and the battery pins that at 0.

**Operand order in an asm block allocates registers.** Reflowing
`YARNS_CHIFF_ASM_STATE` one operand per line moved `[delta]` past
`[chiff_rate]`, and GCC emitted the same instructions in different registers:
same size, different binary. A rename of operand tags is only provably neutral
if the order is left alone.

**Scalar stats have repeatedly missed what a person sees.** Render an image and
look, or ask. When someone says they cannot see the effect you measured, the
metric is the suspect.

## The spectrogram

Rebuilt 2026-08-20 (`0375e50b`). It is a **Gabor transform**: one Gaussian
bandpass per pixel row, evaluated in the frequency domain, on a log frequency
axis. Rows are computed in parallel across workers, falling back to one thread
and saying so in the readout.

What it is good for: where the chiff's energy is, how its corner sweeps, whether
a transition is smooth. It found a first-derivative discontinuity in the chiff's
input schedule that no scalar metric had reported.

**Drive it offline with `specimg.js out.png key=value`** — keys are the page's
own control ids (`amt`, `chiffDur`, `atk`, `rel`, `gate`, `seed`, plus `w=`/`h=`).
That renders THE PAGE'S OWN analysis; the file deliberately contains none of its
own. Env `REASSIGN=1` and `PERCEPTUAL=1` set the overlay toggles, which the
headless loader otherwise reports as off.

Two optional modes, both off by default:

- **Sharpen transients** (reassignment). Moves each point's energy to the instant
  its own group delay names. A DURATION 0 impulse goes from 360 ms wide to 2 ms.
  Off by default because the group delay is meaningless where the phase is, and
  the chiff is noise over most of the knob — reassigning noise scatters it.
- **Perceptual** (A-weighting + auditory bandwidths). The plain display shows
  30 Hz 41 dB brighter than it is heard and 10 Hz 70 dB brighter, which is why
  the bottom of the plot blazes on a chiff that sounds unremarkable. Use it for
  "should I hear this", not for "what is the engine doing".

**The dashed curve is the resolution limit** — what a single impulse at t=0
would paint. Anything hugging its shape, or narrower than it at that height, is
the instrument and not the exciter. It has to be drawn because it was mistaken
for signal three times: an impulse's width, a decay's trailing edge, and content
lingering after the chiff had stopped.

**What it cannot see.** Nothing below the axis floor, which is 8 cycles per
record — so it moves with the gate. Nothing about time finer than the drawn
curve. And its holes are interference nulls, NOT absence of a frequency:
MEASURED, halving the analysis bandwidth relocated 47 of 54 of them.

## Sim pages

`chiff_sim.html` is committed with the engine inlined, so rebuilding the engine
alone changes nothing — `inline_engine.py` must re-splice it, and `make sim`
does. A/B variants get a badge naming what differs, opt in with
`SIM_LABEL_VARIANT=1`; the badge's text is read from the source, so a page
cannot claim a constant it does not have. The reference page carries none.

Before overwriting a published page, fetch it and diff the markup and the UI
script separately — the engine blob always differs after a rebuild.
