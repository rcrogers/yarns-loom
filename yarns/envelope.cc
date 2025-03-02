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

#include "stmlib/utils/dsp.h"

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

void Motion::Set(int32_t _start, int32_t _target, uint32_t _phase_increment) {
  target = _target;
  delta = _target - _start;
  phase_increment = _phase_increment;
}

int32_t Motion::compute_linear_slope() const {
  int32_t s = (static_cast<int64_t>(delta) * phase_increment) >> 32;
  if (!s) s = delta >= 0 ? 1 : -1;
  return s;
}

uint8_t Motion::max_shift(int32_t n) {
  uint32_t n_unsigned = abs(n >= 0 ? n : n + 1);
  return __builtin_clzl(n_unsigned) - 1; // 0..31
}

int32_t Motion::compute_expo_slope(
  int32_t linear_slope, int8_t shift, uint8_t max_shift
) {
  return shift >= 0
    ? linear_slope << std::min(static_cast<uint8_t>(shift), max_shift)
    : linear_slope >> static_cast<uint8_t>(-shift);
}

void Envelope::Init(int32_t value) {
  value_ = value;
  Trigger(ENV_SEGMENT_DEAD);
}

void Envelope::NoteOff() {
  if (segment_ == ENV_SEGMENT_SUSTAIN) {
    // Normal release
    Trigger(ENV_SEGMENT_RELEASE);
  } else {
    Trigger(ENV_SEGMENT_RELEASE_PRELUDE);
  }
}

