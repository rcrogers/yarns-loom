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

#include "yarns/multi.h" // TODO

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
  segment_ = ENV_SEGMENT_DEAD;
  motion_ = NULL;
  catchup_samples_ = 0;
  expo_samples_ = 0;
  std::fill(
    &expo_slope_[0],
    &expo_slope_[LUT_EXPO_SLOPE_SHIFT_SIZE],
    0
  );
  // Trigger(ENV_SEGMENT_DEAD);
}

void Envelope::NoteOff() {
  // multi.PrintDebugByte(0xEF);
  if (segment_ < ENV_SEGMENT_RELEASE) {
    Trigger(ENV_SEGMENT_RELEASE);
  }
}

void Envelope::NoteOn(
  ADSR& adsr,
  int32_t min_target, int32_t max_target // Actual bounds, 16-bit signed
) {
  // multi.PrintDebugByte(0xAB);
  // NB: min_target changes between notes IFF calibration/settings change
  int16_t scale = max_target - min_target;
  min_target <<= 16;
  int32_t peak = min_target + scale * adsr.peak;
  int32_t sustain = min_target + scale * adsr.sustain;

  attack_.expected_start = min_target;
  attack_.target = peak;
  attack_.phase_increment = adsr.attack;

  decay_.expected_start = peak;
  decay_.target = sustain;
  decay_.phase_increment = adsr.decay;

  release_.expected_start = sustain;
  release_.target = min_target;
  release_.phase_increment = adsr.release;

  // TODO could precompute decay/release slopes here, don't have to wait for them to arrive.  downside is that decay can be skipped (release cannot).  Trigger would need to know whether to use the precomputed slope or compute a new one.

  if (segment_ > ENV_SEGMENT_SUSTAIN) {
    // TODO skip to decay if we are already above peak? is a decay prelude needed?
    Trigger(ENV_SEGMENT_ATTACK);
  }
}

int16_t Envelope::tremolo(uint16_t strength) const {
  int32_t relative_value = (value_ - release_.target) >> 16;
  return relative_value * -strength >> 16;
}

