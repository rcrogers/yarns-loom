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
#include "yarns/envelope.h"           // LUT_EXPO_SLOPE_SHIFT_SIZE

#ifndef STACK_RESERVATION_BYTES
#error "STACK_RESERVATION_BYTES must be passed by the makefile; see yarns/makefile"
#endif

namespace yarns {
namespace stack_budget {

// Oscillator::Render (oscillator.cc): three int16 scratch buffers
// (timbre_samples, audio_samples, gain_samples), each kAudioBlockSize. Kept on
// the stack deliberately -- hot audio path; static locals caused audible
// glitches under 4-voice paraphonic renders. See oscillator.cc Render() for
// rationale.
const size_t kOscillatorRender =
    3 * kAudioBlockSize * sizeof(int16_t);

// CVOutput::RenderSamples (voice.cc): one int16 samples[kAudioBlockSize]
// local. Passed into Oscillator::Render along the render path.
const size_t kCVOutputRenderSamples =
    kAudioBlockSize * sizeof(int16_t);

// Envelope::RenderStageDispatch (envelope.cc): the compiler inlines up to
// three specializations of RenderStage<>, each of which copies the
// expo_slope_lut_ table to a local expo_slope[] buffer. We budget for all
// three being live simultaneously (conservative -- they're typically not).
const size_t kEnvelopeDispatch =
    3 * LUT_EXPO_SLOPE_SHIFT_SIZE * sizeof(int32_t);

// Conservative allowance for compiler framing across the full call chain:
// main -> RenderSamples -> Oscillator::Render -> fn (render impl)
//      -> Envelope::RenderSamples -> RenderStageDispatch
// Each frame contributes saved registers, alignment padding, return addrs.
// ARM AAPCS + Cortex-M3 Thumb: push/pop of callee-saves is typically 20-40 B
// per frame; this path has ~6 frames so allow 256 B total.
const size_t kCallChainOverhead = 256;

const size_t kWorstCaseRenderPath =
    kOscillatorRender +
    kCVOutputRenderSamples +
    kEnvelopeDispatch +
    kCallChainOverhead;

STATIC_ASSERT(
    kWorstCaseRenderPath <= STACK_RESERVATION_BYTES,
    render_path_exceeds_stack_reservation_bump_STACK_RESERVATION_in_yarns_makefile_AND_linker_script);

}  // namespace stack_budget
}  // namespace yarns

#endif  // YARNS_STACK_BUDGET_H_
