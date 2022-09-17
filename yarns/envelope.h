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
  ENV_SEGMENT_SUSTAIN,
  ENV_SEGMENT_RELEASE,
  ENV_SEGMENT_DEAD,
  ENV_NUM_SEGMENTS,
};

struct ADSR {
  uint16_t peak, sustain; // Platonic, unscaled targets
  uint32_t attack, decay, release; // Timing
};

class Envelope {
 public:
  Envelope() { }
  ~Envelope() { }

  void Init() {
    gate_ = false;
    value_ = segment_target_[ENV_SEGMENT_RELEASE] = 0;
    Trigger(ENV_SEGMENT_DEAD);
  }

  inline void NoteOff() {
    gate_ = false;
    switch (segment_) {
      case ENV_SEGMENT_ATTACK : next_tick_segment_ = ENV_SEGMENT_DECAY  ; break;
      case ENV_SEGMENT_SUSTAIN: next_tick_segment_ = ENV_SEGMENT_RELEASE; break;
      default: break;
    }
  }

  inline void NoteOn(
    ADSR& adsr,
    int32_t min_target, int32_t max_target // Actual bounds, 16-bit signed
  ) {
    adsr_ = &adsr;
    int16_t scale = max_target - min_target;
    positive_scale_ = scale >= 0;
    min_target <<= 16;
    // TODO if attack and decay are going same direction because sustain is higher than peak, merge them?
    segment_target_[ENV_SEGMENT_ATTACK] = min_target + scale * adsr.peak;
    segment_target_[ENV_SEGMENT_DECAY] = segment_target_[ENV_SEGMENT_SUSTAIN] = min_target + scale * adsr.sustain;
    segment_target_[ENV_SEGMENT_RELEASE] = min_target;

    if (!gate_) {
      gate_ = true;
      next_tick_segment_ = ENV_SEGMENT_ATTACK;
    }
  }

  inline int16_t tremolo(uint16_t strength) const {
    int32_t relative_value = (value_ - segment_target_[ENV_SEGMENT_RELEASE]) >> 16;
    return relative_value * -strength >> 16;
  }
  
  inline void Trigger(EnvelopeSegment segment) {
    if (!gate_ && segment == ENV_SEGMENT_SUSTAIN) {
      segment = ENV_SEGMENT_RELEASE; // Skip sustain
    }
    segment_ = next_tick_segment_ = segment;
    switch (segment) {
      case ENV_SEGMENT_ATTACK : phase_increment_ = adsr_->attack  ; break;
      case ENV_SEGMENT_DECAY  : phase_increment_ = adsr_->decay   ; break;
      case ENV_SEGMENT_RELEASE: phase_increment_ = adsr_->release ; break;
      default: phase_increment_ = 0; return;
    }
    target_ = segment_target_[segment];

    // In case the segment is not starting from its nominal value (e.g. an
    // attack that interrupts a still-high release), adjust its timing and slope
    // to try to match the nominal sound and feel
    int32_t actual_delta = target_ - value_;
    int32_t nominal_delta = target_ - segment_target_[
      stmlib::modulo(static_cast<int8_t>(segment) - 1, ENV_SEGMENT_DEAD)
    ];
    positive_segment_slope_ = nominal_delta >= 0;
    if (positive_segment_slope_ != (actual_delta >= 0)) {
      // If deltas differ in sign, the direction is wrong -- skip segment
      next_tick_segment_ = static_cast<EnvelopeSegment>(segment_ + 1);
      return;
    }
    // Pick the larger delta, and thus the steeper slope that reaches the target
    // more quickly.  If actual delta is smaller than nominal (e.g. from
    // re-attacks that begin high), use nominal's steeper slope (e.g. so the
    // attack sounds like a quick catch-up vs a flat, blaring hold stage). If
    // actual is greater (rare in practice), use that
    int32_t delta = positive_segment_slope_
      ? std::max(nominal_delta, actual_delta)
      : std::min(nominal_delta, actual_delta);

    // Prepare inputs for Tick to convert slope
    linear_slope_ = (static_cast<int64_t>(delta) * phase_increment_) >> 32;
    if (!linear_slope_) linear_slope_ = positive_segment_slope_ ? 1 : -1;
    max_shift_ = __builtin_clzl(abs(linear_slope_));
    expo_dirty_ = true;
    phase_ = 0;
  }

  inline void Tick() {
    while (segment_ != next_tick_segment_) { // Event loop
      Trigger(next_tick_segment_);
    }
    if (!phase_increment_) return;

    phase_ += phase_increment_;
    if (phase_ < phase_increment_) phase_ = UINT32_MAX;
    int8_t shift = lut_expo_slope_shift[phase_ >> 24];
    if (shift != expo_slope_shift_) expo_dirty_ = true;

    if (expo_dirty_) { // Calculate a fresh expo slope
      expo_dirty_ = false;
      expo_slope_shift_ = shift;
      expo_slope_ = 0;
      if (linear_slope_ != 0) expo_slope_ = shift >= 0
        ? linear_slope_ << std::min(static_cast<uint8_t>(shift), max_shift_)
        : linear_slope_ >> static_cast<uint8_t>(-shift);
      if (!expo_slope_) expo_slope_ = linear_slope_;
      target_overshoot_threshold_ = target_ - expo_slope_;
    }

    if (positive_segment_slope_ // The slope is about to overshoot the target
      ? value_ > target_overshoot_threshold_
      : value_ < target_overshoot_threshold_
    ) {
      value_ = target_; // TODO can cause jumps?
      next_tick_segment_ = static_cast<EnvelopeSegment>(segment_ + 1);
      return;
    }

    value_ += expo_slope_;
  }

  inline int16_t value() const { return value_ >> 16; }

 private:
  bool gate_;
  ADSR* adsr_;
  EnvelopeSegment next_tick_segment_;
  
  // Value that needs to be reached at the end of each segment.
  int32_t segment_target_[ENV_SEGMENT_DEAD];
  bool positive_scale_, positive_segment_slope_;
  
  // Current segment.
  EnvelopeSegment segment_;
  
  // Target and current value of the current segment.
  int32_t target_;
  int32_t value_;

  // Cache
  int8_t expo_slope_shift_;
  int32_t expo_slope_;
  bool expo_dirty_;
  int32_t target_overshoot_threshold_;
  uint8_t max_shift_;

  // The naive value increment per tick, before exponential conversion
  int32_t linear_slope_;

  uint32_t phase_, phase_increment_;

  DISALLOW_COPY_AND_ASSIGN(Envelope);
};

}  // namespace yarns

#endif  // YARNS_ENVELOPE_H_