void Envelope::Trigger(EnvelopeSegment segment) {
  // multi.PrintDebugByte(segment > ENV_SEGMENT_DEAD);

  segment_ = segment;
  switch (segment) {
    case ENV_SEGMENT_ATTACK : motion_ = &attack_  ; break;
    case ENV_SEGMENT_DECAY  : motion_ = &decay_   ; break;
    case ENV_SEGMENT_RELEASE: motion_ = &release_ ; break;
    case ENV_SEGMENT_SUSTAIN:
    case ENV_SEGMENT_DEAD:
    case ENV_NUM_SEGMENTS: // Should never happen
    // default: // Uncommenting this makes everything freeze
      motion_ = NULL;
      // multi.PrintDebugByte(reinterpret_cast<uintptr_t>(motion_) & 0xFF);
      multi.PrintDebugByte(0xA0 + segment);
      return;
  }
  
  int32_t expected_distance_to_target = motion_->target - motion_->expected_start;
  bool positive_slope_expected = expected_distance_to_target >= 0;
  
  if ((motion_->target - value_ >= 0) != positive_slope_expected) {
    // We're going in the wrong direction
    if (segment == ENV_SEGMENT_ATTACK) {
      // Skip to next segment for attack
      return Trigger(ENV_SEGMENT_DECAY);
    }
    // TODO skip decay?
  }
  
  // TODO is there any advantage to deriving expo_samples_ count first, and basing slope on that?

  // Set linear and expo slopes

  // int32_t linear_slope = (static_cast<int64_t>(expected_distance_to_target) * motion_->phase_increment) >> 32;
  // Cross multiply instead of using int64_t

  int32_t linear_slope = static_cast<int32_t>(
    multiply_64(expected_distance_to_target, motion_->phase_increment)
  );
  if (!linear_slope) linear_slope = positive_slope_expected ? 1 : -1;
  if (motion_->phase_increment >= (UINT32_MAX / (LUT_EXPO_SLOPE_SHIFT_SIZE * 2))) {
    // This segment is so short that the expo slope slices are on the order of 1 sample, which will cause significant error.  Fall back on linear slope.
    std::fill(
      &expo_slope_[0],
      &expo_slope_[LUT_EXPO_SLOPE_SHIFT_SIZE],
      linear_slope
    );
  } else {
    uint32_t slope_for_clz = abs(linear_slope >= 0 ? linear_slope : linear_slope + 1);
    uint8_t max_shift = __builtin_clzl(slope_for_clz) - 1; // 0..31
    for (uint8_t i = 0; i < LUT_EXPO_SLOPE_SHIFT_SIZE; ++i) {
      int8_t shift = lut_expo_slope_shift[i];
      expo_slope_[i] = shift >= 0
        ? linear_slope << std::min(static_cast<uint8_t>(shift), max_shift)
        : linear_slope >> static_cast<uint8_t>(-shift);
    }
  }
  
  // If Trigger was automatic, or non-interrupting manual, this should be 0
  int32_t catchup_distance = motion_->expected_start - value_;
  if ((catchup_distance >= 0) == positive_slope_expected) {
    // Expo phase will start from the beginning
    phase_ = 0;
    // May need some linear catchup before the expo phase begins
    catchup_samples_ = expo_slope_[0] ? catchup_distance / expo_slope_[0] : 0;
  } else {
    // We're closer to target than nominal, so fast-forward to a phase determined by the distance we've already traveled
    // int64_t completed_distance_40 = -catchup_distance << 8;
    // uint8_t value_fraction_completed = expected_distance_to_target ? completed_distance_40 / expected_distance_to_target : 0;
    uint8_t expected_distance_24 = expected_distance_to_target >> 8;
    uint8_t value_fraction_completed = expected_distance_24 ? -catchup_distance / expected_distance_24 : 0;
    // This lookup assumes lut_env_expo is roughly symmetric across the line y = 1 - x, so it can serve as its own inverse function
    phase_ = (UINT16_MAX - lut_env_expo[UINT8_MAX - value_fraction_completed]) << 16;
    catchup_samples_ = 0;
  }
  expo_samples_ = (UINT32_MAX - phase_) / motion_->phase_increment;
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

void Envelope::RenderSamples(
  stmlib::RingBuffer<int16_t, kAudioBlockSize * 2>* buffer, 
  int32_t value_bias,
  int32_t slope_bias, 
  size_t render_samples_needed
) {
  return RenderSamples(buffer, value_bias, slope_bias, motion_, render_samples_needed);
}

// template<size_t BUFFER_SIZE>
// void Envelope::RenderSamples(
//   stmlib::RingBuffer<int16_t, BUFFER_SIZE>* buffer, 
void Envelope::RenderSamples(
  stmlib::RingBuffer<int16_t, kAudioBlockSize * 2>* buffer, 
  int32_t value_bias,
  int32_t slope_bias, 
  Motion* force_motion,
  size_t render_samples_needed
) {
  int32_t value = value_;
  // int32_t biased_value = value_ + value_bias;
  uint32_t phase = phase_;
  uint32_t phase_increment = motion_->phase_increment;

  if (!force_motion) {
  // if (!motion_) {
  // if (!(segment_ == ENV_SEGMENT_ATTACK || segment_ == ENV_SEGMENT_DECAY || segment_ == ENV_SEGMENT_RELEASE)) {
    multi.PrintDebugByte(0xB0 + segment_); // Never prints on !motion_ check
    while (render_samples_needed--) { buffer->Overwrite(value >> 16); }
    return;
  }

  size_t catchup_samples_rendered = std::min(catchup_samples_, render_samples_needed);
  for (size_t i = catchup_samples_rendered; i--; ) {
    value += expo_slope_[0]; // Use the first (steepest) slope directly
    buffer->Overwrite(value >> 16);
  }
  render_samples_needed -= catchup_samples_rendered;

  size_t expo_samples_rendered = std::min(expo_samples_, render_samples_needed);
  for (size_t i = expo_samples_rendered; i--; ) {
    phase += phase_increment;
    int32_t slope = expo_slope_[phase >> (32 - kLutExpoSlopeShiftSizeBits)];
    /* slope += slope_bias; */
    value += slope;
    buffer->Overwrite(value >> 16);
  }
  render_samples_needed -= expo_samples_rendered;

  if (expo_samples_rendered == expo_samples_) { // Done rendering all samples for this segment
    // TODO needed to avoid wrong direction check? we probably don't want wrong direction for auto triggered segments, so this is good I think
    value_ = force_motion->target;
    
    // TODO why god why
    if (segment_ == ENV_SEGMENT_SUSTAIN) {
      // multi.PrintDebugByte(motion_ ? 1 : 2);
      // return;
    } else if (segment_ == ENV_SEGMENT_DEAD) {
      // multi.PrintDebugByte(motion_ ? 3 : 4);
      // return;
    }
    // With min attack/decay, this prints 0x3C and 0x38, indicating attack/decay rendered 4 samples each, exactly as expected
    // multi.PrintDebugByte(render_samples_needed & 0xFF);
    Trigger(static_cast<EnvelopeSegment>(segment_ + 1));
    // No change here
    // multi.PrintDebugByte(render_samples_needed & 0xFF);

    // WITH ADJUSTED MOTION CHECK:
    // RELEASE never prints here, because it's not triggered by the end of a segment
    // DEAD prints once
    // multi.PrintDebugByte(segment_);

    // Print whether motion_ is now null -- looks right
    // multi.PrintDebugByte(reinterpret_cast<uintptr_t>(motion_) & 0xFF);

    if (render_samples_needed) {
      // NB: may cause yet another Trigger if next segment is short
      return RenderSamples(buffer, value_bias, slope_bias, motion_, render_samples_needed);
    }
  } else { // We'll continue rendering this segment next time
    value_ = value;
    phase_ = phase;
    catchup_samples_ -= catchup_samples_rendered;
    expo_samples_ -= expo_samples_rendered;
  }
}

// template void Envelope::RenderSamples(stmlib::RingBuffer<int16_t, kAudioBlockSize>* buffer, int32_t value_bias, int32_t slope_bias, size_t render_samples_needed);
// template void Envelope::RenderSamples(stmlib::RingBuffer<int16_t, kAudioBlockSize * 2>* buffer, int32_t value_bias, int32_t slope_bias, size_t render_samples_needed);

}  // namespace yarns
