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

#include "stmlib/utils/dsp.h"

#include "yarns/resources.h"

namespace yarns {

const uint8_t kUpdatePeriod = 24;

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
    clock_ = 0;

    target_[ENV_SEGMENT_ATTACK] = 65535;
    target_[ENV_SEGMENT_RELEASE] = 0;
    target_[ENV_SEGMENT_DEAD] = 0;

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

  // All params 7-bit
  inline void SetADSR(uint8_t a, uint8_t d, uint8_t s, uint8_t r) {
    increment_[ENV_SEGMENT_ATTACK] = lut_portamento_increments[a];
    increment_[ENV_SEGMENT_DECAY] = lut_portamento_increments[d];
    target_[ENV_SEGMENT_DECAY] = target_[ENV_SEGMENT_SUSTAIN] = s << 9;
    increment_[ENV_SEGMENT_RELEASE] = lut_portamento_increments[r];
  }
  
  inline void Trigger(EnvelopeSegment segment) {
    if (segment == ENV_SEGMENT_DEAD) {
      value_ = 0;
    }
    if (!gate_) {
      CONSTRAIN(target_[segment], 0, value_); // No rise without gate
      if (segment == ENV_SEGMENT_SUSTAIN) {
        segment = ENV_SEGMENT_RELEASE; // Skip sustain
      }
    }
    a_ = value_;
    b_ = target_[segment];
    segment_ = segment;
    phase_ = 0;
    // clock_ = 0;
  }

  inline void NeedsClock() {
    clock_dirty_ = true;
  }

  inline void Clock() { // 48kHz
    if (!clock_dirty_) return;
    clock_++;
    // clock_dirty_ = false; // Makes performance worse for some reason?
    value_fresh_ = false;
    if (clock_ < kUpdatePeriod) return;
    clock_ = 0;
    value_dirty_ = true;
    uint32_t increment = increment_[segment_];
    phase_ += increment;
    if (phase_ < increment) {
      value_ = b_;
      Trigger(static_cast<EnvelopeSegment>(segment_ + 1));
    }
  }

  inline bool Render() {
    Clock();
    if (!value_dirty_) return value_fresh_;
    value_ = Mix(a_, b_, Interpolate824(lut_env_expo, phase_));
    value_dirty_ = false;
    value_fresh_ = true;
    return value_fresh_;
  }

  inline uint16_t value() { return value_; }

 private:
  bool gate_;

  // Phase increments for each segment.
  uint32_t increment_[ENV_NUM_SEGMENTS];
  
  // Value that needs to be reached at the end of each segment.
  uint16_t target_[ENV_NUM_SEGMENTS];
  
  // Current segment.
  size_t segment_;
  
  // Start and end value of the current segment.
  uint16_t a_;
  uint16_t b_;
  uint16_t value_;
  uint32_t phase_;
  uint8_t clock_;
  bool clock_dirty_;
  bool value_fresh_; // Newly rendered
  bool value_dirty_; // Needs render

  DISALLOW_COPY_AND_ASSIGN(Envelope);
};

}  // namespace yarns

#endif  // YARNS_ENVELOPE_H_
