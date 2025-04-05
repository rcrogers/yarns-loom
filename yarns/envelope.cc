// Copyright 2012 Emilie Gillet.
// Copyright 2020 Chris Rogers.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.

#include "yarns/envelope.h"

#include "yarns/multi.h"

#include "stmlib/dsp/dsp.h"

#pragma GCC diagnostic error "-Wsign-conversion"
#pragma GCC diagnostic error "-Wsign-compare"
// #pragma GCC diagnostic error "-Wconversion"

namespace yarns {

using namespace stmlib;

/*
Edge cases when a manually started stage has an off-nominal delta to target:
1. Wrong direction (skip)
  - Affects: ATTACK, DECAY
    - "High attack interrupted by low attack"
      - NoteOn with high peak
      - ATTACK interrupted by early NoteOff
      - EARLY_RELEASE interrupted by NoteOn with a peak level below current value
    - Inverted decay, if peak level is below sustain level
    - In general, delta from actual value to target has different sign than delta from nominal start value to target
      - I.e., we seem to have already passed the target
  - Solution
    - ATTACK has wrong direction: skip to DECAY
    - DECAY has wrong direction: this is more ambiguous
      - Should we just disallow peak < sustain?
2. Value too far from target (prelude)
  - Affects: DECAY, RELEASE
    - Primary case: NoteOff before SUSTAIN stage has started
    - Can also apply to decay start, e.g.:
      - "High attack interrupted by low attack" causes ATTACK to skip to DECAY
      - ATTACK skips to decay
      - DECAY is now further from sustain level than expected
  - Solution: prelude
    - Populate the stage's expo slopes
    - Extend the stage's initial/steepest slope backward in time
    - Give each Motion a new counter of prelude samples remaining
      - While these count down, use the first expo slope value only
      - See calculation for release_prelude_.samples_left
3. Value too close to target
  - Affects: ATTACK, RELEASE
    - NoteOff from below sustain level
    - NoteOn when above minimum level
  - Solution: truncate beginning of phase
    - Want to skip forward in phase
    - Translating "value too close" to a phase is nonlinear due to expo slope
      - Use LUT that translates an expo value to a phase?
*/

void Envelope::Init(int32_t value) {
  value_ = value;
  bias_ = 0;
  stage_ = ENV_STAGE_DEAD;
  for (uint8_t i = 0; i < kNumEdges; ++i) {
    edges_[i].slope = 0;
    edges_[i].samples = 0;
  }
  // Trigger(ENV_STAGE_DEAD);
}

void Envelope::NoteOff() {
  if (stage_ < ENV_STAGE_RELEASE) {
    Trigger(ENV_STAGE_RELEASE);
  }
}

void Envelope::NoteOn(
  ADSR& adsr,
  int32_t min_target, int32_t max_target // Actual bounds, 16-bit signed
) {
  // NB: min_target changes between notes IFF calibration/settings change
  // TODO scale is only dynamic for timbre envelopes
  int16_t scale = max_target - min_target; // TODO overflow?
  min_target <<= 16;
  // TODO can delay calculating sustain level until decay is triggered
  int32_t peak = min_target + scale * adsr.peak;
  int32_t sustain = min_target + scale * adsr.sustain;

  attack_.nominal_samples = adsr.attack;
  attack_.nominal_offset = min_target;
  attack_.target = peak;

  decay_.nominal_samples = adsr.decay;
  decay_.nominal_offset = peak;
  decay_.target = sustain;

  release_.nominal_samples = adsr.release;
  release_.nominal_offset = sustain;
  release_.target = min_target;

  // TODO could precompute decay/release slopes here, don't have to wait for them to arrive.  downside is that decay can be skipped (release cannot).  Trigger would need to know whether to use the precomputed slope or compute a new one.

  if (stage_ > ENV_STAGE_SUSTAIN) {
    // multi.PrintInt32E(value_);
    Trigger(ENV_STAGE_ATTACK);
  }
}

int16_t Envelope::tremolo(const uint16_t strength) const {
  int32_t relative_value = (value_ - release_.target) >> 16;
  return relative_value * -strength >> 16;
}

void Envelope::Trigger(EnvelopeStage stage) {
  // multi.PrintDebugByte(0x09 + (stage << 4));

  stage_ = stage;
  expo_ = NULL;
  switch (stage) {
    case ENV_STAGE_ATTACK:
      expo_ = &attack_;
      break;
    case ENV_STAGE_DECAY:
      expo_ = &decay_;
      break;
    case ENV_STAGE_RELEASE:
      expo_ = &release_;
      break;
    default: return;
  }

  const int32_t nominal_scale = SatSub(expo_->target, expo_->nominal_offset);
  const int32_t measured_scale = SatSub(expo_->target, value_);
  const bool movement_expected = nominal_scale != 0;

  // Skip stage if there is a direction disagreement or nowhere to go
  if (
    // Already at target (important to skip because 0 disrupts direction checks)
    !measured_scale || (
      // The stage is supposed to have a direction
      movement_expected && (
        // It doesn't agree with the actual direction
        (nominal_scale > 0) != (measured_scale > 0)
      )
      // Cases: NoteOn during release from above peak level
      // TODO are direction skips good in case of polarity reversals? only if there are non-attack cases
    )
  ) {
    // Seeing some of these after a totally normal ADS to 0 sustain, probably indicating underflow.  How to avoid overshoot?
    // multi.PrintDebugByte(0x0F + (stage << 4));
    return Trigger(static_cast<EnvelopeStage>(stage + 1));
  }

  // Skip beginning of stage if closer to target than expected
  // Cases: NoteOn during release (of same polarity); NoteOff from below sustain level during attack 
  const bool truncate_start = movement_expected && (
    // NB: we already ruled out direction disagreement
    abs(measured_scale) < abs(nominal_scale)
  );

  // Determine X and Y distance to travel during this stage
  int32_t final_scale;
  size_t stage_samples;
  if (truncate_start) {
    // Keep the nominal stage steepness
    final_scale = nominal_scale;

    // Pre-advance X to reflect Y already traveled
    // TODO performance here may be painfully bad
    // TODO the result, stage_samples, could be shared between voice's envelopes
    const int32_t y_progress_amount = SatSub(value_, expo_->nominal_offset);
    const int32_t y_progress_fraction_16 = y_progress_amount / (final_scale >> 16);
    const uint16_t x_progress_fraction = Interpolate88(lut_env_inverse_expo, stmlib::ClipU16(y_progress_fraction_16));
    const uint16_t x_remaining_fraction = UINT16_MAX - x_progress_fraction;
    
    // We assume max lut_envelope_sample_counts is 2^18, so we only need 2 prelim downshifts
    stage_samples = ((expo_->nominal_samples >> 1) * static_cast<size_t>(x_remaining_fraction >> 1)) >> 14;
    if (!stage_samples) return Trigger(static_cast<EnvelopeStage>(stage + 1));
    // multi.PrintDebugByte(0x0B + (stage << 4));
  } else {
    // We're at least as far as expected (possibly farther).  Make the curve as steep as needed to cover the Y distance in the expected time
    // Cases: NoteOff during attack/decay from between sustain/peak levels; NoteOn during release of opposite polarity (hi timbre); normal well-adjusted stages
    final_scale = measured_scale;

    stage_samples = expo_->nominal_samples;
    // multi.PrintDebugByte(0x0A + (stage << 4));
  }  

  /*
    Slope calculation methods, from most accurate/slow to least:
    1. 2 mults per edge: calculate a scaled expo endpoint for each edge (1 mult) and a delta from the last endpoint, then calculate a slope from the delta (1 mult)
    2. 1 mult per edge: scale linear slope via LUT of ratios in 3.13 or 2.14 format 
    4. 0 mults per edge: if function has a constant 2nd derivative, generate a single scaled number that is *added* to the slope each slice, e.g. -x² + 2x
      - https://claude.ai/chat/2f2083f0-11b9-42ea-a62c-f85652cda175
    3. 0 mults per edge: scale linear slope with bit shifts
  */

  // uint32_t phase_increment = Interpolate88(lut_envelope_phase_increments, expo_->duration >> (8 - 5));
  // int32_t linear_slope = (static_cast<int64_t>(final_scale) * phase_increment) >> 32;
  const int32_t linear_slope = final_scale / static_cast<int32_t>(expo_->nominal_samples);
  if (!linear_slope) return Trigger(static_cast<EnvelopeStage>(stage + 1));
  const uint32_t slope_for_clz = static_cast<uint32_t>(abs(linear_slope >= 0 ? linear_slope : linear_slope + 1));
  const uint8_t max_shift = __builtin_clzl(slope_for_clz) - 1; // 0..31

  // Set current_edge_, edge slopes, and edge samples
  const size_t edge_samples = expo_->nominal_samples >> kEdgeBits;
  const size_t remainder_samples = stage_samples % edge_samples;
  if (truncate_start) {
    uint8_t num_full_edges = stage_samples / edge_samples;
    current_edge_ = kNumEdges - num_full_edges;
    for (uint8_t i = current_edge_; i < kNumEdges; i++) {
      edges_[i].samples = edge_samples;
      edges_[i].slope = compute_edge_slope(linear_slope, i, max_shift);
    }
    // All remainder samples go in a partial beginning edge
    if (remainder_samples) {
      current_edge_--;
      edges_[current_edge_].samples = remainder_samples;
      edges_[current_edge_].slope = compute_edge_slope(linear_slope, current_edge_, max_shift);
    }
  } else {
    // Use euclidean pattern to distribute remainder samples among edges
    const uint16_t euclidean_pattern_offset = static_cast<uint16_t>(kNumEdges - 1) << 5;
    const uint32_t euclidean_pattern = lut_euclidean[euclidean_pattern_offset + remainder_samples];
    current_edge_ = 0;
    for (uint8_t i = 0; i < kNumEdges; i++) {
      const uint32_t mask = static_cast<uint32_t>(1) << (i % kNumEdges);
      const size_t remainder_contribution = (euclidean_pattern & mask) ? 1 : 0;
      edges_[i].samples = edge_samples + remainder_contribution;
      edges_[i].slope = compute_edge_slope(linear_slope, i, max_shift);
    }
  }
}

int32_t Envelope::compute_edge_slope(int32_t linear_slope, uint8_t edge, uint8_t max_shift) const {
  int8_t shift = lut_expo_slope_shift[edge];
  return shift >= 0
    ? linear_slope << std::min(static_cast<uint8_t>(shift), max_shift)
    : linear_slope >> static_cast<uint8_t>(-shift);
}

/* Render TODO

Try copying expo slope to local

Separate inline buffer of length 4-8?
Write to output buffer 4 samples at a time?

Try outputting slopes instead, then computing a running total
https://claude.ai/chat/127cae75-04b9-4d95-87ac-d01114bd7cf3
Complication: shifting slopes to 16-bit before summing will cause error
Running total must be 32-bit, slope vector must be 32-bit, output buffer can be 16-bit

Options for vector accumulator:
1. Offset slope vector
  - Render buffer of envelope slopes
    - Have to handle corners
  - Offset envelope slope buffer by interpolator slope
  - Compute prefix sum of slope buffer
2. Apply slope/value bias inline, like an idiot
  - Optionally MAKE_BIASED_EXPO_SLOPES
3. Sum 2 q15 vectors
  - Render buffer of interpolator values (with prefix sum?)
  - Sum interpolator value buffer with envelope buffer
4. In-place addition on single buffer
  - Render buffer of interpolator values (with prefix sum?)
  - In render loop, SatAdd with envelope value (into same buffer)
*/

#define RENDER_LOOP(samples_exp, slope_exp) \
  const int32_t slope = (slope_exp); \
  size_t samples = (samples_exp); \
  do { \
    value += slope; \
    bias += bias_slope; \
    int32_t biased_value = (value >> 16) + (bias >> 16); \
    local_buffer[local_buffer_index++] = Clip16(biased_value); \
  } while (--samples);

template<size_t BUFFER_SIZE>
void Envelope::RenderSamples(
  stmlib::RingBuffer<int16_t, BUFFER_SIZE>* buffer, 
  int32_t new_bias
) {
  size_t samples_needed = kAudioBlockSize; // Even if double buffering
  int16_t local_buffer[kAudioBlockSize];
  size_t local_buffer_index = 0;

  int32_t value = value_;
  int32_t bias = bias_;
  const int32_t bias_slope = ((new_bias >> 1) - (bias >> 1)) >> (kAudioBlockSizeBits - 1);

  while (true) {
    if (expo_) {
      const Edge edge = edges_[current_edge_];
      const bool finish_edge = samples_needed >= edge.samples;
      const size_t edge_samples_to_render = finish_edge ? edge.samples : samples_needed;

      RENDER_LOOP(edge_samples_to_render, edge.slope);

      if (finish_edge) {
        // Advance edge/stage as needed
        samples_needed -= edge.samples;
        current_edge_++;
        // multi.PrintDebugByte(0x0C + (current_edge_ << 4));
        if (current_edge_ == kNumEdges) { // Stage is done
          // TODO this may glitch, but hard to reconstruct value_ from biased_value because we are somewhere between old and new bias
          value = value_ = expo_->target;
          Trigger(static_cast<EnvelopeStage>(stage_ + 1));
          // what happens to biased_value_31 if we go to SUSTAIN/DEAD?  prob fucked
        }
        if (!samples_needed) break;
      } else {
        // Save edge state
        edges_[current_edge_].samples -= edge_samples_to_render;
        break;
      }
    } else {
      RENDER_LOOP(samples_needed, 0);
      break;
    }
  }

  value_ = value;
  bias_ = bias;

  std::copy(local_buffer, local_buffer + kAudioBlockSize, buffer->write_ptr());
  buffer->advance_write_ptr(kAudioBlockSize);
}

template void Envelope::RenderSamples(
  stmlib::RingBuffer<int16_t, kAudioBlockSize>* buffer, int32_t new_bias);
template void Envelope::RenderSamples(
  stmlib::RingBuffer<int16_t, kAudioBlockSize * 2>* buffer, int32_t new_bias);

}  // namespace yarns
