// Copyright 2012 Emilie Gillet.
// Copyright 2020 Chris Rogers.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

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

class Envelope {
 public:
  Envelope() { }
  ~Envelope() { }

  void Init() {
    gate_ = false;

    increment_[ENV_SEGMENT_SUSTAIN] = 0;
    increment_[ENV_SEGMENT_DEAD] = 0;
  }

  inline void GateOn() {
    if (!gate_) {
      gate_ = true;
      Trigger(ENV_SEGMENT_ATTACK);
    }
  }

  inline void GateOff() {
    gate_ = false;
    switch (segment_) {
      case ENV_SEGMENT_ATTACK:
        Trigger(ENV_SEGMENT_DECAY);
        break;
      case ENV_SEGMENT_SUSTAIN:
        Trigger(ENV_SEGMENT_RELEASE);
        break;
      default:
        break;
    }
  }

  inline EnvelopeSegment segment() const {
    return static_cast<EnvelopeSegment>(segment_);
  }

  inline void Set(
    uint16_t peak_level, uint16_t sustain_level, // Platonic, unscaled targets
    int32_t min_target, int32_t max_target, // Actual bounds, 16-bit signed
    uint8_t attack_time, uint8_t decay_time, uint8_t release_time // 7-bit
  ) {
    scale_ = max_target - min_target;
    min_target <<= 16;
    segment_target_[ENV_SEGMENT_ATTACK] = min_target + scale_ * peak_level;
    segment_target_[ENV_SEGMENT_DECAY] = segment_target_[ENV_SEGMENT_SUSTAIN] = min_target + scale_ * sustain_level;
    segment_target_[ENV_SEGMENT_RELEASE] = segment_target_[ENV_SEGMENT_DEAD] =
    min_target;
    // TODO could interpolate these from 16-bit parameters
    increment_[ENV_SEGMENT_ATTACK] = lut_envelope_phase_increments[attack_time];
    increment_[ENV_SEGMENT_DECAY] = lut_envelope_phase_increments[decay_time];
    increment_[ENV_SEGMENT_RELEASE] = lut_envelope_phase_increments[release_time];
  }

  inline int16_t tremolo(uint16_t strength) const {
    int32_t relative_value = (value_ - segment_target_[ENV_SEGMENT_DEAD]) >> 16;
    return relative_value * -strength >> 16;
  }
  
  __attribute__ ((__always_inline__))
  inline void Trigger(EnvelopeSegment segment) {
    if (segment == ENV_SEGMENT_DEAD) {
      value_ = segment_target_[ENV_SEGMENT_DEAD];
    }
    if (!gate_ && segment == ENV_SEGMENT_SUSTAIN) {
      segment = ENV_SEGMENT_RELEASE; // Skip sustain
    }
    target_ = segment_target_[segment];
    if (!gate_ && (scale_ >= 0) == (target_ >= value_)) {
      // Moving away from minimum requires a gate -- to prevent e.g. an aborted attack from decaying upward
      target_ = value_;
    }
    phase_increment_ = increment_[segment];
    int8_t delta = (target_ - value_) >> 24; // Take the brunt of the 32-bit shift here to minimize error
    linear_slope_ = (phase_increment_ >> 8) * delta;
    expo_dirty_ = true;
    segment_ = segment;
    phase_ = 0;
    tick_counter_ = -1;
  }

  __attribute__ ((__always_inline__))
  inline void Tick() {
    if (!phase_increment_) return;
    phase_ += phase_increment_;
    tick_counter_ = (tick_counter_ + 1) % 10;
    if (tick_counter_ == 0) {
      int8_t shift = lut_expo_slope_shift[phase_ >> 24];
      if (shift != expo_slope_shift_) expo_dirty_ = true;
      if (expo_dirty_) {
        expo_dirty_ = false;
        expo_slope_shift_ = shift;
        expo_slope_ = 0;
        if (linear_slope_ != 0) expo_slope_ = shift >= 0
          ? linear_slope_ << std::min(static_cast<int>(shift), __builtin_clz(abs(linear_slope_)))
          : linear_slope_ >> static_cast<uint8_t>(-shift);
        target_overshoot_threshold_ = target_ - expo_slope_;
      }
    }
    if (
      phase_ < phase_increment_ || (
        linear_slope_ >= 0 // The slope is about to overshoot the target
          ? value_ > target_overshoot_threshold_
          : value_ < target_overshoot_threshold_
      )
    ) {
      // value_ = target_;
      Trigger(static_cast<EnvelopeSegment>(segment_ + 1));
      // Tick();
    } else {
      value_ += expo_slope_;
    }
  }

  inline int16_t value() const { return value_ >> 16; }

 private:
  bool gate_;

  // Phase increments for each segment.
  uint32_t increment_[ENV_NUM_SEGMENTS];
  
  // Value that needs to be reached at the end of each segment.
  int32_t segment_target_[ENV_NUM_SEGMENTS];
  int16_t scale_;
  
  // Current segment.
  size_t segment_;
  
  // Target and current value of the current segment.
  int32_t target_;
  int32_t value_;

  int8_t tick_counter_;
  int8_t expo_slope_shift_;
  int32_t expo_slope_;
  bool expo_dirty_;
  int32_t target_overshoot_threshold_;
  // The naive value increment per tick, before exponential conversion
  int32_t linear_slope_;

  uint32_t phase_;
  uint32_t phase_increment_;

  // DISALLOW_COPY_AND_ASSIGN(Envelope);
};

}  // namespace yarns

#endif  // YARNS_ENVELOPE_H_
