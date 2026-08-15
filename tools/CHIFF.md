# Chiff tooling

The chiff is the noise burst `yarns/envelope.cc` adds to an envelope's attack.
It is a one-pole filter driven by a PRNG, whose slew time, drive and input are
all read off a decaying AMOUNT. EXCITER AMOUNT and EXCITER DURATION dial it.

Everything here exists because the chiff is a *stochastic* process inside a
realtime integer DSP path: you cannot eyeball it, and most obvious measurements
of it are wrong in ways that look right.

## Entry points

The root `Makefile` documents its own targets. In short:

    make sim        rebuild + re-inline the sim engine (always rebuilds)
    make host       host C-reference battery (41 checks)
    make qemu       differential: render-loop asm == C reference, 10 scenarios
    make check      host + qemu + simparity + peakfloor + xvmod + strictmode + decay
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
| `hosttest/golden.js` | the output is unchanged, sample for sample, over 13 cases | whether a *change* is correct |
| `make host` | 41 invariants: DAC range, bias independence, monotonicity, stage handoff | anything perceptual |
| `chiff_checks/simparity.js` | the published page renders identically to the native build | whether either is right |
| `make cycles` | worst-case cost via the longest path through the CFG | anything the linker pulls in — watch `flash free` |
| hardware | the display, the CV outputs, and how it sounds | — |

**Everything except `make qemu` runs the C twin, not the shipped asm.** Run
qemu in the background the moment it could be relevant; its only cost is
latency.

**Nothing here covers the display or the CV output path.** `simengine/engine.cc`
mirrors `Part::VoiceNoteOn` and stops there. Three defects in that gap shipped
and were caught only by flashing.

## Reading the chiff, not its output

Output statistics cannot see the model. Level and centroid match equally for a
clipped quiet signal and an unclipped loud one. Read the engine's own state:

    test basic <amount> <duration> chiff_trace=1        drive, slew time, input
    test basic <amount> <duration> chiff_state_trace=1  the chiff term alone
    test basic <amount> <duration> slew_trace=1         slew time per block
    test basic <amount> <duration> seed=<n>             select a realization

`hosttest/passthrough.js` is the model as a check. `chiff_checks/deadzone.js`
reads state rather than sound, so it is seed-independent.

## Traps

Each of these produced a confident wrong conclusion, more than once.

**One seed is not a result.** Until `seed=N` existed, every sweep in
`tools/hosttest` rendered the same noise. Per-seed spread at short durations is
~2x — the same size as the effects being measured. Use eight, and quote the
range, not just the mean. Most scripts here still hardcode one seed.

**A per-block trace cannot see inside a block.** Every trace prints once per
rendered block. Anything wrong at a run's start and right at its end is
invisible to all of them; difference the output against an AMOUNT 0 run instead.

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

**Scalar stats have repeatedly missed what a person sees.** Render an image and
look, or ask. When someone says they cannot see the effect you measured, the
metric is the suspect.

## Sim pages

`chiff_sim.html` is committed with the engine inlined, so rebuilding the engine
alone changes nothing — `inline_engine.py` must re-splice it, and `make sim`
does. A/B variants get a badge naming what differs, opt in with
`SIM_LABEL_VARIANT=1`; the badge's text is read from the source, so a page
cannot claim a constant it does not have. The reference page carries none.

Before overwriting a published page, fetch it and diff the markup and the UI
script separately — the engine blob always differs after a rebuild.