void Envelope::NoteOn(
  ADSR& adsr,
  int32_t min_target, int32_t max_target // Actual bounds, 16-bit signed
) {
  // NB: min_target changes between notes IFF calibration/settings change
  int16_t scale = max_target - min_target;
  min_target <<= 16;
  int32_t peak = min_target + scale * adsr.peak;
  int32_t sustain = min_target + scale * adsr.sustain;

  // NB: attack_.delta is actual, not nominal
  attack_.Set(value_, peak, adsr.attack);
  decay_.Set(peak, sustain, adsr.decay);
  release_.Set(sustain, min_target, adsr.release);

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

// Populates expo slope table for the new segment
void Envelope::Trigger(EnvelopeSegment segment) {
  segment_ = segment;
  switch (segment) {
    case ENV_SEGMENT_ATTACK : motion_ = &attack_  ; break;
    case ENV_SEGMENT_DECAY  : motion_ = &decay_   ; break;
    case ENV_SEGMENT_RELEASE: motion_ = &release_ ; break;
    case ENV_SEGMENT_RELEASE_PRELUDE: motion_ = &release_prelude_; break;
    case ENV_SEGMENT_SUSTAIN:
    case ENV_SEGMENT_DEAD:
    case ENV_NUM_SEGMENTS: // Should never happen
      motion_ = NULL;
  }

  phase_ = 0;
  if (!motion_) { // No motion => no expo slopes
    std::fill(
      &expo_slope_[0],
      &expo_slope_[LUT_EXPO_SLOPE_SHIFT_SIZE],
      0
    );
    return;
  }

  if (segment == ENV_SEGMENT_RELEASE_PRELUDE) {
    // RELEASE_PRELUDE heads for the sustain level at RELEASE's steepest
    // slope, then RELEASE begins.
    int32_t linear_slope = release_.compute_linear_slope();
    int32_t prelude_slope = Motion::compute_expo_slope(
      linear_slope, lut_expo_slope_shift[0], Motion::max_shift(linear_slope)
    );
    // Use the steepest release slope for this entire segment
    std::fill(
      &expo_slope_[0],
      &expo_slope_[LUT_EXPO_SLOPE_SHIFT_SIZE],
      prelude_slope
    );
    int32_t delta_to_nominal_start = decay_.target - value_;
    release_prelude_.phase_increment = (prelude_slope / (delta_to_nominal_start >> 16)) << 16;
    release_prelude_.samples_left = UINT32_MAX / release_prelude_.phase_increment;
  } else { // Build normal expo slopes

    // TODO handle upward decay?? no longer covered by wrong-direction logic

    if (segment == ENV_SEGMENT_ATTACK) {
      // In case the attack is not starting from 0 (e.g. it interrupts a
      // still-high release), adjust its timing and slope to try to match the
      // nominal sound and feel
      int32_t nominal_delta = attack_.target - release_.target;
      bool positive_segment_slope = nominal_delta >= 0;
      if (positive_segment_slope != (attack_.delta >= 0)) {
        // If deltas differ in sign, the direction is wrong -- skip segment
        return Trigger(static_cast<EnvelopeSegment>(segment + 1));
      }
      // Pick the larger delta, and thus the steeper slope that reaches the
      // target more quickly.  If actual delta is smaller than nominal (e.g.
      // from re-attacks that begin high), use nominal's steeper slope (e.g.
      // so the attack sounds like a quick catch-up vs a flat, blaring hold
      // stage). If actual is greater (rare in practice), use that

      // TODO "reaching the target" doesn't work now, need to truncate the segment
      attack_.delta = positive_segment_slope
        ? std::max(nominal_delta, attack_.delta)
        : std::min(nominal_delta, attack_.delta);
    } else if (segment == ENV_SEGMENT_RELEASE) {
      // TODO If we are below sustain level due to an aborted attack
    }

    int32_t linear_slope = motion_->compute_linear_slope();
    motion_->samples_left = UINT32_MAX / motion_->phase_increment;
    uint8_t max_shift = Motion::max_shift(linear_slope);
    // TODO fast segments (0.1ms) can span multiple slices in a single sample, which means the steep initial expo slope will be in effect for more of the phase than intended, potentially causing significant error.  fall back on linear slope if samples_left does not exceed LUT_EXPO_SLOPE_SHIFT_SIZE by 1x (0.2ms segment) or 2x (0.4ms segment)?
    for (uint8_t i = 0; i < LUT_EXPO_SLOPE_SHIFT_SIZE; ++i) {
      int8_t shift = lut_expo_slope_shift[i];
      expo_slope_[i] = Motion::compute_expo_slope(linear_slope, shift, max_shift);
    }
  }
}

#define MAKE_BIASED_EXPO_SLOPES() \
  int32_t biased_expo_slopes[LUT_EXPO_SLOPE_SHIFT_SIZE]; \
  for (uint8_t i = 0; i < LUT_EXPO_SLOPE_SHIFT_SIZE; ++i) { \
    biased_expo_slopes[i] = expo_slope_[i] + slope_bias; \
  }

/*
TODO try copying expo slope to local
TODO try outputting slopes instead, then computing a running total
https://claude.ai/chat/127cae75-04b9-4d95-87ac-d01114bd7cf3
Complication: shifting slopes before summing will cause error
Running total must be 32-bit, slope vector must be 32-bit, output buffer can be 16-bit
Separate inline buffer of length 4-8?
Also need to calculate a delta on segment change
Alternately, feasible for interpolator to compute prefix sum buffer?
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

template<size_t BUFFER_SIZE> // To allow both double and single buffering
// void Envelope::RenderSamples(stmlib::RingBuffer<int16_t, BUFFER_SIZE>* buffer, int32_t value_bias, int32_t slope_bias) {
void Envelope::RenderSamples(stmlib::RingBuffer<int16_t, BUFFER_SIZE>* buffer, int32_t value_bias, int32_t slope_bias, size_t samples_to_render) {
  // NB: theoretically it would be nice if we could pick up on a NoteOn/NoteOff in the render loop and immediately change direction.  However, MIDI input processing is synchronous with regard to rendering, so this scenario does not arise and there's no point supporting it.

  // TODO could template this
  if (!motion_) {
    while (samples_to_render--) {
      buffer->Overwrite(value_ >> 16);
    }
    return;
  }

  uint32_t phase = phase_;
  uint32_t phase_increment = motion_->phase_increment;

  uint32_t samples_until_trigger = motion_->samples_left;
  bool will_trigger = samples_to_render >= samples_until_trigger;
  size_t samples_pre_trigger = will_trigger ? samples_until_trigger : samples_to_render;
  size_t samples_post_trigger = samples_to_render - samples_until_trigger;

  // int32_t biased_value = value_ + value_bias;
  int32_t value = value_;
  // int16_t* buffer_start = buffer->write_ptr();
  while (samples_pre_trigger--) {
    phase += phase_increment;
    int32_t slope = expo_slope_[phase >> (32 - kLutExpoSlopeShiftSizeBits)];
    /* slope += slope_bias; */
    value += slope;
    buffer->Overwrite(value >> 16);
  }

  if (will_trigger) { // Mid-segment state is irrelevant
    value_ = motion_->target;
    Trigger(static_cast<EnvelopeSegment>(segment_ + 1));
    if (samples_post_trigger) {
      // NB: this may cause another Trigger if next segment is short
      return RenderSamples(buffer, value_bias, slope_bias, samples_post_trigger);
    }
  } else { // We're still mid-segment, so save its state
    value_ = value;
    phase_ = phase;
    motion_->samples_left -= samples_to_render;
  }
}

template void Envelope::RenderSamples(stmlib::RingBuffer<int16_t, kAudioBlockSize>* buffer, int32_t value_bias, int32_t slope_bias, size_t samples_to_render);
template void Envelope::RenderSamples(stmlib::RingBuffer<int16_t, kAudioBlockSize * 2>* buffer, int32_t value_bias, int32_t slope_bias, size_t samples_to_render);

}  // namespace yarns
