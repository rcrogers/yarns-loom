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
    segment_ = next_tick_segment_ = ENV_SEGMENT_DEAD;
  }

  inline void NoteOff() {
    gate_ = false;
    switch (segment_) {
      case ENV_SEGMENT_ATTACK:
        next_tick_segment_ = ENV_SEGMENT_DECAY;
        break;
      case ENV_SEGMENT_SUSTAIN:
        next_tick_segment_ = ENV_SEGMENT_RELEASE;
        break;
      default:
        break;
    }
  }

  inline EnvelopeSegment segment() const {
    return static_cast<EnvelopeSegment>(segment_);
  }

  inline void NoteOn(
    ADSR& adsr,
    int32_t min_target, int32_t max_target // Actual bounds, 16-bit signed
  ) {
    int16_t scale = max_target - min_target;
    positive_scale_ = scale >= 0;
    min_target <<= 16;
    segment_target_[ENV_SEGMENT_ATTACK] = min_target + scale * adsr.peak;
    segment_target_[ENV_SEGMENT_DECAY] = segment_target_[ENV_SEGMENT_SUSTAIN] = min_target + scale * adsr.sustain;
    segment_target_[ENV_SEGMENT_RELEASE] = min_target;
    adsr_ = &adsr;

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
    segment_ = next_tick_segment_ = segment;
    if (segment == ENV_SEGMENT_DEAD) {
      value_ = segment_target_[ENV_SEGMENT_RELEASE];
    }
    if (!gate_ && segment == ENV_SEGMENT_SUSTAIN) {
      segment = ENV_SEGMENT_RELEASE; // Skip sustain
    }
    switch (segment) {
      case ENV_SEGMENT_ATTACK: phase_increment_ = adsr_->attack; break;
      case ENV_SEGMENT_DECAY: phase_increment_ = adsr_->decay; break;
      case ENV_SEGMENT_RELEASE: phase_increment_ = adsr_->release; break;
      default: phase_increment_ = 0; return;
    }
    target_ = segment_target_[segment];
    if (!gate_ && positive_scale_ == (target_ >= value_)) {
      // Moving away from minimum requires a gate -- to prevent e.g. an aborted attack from decaying upward
      target_ = value_;
    }
    int32_t delta = target_ - value_;
    positive_segment_slope_ = delta >= 0;
    linear_slope_ = (static_cast<int64_t>(delta) * phase_increment_) >> 32;
    max_shift_ = __builtin_clzl(abs(linear_slope_));
    expo_dirty_ = true;
    phase_ = 0;
  }

  inline void Tick() {
    if (segment_ != next_tick_segment_) {
      Trigger(next_tick_segment_);
    }
    if (!phase_increment_) return;
    phase_ += phase_increment_;
    if (phase_ < phase_increment_) {
      Trigger(static_cast<EnvelopeSegment>(segment_ + 1));
      return;
    }
    int8_t shift = lut_expo_slope_shift[phase_ >> 24];
    if (shift != expo_slope_shift_) expo_dirty_ = true;
    if (expo_dirty_) {
      expo_dirty_ = false;
      expo_slope_shift_ = shift;
      expo_slope_ = 0;
      if (linear_slope_ != 0) expo_slope_ = shift >= 0
        ? linear_slope_ << std::min(static_cast<uint8_t>(shift), max_shift_)
        : linear_slope_ >> static_cast<uint8_t>(-shift);
      target_overshoot_threshold_ = target_ - expo_slope_;
    }
    if (positive_segment_slope_ // The slope is about to overshoot the target
      ? value_ > target_overshoot_threshold_
      : value_ < target_overshoot_threshold_
    ) {
      // value_ = target_;
      Trigger(static_cast<EnvelopeSegment>(segment_ + 1));
      // Tick();
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
