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

enum EnvelopeSegment {
  ENV_SEGMENT_ATTACK,
  ENV_SEGMENT_DECAY,
  ENV_SEGMENT_EARLY_RELEASE, // Gate ended before sustain, so skip sustain
  ENV_SEGMENT_SUSTAIN,
  ENV_SEGMENT_RELEASE,
  ENV_SEGMENT_DEAD,
  ENV_NUM_SEGMENTS,
};

struct ADSR {
  uint16_t peak, sustain; // Platonic, unscaled targets
  uint32_t attack, decay, release; // Timing
};

struct Motion {
  int32_t target, delta;
  uint32_t phase_increment;

  inline void Set(int32_t _start, int32_t _target, uint32_t _phase_increment) {
    target = _target;
    delta = _target - _start;
    phase_increment = _phase_increment;
  }

  inline int32_t compute_linear_slope() const { // Divide delta by duration
    int32_t s = (static_cast<int64_t>(delta) * phase_increment) >> 32;
    if (!s) s = delta >= 0 ? 1 : -1;
    return s;
  }

  static inline uint8_t max_shift(int32_t n) {
    uint32_t n_unsigned = abs(n >= 0 ? n : n + 1);
    return __builtin_clzl(n_unsigned) - 1; // 0..31
  }

  static inline int32_t compute_expo_slope(
    int32_t linear_slope, int8_t shift, uint8_t max_shift
  ) {
    return shift >= 0
      ? linear_slope << std::min(static_cast<uint8_t>(shift), max_shift)
      : linear_slope >> static_cast<uint8_t>(-shift);
  }

};

const uint8_t kLutExpoSlopeShiftSizeBits = 4;
STATIC_ASSERT(
  1 << kLutExpoSlopeShiftSizeBits == LUT_EXPO_SLOPE_SHIFT_SIZE,
  expo_slope_shift_size
);

class Envelope {
 public:
  Envelope() { }
  ~Envelope() { }

  void Init(int32_t value) {
    gate_ = false;
    value_ = value;
    Trigger(ENV_SEGMENT_DEAD);
    std::fill(
      &expo_slope_[0],
      &expo_slope_[LUT_EXPO_SLOPE_SHIFT_SIZE],
      0
    );
  }

  inline void NoteOff() {
    gate_ = false;
    switch (segment_) {
      case ENV_SEGMENT_ATTACK:
      case ENV_SEGMENT_DECAY:
        next_tick_segment_ = ENV_SEGMENT_EARLY_RELEASE; break;
      case ENV_SEGMENT_SUSTAIN:
        next_tick_segment_ = ENV_SEGMENT_RELEASE; break;
      default: break;
    }
  }

  inline void NoteOn(
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

    if (!gate_) {
      gate_ = true;
      next_tick_segment_ = ENV_SEGMENT_ATTACK;
    }
  }

  inline int16_t tremolo(uint16_t strength) const {
    int32_t relative_value = (value_ - release_.target) >> 16;
    return relative_value * -strength >> 16;
  }
  
  inline void Trigger(EnvelopeSegment segment) {
    if (gate_ && segment == ENV_SEGMENT_EARLY_RELEASE) {
      segment = ENV_SEGMENT_SUSTAIN; // Skip early-release when gate is high
    }
    if (!gate_ && segment == ENV_SEGMENT_SUSTAIN) {
      segment = ENV_SEGMENT_RELEASE; // Skip sustain when gate is low
    }
    segment_ = next_tick_segment_ = segment;
    switch (segment) {
      case ENV_SEGMENT_ATTACK : motion_ = &attack_  ; break;
      case ENV_SEGMENT_DECAY  : motion_ = &decay_   ; break;
      case ENV_SEGMENT_RELEASE: motion_ = &release_ ; break;
      case ENV_SEGMENT_EARLY_RELEASE: motion_ = &early_release_; break;
      case ENV_SEGMENT_SUSTAIN:
      case ENV_SEGMENT_DEAD:
      case ENV_NUM_SEGMENTS: // Should never happen
        motion_ = NULL;
        return;
    }

    // TODO value_ = target_;
    
    if (segment == ENV_SEGMENT_EARLY_RELEASE) {
      // If gate ends before reaching sustain, EARLY_RELEASE heads for the
      // sustain level at RELEASE's steepest slope, then RELEASE begins.
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
      early_release_.phase_increment = (steepest_expo_slope / (delta_to_sustain >> 16)) << 16;
    } else { // Build normal expo slopes

      if (segment == ENV_SEGMENT_ATTACK) {
        // In case the attack is not starting from 0 (e.g. it interrupts a
        // still-high release), adjust its timing and slope to try to match the
        // nominal sound and feel
        int32_t nominal_delta = attack_.target - release_.target;
        bool positive_segment_slope = nominal_delta >= 0;
        if (positive_segment_slope != (attack_.delta >= 0)) {
          // If deltas differ in sign, the direction is wrong -- skip segment
          next_tick_segment_ = static_cast<EnvelopeSegment>(segment + 1);
          return;
        }
        // Pick the larger delta, and thus the steeper slope that reaches the
        // target more quickly.  If actual delta is smaller than nominal (e.g.
        // from re-attacks that begin high), use nominal's steeper slope (e.g.
        // so the attack sounds like a quick catch-up vs a flat, blaring hold
        // stage). If actual is greater (rare in practice), use that
        attack_.delta = positive_segment_slope
          ? std::max(nominal_delta, attack_.delta)
          : std::min(nominal_delta, attack_.delta);
      }

      int32_t linear_slope = motion_->compute_linear_slope();
      uint8_t max_shift = Motion::max_shift(linear_slope);
      for (uint8_t i = 0; i < LUT_EXPO_SLOPE_SHIFT_SIZE; ++i) {
        int8_t shift = lut_expo_slope_shift[i];
        expo_slope_[i] = Motion::compute_expo_slope(linear_slope, shift, max_shift);
      }  
    }
  }

  inline void Tick() {
    // TODO simplify to a single render_new_segment_ dirty check?  also can NoteOn/Noteoff handle their triggers externally and just set the dirty bit here?  compute expo slopes for attack/decay on NoteOn, release/early_release on NoteOff.  when decay starts (the only auto transition that really matters), just switch to its slope table
    // ALSO: this "note arrives during render" scenario is fantasy, it's all synchronous.  The inside of the render loop doesn't need memory reads, EXCEPT when it itself completes segments
    // Can even have a "zero slope table" so value increment isn't conditional
    // Also try copying slope table into locals
    // Also: "running total" vector fn?
    while (segment_ != next_tick_segment_) { // Event loop
      Trigger(next_tick_segment_);
      phase_ = 0;
    }

    if (!motion_) return;
    phase_ += motion_->phase_increment;
    if (phase_ < motion_->phase_increment) {
      next_tick_segment_ = static_cast<EnvelopeSegment>(segment_ + 1);
      // TODO jump to target, or do an immediate Tick to trigger next?
      return Tick();
    }

    value_ += expo_slope_[phase_ >> (32 - kLutExpoSlopeShiftSizeBits)];
  }

  inline int16_t value() const { return value_ >> 16; }

 private:
  bool gate_;
  EnvelopeSegment next_tick_segment_;
  
  Motion attack_, decay_, release_, early_release_;
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
