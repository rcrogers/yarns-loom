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
    int32_t min_target, int32_t max_target, // Actual bounds
    uint8_t attack_time, uint8_t decay_time, uint8_t release_time // 7-bit
  ) {
    int32_t scale = max_target - min_target;
    segment_target_[ENV_SEGMENT_ATTACK] = min_target + (scale >> 16) * peak_level;
    segment_target_[ENV_SEGMENT_DECAY] = segment_target_[ENV_SEGMENT_SUSTAIN] = min_target + (scale >> 16) * sustain_level;
    segment_target_[ENV_SEGMENT_RELEASE] = segment_target_[ENV_SEGMENT_DEAD] =
    min_target;
    // TODO could interpolate these from 16-bit parameters
    increment_[ENV_SEGMENT_ATTACK] = lut_envelope_phase_increments[attack_time];
    increment_[ENV_SEGMENT_DECAY] = lut_envelope_phase_increments[decay_time];
    increment_[ENV_SEGMENT_RELEASE] = lut_envelope_phase_increments[release_time];
  }

  inline int32_t tremolo(uint16_t t) const {
    int32_t relative_value = value_ - segment_target_[ENV_SEGMENT_DEAD];
    return relative_value * -t >> 16;
  }
  
  inline void Trigger(EnvelopeSegment segment) {
    if (segment == ENV_SEGMENT_DEAD) {
      value_ = segment_target_[ENV_SEGMENT_DEAD];
    }
    if (!gate_ && segment == ENV_SEGMENT_SUSTAIN) {
      segment = ENV_SEGMENT_RELEASE; // Skip sustain
    }
    target_ = segment_target_[segment];
    if (!gate_) { // Moving away from 0 ("rising") requires a gate
      if (target_ - value_ >= 0) {
        CONSTRAIN(target_, segment_target_[ENV_SEGMENT_RELEASE], value_);
      } else {
        CONSTRAIN(target_, value_, segment_target_[ENV_SEGMENT_RELEASE]);
      }
    }
    phase_increment_ = increment_[segment];
    linear_slope_ = static_cast<int64_t>(target_ - value_) * phase_increment_ >> 32;
    segment_ = segment;
    phase_ = 0;
  }

  inline void Tick() {
    phase_ += phase_increment_;
    int32_t slope = stmlib::exponentialize(linear_slope_, lut_expo_slope_shift[phase_ >> 24]);
    if (
      (slope > 0 && value_ >= target_ - slope) ||
      (slope < 0 && value_ <= target_ - slope) ||
      phase_ < phase_increment_
    ) {
      value_ = target_;
      Trigger(static_cast<EnvelopeSegment>(segment_ + 1));
    } else {
      value_ += slope;
    }
  }

  inline int32_t value() const { return value_; }

 private:
  bool gate_;

  // Phase increments for each segment.
  uint32_t increment_[ENV_NUM_SEGMENTS];
  
  // Value that needs to be reached at the end of each segment.
  int32_t segment_target_[ENV_NUM_SEGMENTS];
  
  // Current segment.
  size_t segment_;
  
  // Target and current value of the current segment.
  int32_t target_;
  int32_t value_;

  // The naive value increment per tick, before exponential conversion
  int32_t linear_slope_;

  uint32_t phase_;
  uint32_t phase_increment_;

  DISALLOW_COPY_AND_ASSIGN(Envelope);
};

}  // namespace yarns

#endif  // YARNS_ENVELOPE_H_
