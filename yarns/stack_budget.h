// Copyright 2026 Chris Rogers.
//
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Compile-time cross-check: sum of known large stack locals on the boot-time
// render path must fit within the linker's stack reservation.
//
// This is hand-maintained: it only catches regressions in frames we've
// explicitly listed. If someone adds a new 500-byte local to a function on
// the render path and doesn't list it here, the assertion won't notice.
// However, the common regression we DO care about -- somebody bumping
// kAudioBlockSize or similar such that our sizeof()-based math balloons, or
// someone reducing STACK_RESERVATION in the makefile -- is caught loudly.
//
// The budget is also a legible record of why the linker reservation is set
// where it is. If you find yourself tempted to reduce it, read here first.

#ifndef YARNS_STACK_BUDGET_H_
#define YARNS_STACK_BUDGET_H_

#include "stmlib/stmlib.h"            // STATIC_ASSERT

#include "yarns/drivers/dac.h"        // kAudioBlockSize

#ifndef STACK_RESERVATION_BYTES
#error "STACK_RESERVATION_BYTES must be passed by the makefile; see yarns/makefile"
#endif

namespace yarns {
namespace stack_budget {

// THE ARRAYS, sizeof-derived, so bumping kAudioBlockSize is caught here rather
// than at runtime.
//
// Oscillator::Render keeps ONE array of two halves (timbre at [p], gain at
// [p + kAudioBlockSize]) on the stack deliberately -- hot audio path; static
// locals caused audible glitches under 4-voice paraphonic renders. See
// oscillator.cc Render() for the rationale.
const size_t kOscillatorRenderArray = 2 * kAudioBlockSize * sizeof(int16_t);
// CVOutput::RenderSamples (voice.cc): one int16 samples[kAudioBlockSize].
const size_t kCVOutputRenderArray = kAudioBlockSize * sizeof(int16_t);

// EVERYTHING ELSE ON THE DEEPEST PATH, measured rather than guessed. The
// makefile passes -fstack-usage, which writes a .su beside every object, so
// these are read and not estimated. From build/yarns/*.su, 2026-08-26, with the
// arrays above subtracted out:
//
//   CVOutput::RenderSamples      152 - 128 =  24
//   Oscillator::Render           272 - 256 =  16
//   Envelope::RenderSamples                =  16
//   Envelope::RenderStage                  = 248  <- spill slots, see below
//   Envelope::AdvanceChiffDecay            =  48
//
// RenderStage dominates because its per-run setup holds the eleven values the
// render loop needs live at once; 88 of its instructions are stack traffic.
// RenderStage tail-calls HandOffToNextStage with b.w and re-enters ITSELF as a
// loop, so the stage machinery does not nest and this counts once.
//
// The figure this replaced allowed 256 B for "~6 frames at 20-40 B each". One
// frame on this path is 248.
const size_t kMeasuredFraming = 24 + 16 + 16 + 248 + 48;

// Above CVOutput::RenderSamples: the audio ISR's context save and the callers
// in yarns.cc, which are not on a .su path this header can cite. Unchanged
// allowance, and the only estimate left here.
const size_t kCallerAllowance = 256;

const size_t kWorstCaseRenderPath =
    kOscillatorRenderArray +
    kCVOutputRenderArray +
    kMeasuredFraming +
    kCallerAllowance;

STATIC_ASSERT(
    kWorstCaseRenderPath <= STACK_RESERVATION_BYTES,
    render_path_exceeds_stack_reservation_bump_STACK_RESERVATION_in_yarns_makefile_AND_linker_script);

}  // namespace stack_budget
}  // namespace yarns

#endif  // YARNS_STACK_BUDGET_H_
