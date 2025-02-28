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

#ifndef YARNS_ENVELOPE_H_
#define YARNS_ENVELOPE_H_

#include "stmlib/stmlib.h"
#include "stmlib/utils/ring_buffer.h"
#include "stmlib/utils/dsp.h"

#include "yarns/resources.h"

namespace yarns {

using namespace stmlib;

const size_t kAudioBlockSizeBits = 6;
const size_t kAudioBlockSize = 1 << kAudioBlockSizeBits;

const uint8_t kLutExpoSlopeShiftSizeBits = 4;
STATIC_ASSERT(
  1 << kLutExpoSlopeShiftSizeBits == LUT_EXPO_SLOPE_SHIFT_SIZE,
  expo_slope_shift_size
);

enum EnvelopeSegment {
  ENV_SEGMENT_ATTACK,           // manual start, auto/manual end
  ENV_SEGMENT_DECAY,            // auto start, auto/manual end
  ENV_SEGMENT_SUSTAIN,          // no motion
  ENV_SEGMENT_RELEASE_PRELUDE,  // manual start, auto end
  ENV_SEGMENT_RELEASE,          // manual start, auto end
  ENV_SEGMENT_DEAD,             // no motion
  ENV_NUM_SEGMENTS,
};

// Manual start edge cases: too far from target (prelude), too close to target (truncate beginning), wrong direction (skip?)
// Can also apply to decay start if attack was skipped due to wrong direction

struct ADSR {
  uint16_t peak, sustain; // Platonic, unscaled targets
  uint32_t attack, decay, release; // Timing
};

struct Motion {
  int32_t target, delta;
  uint32_t phase_increment;

  void Set(int32_t _start, int32_t _target, uint32_t _phase_increment) {
    target = _target;
    delta = _target - _start;
    phase_increment = _phase_increment;
  }

  int32_t compute_linear_slope() const { // Divide delta by duration
    int32_t s = (static_cast<int64_t>(delta) * phase_increment) >> 32;
    if (!s) s = delta >= 0 ? 1 : -1;
    return s;
  }

  static uint8_t max_shift(int32_t n) {
    uint32_t n_unsigned = abs(n >= 0 ? n : n + 1);
    return __builtin_clzl(n_unsigned) - 1; // 0..31
  }

  static int32_t compute_expo_slope(
    int32_t linear_slope, int8_t shift, uint8_t max_shift
  ) {
    return shift >= 0
      ? linear_slope << std::min(static_cast<uint8_t>(shift), max_shift)
      : linear_slope >> static_cast<uint8_t>(-shift);
  }

};

class Envelope {
 public:
  Envelope() { }
  ~Envelope() { }

  void Init(int32_t value) {
    value_ = value;
    Trigger(ENV_SEGMENT_DEAD);
    std::fill(
      &expo_slope_[0],
      &expo_slope_[LUT_EXPO_SLOPE_SHIFT_SIZE],
      0
    );
  }

  void NoteOff() {
    if (segment_ == ENV_SEGMENT_SUSTAIN) {
      // Normal release
      Trigger(ENV_SEGMENT_RELEASE);
    } else {
      // TODO If we are "above" sustain level, do a release prelude.  If we are "below" sustain level, skip some portion of the beginning of the release.
      Trigger(ENV_SEGMENT_RELEASE_PRELUDE);
    }
  }

