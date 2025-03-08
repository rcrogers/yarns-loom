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

namespace yarns {

using namespace stmlib;

/*
Edge cases when a manually started segment has an off-nominal delta to target:
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
    - Primary case: NoteOff before SUSTAIN segment has started
    - Can also apply to decay start, e.g.:
      - "High attack interrupted by low attack" causes ATTACK to skip to DECAY
      - ATTACK skips to decay
      - DECAY is now further from sustain level than expected
  - Solution: prelude
    - Populate the segment's expo slopes
    - Extend the segment's initial/steepest slope backward in time
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
  phase_ = 0;
  output_bias_ = 0;
  segment_ = ENV_SEGMENT_DEAD;
  motion_ = NULL;
  segment_samples_ = 0;
  // std::fill(
  //   &expo_slope_[0],
  //   &expo_slope_[LUT_EXPO_SLOPE_SHIFT_SIZE],
  //   0
  // );
  // Trigger(ENV_SEGMENT_DEAD);
}

void Envelope::NoteOff() {
  if (segment_ < ENV_SEGMENT_RELEASE) {
    Trigger(ENV_SEGMENT_RELEASE, true);
  }
}

void Envelope::NoteOn(
  ADSR& adsr,
  int32_t min_target, int32_t max_target // Actual bounds, 16-bit signed
) {
  // NB: min_target changes between notes IFF calibration/settings change
  int16_t scale = max_target - min_target; // TODO overflow?
  min_target <<= 16;
  int32_t peak = min_target + scale * adsr.peak;
  int32_t sustain = min_target + scale * adsr.sustain;

  attack_.expected_start = min_target;
  attack_.target = peak;
  attack_.phase_increment = adsr.attack;
  attack_.delta_31 = 0;
  attack_.actual_start = 0;

  decay_.expected_start = peak;
  decay_.target = sustain;
  decay_.phase_increment = adsr.decay;
  decay_.delta_31 = 0;
  decay_.actual_start = 0;

  release_.expected_start = sustain;
  release_.target = min_target;
  release_.phase_increment = adsr.release;
  release_.delta_31 = 0;
  release_.actual_start = 0;

  // TODO could precompute decay/release slopes here, don't have to wait for them to arrive.  downside is that decay can be skipped (release cannot).  Trigger would need to know whether to use the precomputed slope or compute a new one.

  if (segment_ > ENV_SEGMENT_SUSTAIN) {
    multi.PrintInt32E(value_);
    Trigger(ENV_SEGMENT_ATTACK, true);
  }
}

int16_t Envelope::tremolo(uint16_t strength) const {
  int32_t relative_value = (value_ - release_.target) >> 16;
  return relative_value * -strength >> 16;
}

#define DIFF_DOWNSHIFT(a, b) \
  (((a) >> 1) - ((b) >> 1));

void Envelope::Trigger(EnvelopeSegment segment, bool manual) {
  segment_ = segment;
  switch (segment) {
    case ENV_SEGMENT_ATTACK : motion_ = &attack_  ; break;
    case ENV_SEGMENT_DECAY  : motion_ = &decay_   ; break;
    case ENV_SEGMENT_RELEASE: motion_ = &release_ ; break;
    default:
      motion_ = NULL;
      return;
  }  

  int32_t nominal_delta_31 = DIFF_DOWNSHIFT(motion_->target, motion_->expected_start);
  int32_t actual_delta_31 = DIFF_DOWNSHIFT(motion_->target, value_);
  bool movement_expected = nominal_delta_31 != 0;

  // Skip segment if there is a direction disagreement or nowhere to go
  if (
    // Already at target (important to skip because 0 disrupts direction checks)
    !actual_delta_31 || (
      // The segment is supposed to have a direction
      movement_expected && (
        // It doesn't agree with the actual direction
        (nominal_delta_31 > 0) !=
        (actual_delta_31 > 0)
      )
      // Cases: NoteOn during release from above peak level
      // TODO are direction skips good in case of polarity reversals? only if there are non-attack cases
    )
  ) {
    // Seeing some of these after a totally normal ADS to 0 sustain, probably indicating underflow.  How to avoid overshoot?
    // multi.PrintDebugByte(0x0F + (segment << 4));
    return Trigger(static_cast<EnvelopeSegment>(segment + 1), manual);
  }

  // multi.PrintDebugByte(0x09 + (segment << 4));

  // Determine X and Y distance to travel during this segment
  motion_->actual_start = value_;
  if ( // Closer to target than expected
    movement_expected && (
      // NB: we already ruled out direction disagreement
      abs(actual_delta_31) < abs(nominal_delta_31)
    )
  ) {
    // Cases: NoteOn during release (of same polarity); NoteOff from below sustain level during attack 

    // TODO this pathway is still glitchy, math wrong?
    // multi.PrintDebugByte(0x0B + (segment << 4));

    // Keep the nominal segment contour
    motion_->delta_31 = nominal_delta_31;

    // Pre-advance X to reflect Y already traveled
    int32_t delta_amount_completed_31 = DIFF_DOWNSHIFT(value_, motion_->expected_start);
    // TODO possible overflow?
    int32_t delta_total_23 = motion_->delta_31 >> 8;
    uint8_t delta_fraction_completed_8 = delta_total_23 ? delta_amount_completed_31 / delta_total_23 : 0;
    // This lookup assumes lut_env_expo is roughly symmetric across the line y = 1 - x, so it can serve as its own inverse function
    // TODO no longer true!
    STATIC_ASSERT(LUT_ENV_EXPO_SIZE == UINT8_MAX + 2, lut_env_expo_size);
    phase_ = (UINT16_MAX - lut_env_expo[UINT8_MAX - delta_fraction_completed_8]) << 16;
    // if (segment == ENV_SEGMENT_ATTACK) multi.PrintInt32E(phase_);
  } else {
    // We're at least as far as expected (possibly farther).  Make the curve as steep as needed to cover the Y distance in the expected time
    // Cases: NoteOff during attack/decay from between sustain/peak levels; NoteOn during release of opposite polarity (hi timbre); normal well-adjusted segments
    // multi.PrintDebugByte(0x0A + (segment << 4));
    motion_->delta_31 = actual_delta_31;
    phase_ = 0;
  }
  segment_samples_ = (UINT32_MAX - phase_) / motion_->phase_increment;

  // // TODO any advantage to calculating this from samples_left_?  maybe messed up by phase skipping
  // int32_t linear_slope = (static_cast<int64_t>(motion_->actual_delta_31) * motion_->phase_increment) >> 31;
  // // multi.PrintInt32E(linear_slope);
  // int32_t minimal_slope = actual_delta_31 > 0 ? 1 : -1;
  // if (!linear_slope) linear_slope = minimal_slope;

  // // Create segment curve from linear slope
  // if (motion_->phase_increment >= (UINT32_MAX / (LUT_EXPO_SLOPE_SHIFT_SIZE * 2))) {
  //   // This segment is so short that the expo slope slices are on the order of 1 sample, which will cause significant error.  Fall back on linear slope.
  //   std::fill(
  //     &expo_slope_[0],
  //     &expo_slope_[LUT_EXPO_SLOPE_SHIFT_SIZE],
  //     linear_slope
  //   );
  // } else {
  //   uint32_t slope_for_clz = abs(linear_slope >= 0 ? linear_slope : linear_slope + 1);
  //   uint8_t max_shift = __builtin_clzl(slope_for_clz) - 1; // 0..31
  //   for (uint8_t i = 0; i < LUT_EXPO_SLOPE_SHIFT_SIZE; ++i) {
  //     int8_t shift = lut_expo_slope_shift[i];
  //     int32_t expo_slope = shift >= 0
  //       ? linear_slope << std::min(static_cast<uint8_t>(shift), max_shift)
  //       : linear_slope >> static_cast<uint8_t>(-shift);
  //     if (!expo_slope) expo_slope = minimal_slope;
  //     expo_slope_[i] = expo_slope;
  //   }
  // }
}

/*
#define MAKE_BIASED_EXPO_SLOPES() \
  int32_t biased_expo_slopes[LUT_EXPO_SLOPE_SHIFT_SIZE]; \
  for (uint8_t i = 0; i < LUT_EXPO_SLOPE_SHIFT_SIZE; ++i) { \
    biased_expo_slopes[i] = expo_slope_[i] + slope_bias; \
  }
*/

/* Render TODO

Try copying expo slope to local

Separate inline buffer of length 4-8?
Write to output buffer 4 samples at a time?

Try outputting slopes instead, then computing a running total
https://claude.ai/chat/127cae75-04b9-4d95-87ac-d01114bd7cf3
Complication: shifting slopes before summing will cause error
Running total must be 32-bit, slope vector must be 32-bit, output buffer can be 16-bit

Options for vector accumulator:
1. Offset slope vector
  - Render buffer of envelope slopes
    - Have to handle corners
  - Offset envelope slope buffer by interpolator slope
  - Compute prefix sum of slope buffer
2. Apply slope/value bias inline, like an idiot
  - Optionally MAKE_BIASED_EXPO_SLOPES
3. Sum q15 value vectors (no prefix sum)
  - Render buffer of interpolator values
  - Sum interpolator value buffer with envelope buffer
4. 
*/

/*
#define MAKE_BIASED_EXPO_SLOPES() \
  int32_t biased_expo_slopes[LUT_EXPO_SLOPE_SHIFT_SIZE]; \
  for (uint8_t i = 0; i < LUT_EXPO_SLOPE_SHIFT_SIZE; ++i) { \
    biased_expo_slopes[i] = expo_slope_[i] + slope_bias; \
  }
*/

template<size_t BUFFER_SIZE>
void Envelope::RenderSamples(
  stmlib::RingBuffer<int16_t, BUFFER_SIZE>* buffer, 
  int32_t new_output_bias,
  size_t render_samples_needed
) {
  new_output_bias = 0; // TODO

  int32_t current_output_value = value_ + output_bias_;

  if (!motion_) {
    int32_t slope = (new_output_bias - output_bias_) / render_samples_needed;
    while (render_samples_needed--) {
      current_output_value += slope;
      buffer->Overwrite(current_output_value >> 16);
    }
    output_bias_ = new_output_bias;
    return;
  }
  output_bias_ = new_output_bias;

  // Linear interpolation over this render cycle
  size_t samples_rendered = std::min(segment_samples_, render_samples_needed);
  phase_ += motion_->phase_increment * samples_rendered;
  uint16_t expo_fraction = Interpolate824(lut_env_expo, phase_);
  int32_t motion_progress_31 = (static_cast<int64_t>(motion_->delta_31) * expo_fraction) >> 16;
  int32_t output_target_31 = (new_output_bias >> 1) + (motion_->actual_start >> 1) + motion_progress_31;
  CONSTRAIN(output_target_31, INT32_MIN >> 1, INT32_MAX >> 1); // TODO use min/max from noteon?
  int32_t render_delta_31 = DIFF_DOWNSHIFT(output_target_31 << 1, current_output_value);
  CONSTRAIN(render_delta_31, INT32_MIN >> 1, INT32_MAX >> 1);
  multi.PrintInt32E(render_delta_31);

  int32_t slope = (render_delta_31 / samples_rendered) << 1;
  for (size_t i = samples_rendered; i--; ) {
    current_output_value += slope;
    buffer->Overwrite(current_output_value >> 16);
  }

  if (samples_rendered == segment_samples_) {
    // Segment is complete
    value_ = motion_->target;
    Trigger(static_cast<EnvelopeSegment>(segment_ + 1), false);
    render_samples_needed -= samples_rendered;
    if (render_samples_needed) {
      // NB: may cause yet another Trigger if next segment is short
      return RenderSamples(buffer, new_output_bias, render_samples_needed);
    }
  } else {
    // We'll continue rendering this segment next time
    value_ = current_output_value - new_output_bias; // CONSTRAIN complicates this
    segment_samples_ -= samples_rendered;
  }
}

template void Envelope::RenderSamples(
  stmlib::RingBuffer<int16_t, kAudioBlockSize>* buffer, int32_t new_output_bias, size_t render_samples_needed);
template void Envelope::RenderSamples(
  stmlib::RingBuffer<int16_t, kAudioBlockSize * 2>* buffer, int32_t new_output_bias, size_t render_samples_needed);

}  // namespace yarns