  void NoteOn(
    ADSR& adsr,
    int32_t min_target, int32_t max_target // Actual bounds, 16-bit signed
  ) {
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

  int16_t tremolo(uint16_t strength) const {
    int32_t relative_value = (value_ - release_.target) >> 16;
    return relative_value * -strength >> 16;
  }
  
  // Populates expo slope table for the new segment
  void Trigger(EnvelopeSegment segment) {
    // Irrelevant now bc RELEASE_PRELUDE comes after sustain
    // if (gate_ && segment == ENV_SEGMENT_RELEASE_PRELUDE) {
    //   segment = ENV_SEGMENT_SUSTAIN; // Skip early-release when gate is high
    // }
    // // Irrelevant now because RELEASE_PRELUDE should transition directly into RELEASE
    // if (!gate_ && segment == ENV_SEGMENT_SUSTAIN) {
    //   segment = ENV_SEGMENT_RELEASE; // Skip sustain when gate is low
    // }
    // autonomous transitions that rely on incrementing segment:
    // attack -> decay: increment is fine
    // decay -> sustain: increment is fine
    // release_prelude -> release: increment is fine
    // release -> dead: increment is fine
    // TODO what about skips below?

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
      int8_t steepest_shift = lut_expo_slope_shift[0];
      int32_t steepest_expo_slope = Motion::compute_expo_slope(
        linear_slope, steepest_shift, Motion::max_shift(linear_slope)
      );
      // Use the steepest release slope for this entire segment
      std::fill(
        &expo_slope_[0],
        &expo_slope_[LUT_EXPO_SLOPE_SHIFT_SIZE],
        steepest_expo_slope
      );
      int32_t delta_to_sustain = decay_.target - value_;
      release_prelude_.phase_increment = (steepest_expo_slope / (delta_to_sustain >> 16)) << 16;
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
        // TODO truncating phase based on delta-of-deltas is hard due to exp!
        attack_.delta = positive_segment_slope
          ? std::max(nominal_delta, attack_.delta)
          : std::min(nominal_delta, attack_.delta);
      } else if (segment == ENV_SEGMENT_RELEASE) {
        // If we are below sustain level due to an aborted attack
      }

      int32_t linear_slope = motion_->compute_linear_slope();
      uint8_t max_shift = Motion::max_shift(linear_slope);
      for (uint8_t i = 0; i < LUT_EXPO_SLOPE_SHIFT_SIZE; ++i) {
        int8_t shift = lut_expo_slope_shift[i];
        expo_slope_[i] = Motion::compute_expo_slope(linear_slope, shift, max_shift);
      }  
    }
  }

  template<size_t BUFFER_SIZE> // To allow both double and single buffering
  void RenderSamples(stmlib::RingBuffer<int16_t, BUFFER_SIZE>* buffer, int32_t value_bias, int32_t slope_bias) {
    // NB: theoretically it would be nice if we could pick up on a NoteOn/NoteOff in the render loop and immediately change direction.  However, MIDI input processing is synchronous with regard to rendering, so this scenario does not arise and there's no point supporting it.

    // TODO try copying slope table into locals

    int32_t value = value_ + value_bias;
    uint32_t phase = phase_;
    uint32_t phase_increment = motion_ ? motion_->phase_increment : 0;
    // int16_t* buffer_start = buffer->write_ptr();
    size_t size = kAudioBlockSize;
    while (size--) {
      // TODO Decrement int32 phase and check greater than 0? Maybe try for osc too
      phase += phase_increment;
      if (phase < phase_increment) {
        value = motion_->target;
        Trigger(static_cast<EnvelopeSegment>(segment_ + 1));
        phase = 0;
        phase_increment = motion_ ? motion_->phase_increment : 0;
      }
      // TODO try outputting slopes instead, then computing a running total
      // https://claude.ai/chat/127cae75-04b9-4d95-87ac-d01114bd7cf3
      // Complication: shifting slopes before summing will cause error
      // Running total must be 32-bit, slope vector must be 32-bit, output buffer can be 16-bit
      // Separate inline buffer of length 4-8?
      // Also need to calculate a delta on segment change
      int32_t slope = expo_slope_[phase >> (32 - kLutExpoSlopeShiftSizeBits)];
      value += slope + slope_bias;
      buffer->Overwrite(value >> 16);
    }
    value_ = value - value_bias;
    phase_ = phase;
  }

  int16_t value() const { return value_ >> 16; }

 private:
  Motion attack_, decay_, release_, release_prelude_;
  Motion* motion_;
  
  // Current segment.
  EnvelopeSegment segment_;
  
  int32_t value_;

  // Maps slices of the phase to slopes, approximating an exponential curve
  int32_t expo_slope_[LUT_EXPO_SLOPE_SHIFT_SIZE];

  uint32_t phase_;

  DISALLOW_COPY_AND_ASSIGN(Envelope);
};

}  // namespace yarns

#endif  // YARNS_ENVELOPE_H_
