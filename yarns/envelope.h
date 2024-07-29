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

const size_t kEnvelopeBlockSize = 32;

enum EnvelopeSegment {
  ENV_SEGMENT_ATTACK,
  ENV_SEGMENT_DECAY,
  ENV_SEGMENT_EARLY_RELEASE,
  ENV_SEGMENT_SUSTAIN,
  ENV_SEGMENT_RELEASE,
  ENV_SEGMENT_DEAD,
  ENV_NUM_SEGMENTS,
};

struct ADSR {
  uint16_t peak, sustain; // Platonic, unscaled targets
  uint32_t attack, decay, release; // Timing
};

template<uint8_t NUM_SCALINGS>
class Envelope {
 public:
  Envelope() { }
  ~Envelope() { }

  void Init() {
    gate_ = false;
    for (uint8_t scaling = 0; scaling < NUM_SCALINGS; ++scaling) {
      scaling_active_[scaling] = false;
      value_[scaling] = 0;
      segment_target_[scaling][ENV_SEGMENT_RELEASE] = 0;
    }
    Trigger(ENV_SEGMENT_DEAD);
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

  inline void NoteOn(ADSR& adsr) {
    adsr_ = &adsr;
    for (uint8_t scaling = 0; scaling < NUM_SCALINGS; ++scaling) {
      scaling_active_[scaling] = false;
    }

    if (!gate_) {
      gate_ = true;
      next_tick_segment_ = ENV_SEGMENT_ATTACK;
    }
  }

  inline void SetScaling(
    uint8_t scaling,
    int32_t min_target, int32_t max_target // Actual bounds, 16-bit signed
  ) {
    scaling_active_[scaling] = true;
    int16_t scale = max_target - min_target;
    positive_scale_[scaling] = scale >= 0;
    min_target <<= 16;
    segment_target_[scaling][ENV_SEGMENT_ATTACK] = min_target + scale * adsr_->peak;
    segment_target_[scaling][ENV_SEGMENT_DECAY] =
      segment_target_[scaling][ENV_SEGMENT_EARLY_RELEASE] =
      segment_target_[scaling][ENV_SEGMENT_SUSTAIN] =
      min_target + scale * adsr_->sustain;
    segment_target_[scaling][ENV_SEGMENT_RELEASE] = min_target;
  }

  inline int16_t tremolo(uint8_t scaling, uint16_t strength) const {
    int32_t relative_value = (value_[scaling] - segment_target_[scaling][ENV_SEGMENT_RELEASE]) >> 16;
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
      case ENV_SEGMENT_ATTACK : phase_increment_ = adsr_->attack  ; break;
      case ENV_SEGMENT_DECAY  : phase_increment_ = adsr_->decay   ; break;
      case ENV_SEGMENT_EARLY_RELEASE : phase_increment_ = adsr_->release ; break;
      case ENV_SEGMENT_RELEASE: phase_increment_ = adsr_->release ; break;
      default: phase_increment_ = 0; return;
    }

    int8_t prev_segment = stmlib::modulo(static_cast<int8_t>(segment) - 1, ENV_SEGMENT_DEAD);
    for (uint8_t scaling = 0; scaling < NUM_SCALINGS; ++scaling) {
      if (!scaling_active_[scaling]) continue;
      
      int32_t target = segment_target_[scaling][segment];

      // In case the segment is not starting from its nominal value (e.g. an
      // attack that interrupts a still-high release), adjust its timing and slope
      // to try to match the nominal sound and feel
      int32_t actual_delta = target - value_[scaling];
      int32_t nominal_delta = target - segment_target_[scaling][prev_segment];
      bool positive_segment_slope = nominal_delta >= 0;
      if (positive_segment_slope != (actual_delta >= 0)) {
        // If deltas differ in sign, the direction is wrong -- skip segment
        next_tick_segment_ = static_cast<EnvelopeSegment>(segment_ + 1);
        return; // Will be handled by next Tick()
      }
      // Pick the larger delta, and thus the steeper slope that reaches the target
      // more quickly.  If actual delta is smaller than nominal (e.g. from
      // re-attacks that begin high), use nominal's steeper slope (e.g. so the
      // attack sounds like a quick catch-up vs a flat, blaring hold stage). If
      // actual is greater (rare in practice), use that
      int32_t delta = positive_segment_slope
        ? std::max(nominal_delta, actual_delta)
        : std::min(nominal_delta, actual_delta);

      // Prepare inputs for Tick to convert slope
      int32_t linear_slope = (static_cast<int64_t>(delta) * phase_increment_) >> 32;
      if (!linear_slope) linear_slope = positive_segment_slope ? 1 : -1;

      positive_segment_slope_[scaling] = positive_segment_slope;
      linear_slope_[scaling] = linear_slope; 
      max_shift_[scaling] = __builtin_clzl(abs(linear_slope));
    }

    expo_slope_shift_ = 0x7f; // Force recalculation
    phase_ = 0;
  }

  inline void RenderSamples() {
    for (uint8_t scaling = 0; scaling < NUM_SCALINGS; ++scaling) {
      if (scaling_active_[scaling] && output_samples_[scaling].writable() < kEnvelopeBlockSize) {
        return;
      }
    }

    size_t size = kEnvelopeBlockSize;
    while(size--) {
      while (segment_ != next_tick_segment_) { // Event loop
        Trigger(next_tick_segment_);
      }

      // Early-release segment stays at max slope until it reaches target
      if (segment_ != ENV_SEGMENT_EARLY_RELEASE) {
        if (!phase_increment_) return;
        phase_ += phase_increment_;
        if (phase_ < phase_increment_) phase_ = UINT32_MAX;
      }
      
      int8_t shift = lut_expo_slope_shift[phase_ >> 24];
      bool recalculate_expo = shift != expo_slope_shift_;
      expo_slope_shift_ = shift;

      for (uint8_t scaling = 0; scaling < NUM_SCALINGS; ++scaling) {
        if (!scaling_active_[scaling]) continue;

        int32_t target = segment_target_[scaling][segment_];
        
        if (recalculate_expo) { // Calculate a fresh expo slope
          int32_t linear_slope = linear_slope_[scaling];
          int32_t expo_slope = shift >= 0
            ? linear_slope << std::min(static_cast<uint8_t>(shift), max_shift_)
            : linear_slope >> static_cast<uint8_t>(-shift);
          if (!expo_slope) expo_slope = linear_slope;
          target_overshoot_threshold_[scaling] = target - expo_slope;
          expo_slope_[scaling] = expo_slope;
        }

        if (positive_segment_slope_ // The slope is about to overshoot the target
          ? value_ > target_overshoot_threshold_[scaling]
          : value_ < target_overshoot_threshold_[scaling]
        ) {
          // Because the target is closer than the expo slope would have taken us,
          // this tick, which we spend on jumping to the target, is flatter than
          // nominal.  The alternative would be to, instead of jumping, immediately
          // Tick() again
          value_[scaling] = target;
          next_tick_segment_ = static_cast<EnvelopeSegment>(segment_ + 1);
          continue;
        }

        value_[scaling] += expo_slope_;
        output_samples_[scaling].Overwrite(value_[scaling] >> 16);
      }
    }
  }

  inline int16_t value(uint8_t scaling) const { return value_[scaling] >> 16; }
  inline int16_t ReadSample(uint8_t scaling) { return output_samples_[scaling].ImmediateRead(); }
  inline int16_t PeekSample(uint8_t scaling) { return output_samples_[scaling].ImmediatePeek(); }

 private:
  bool scaling_active_[NUM_SCALINGS];

  bool gate_;
  ADSR* adsr_;
  EnvelopeSegment next_tick_segment_;
  
  // Value that needs to be reached at the end of each segment.
  int32_t segment_target_[NUM_SCALINGS][ENV_SEGMENT_DEAD];
  bool positive_scale_[NUM_SCALINGS], positive_segment_slope_[NUM_SCALINGS];
  
  // Current segment.
  EnvelopeSegment segment_;
  
  // Current value of the current segment.
  int32_t value_[NUM_SCALINGS];

  stmlib::RingBuffer<int16_t, kEnvelopeBlockSize * 2> output_samples_[NUM_SCALINGS];

  // Cache
  int8_t expo_slope_shift_;
  int32_t expo_slope_[NUM_SCALINGS];
  int32_t target_overshoot_threshold_[NUM_SCALINGS];
  uint8_t max_shift_[NUM_SCALINGS];

  // The naive value increment per tick, before exponential conversion
  int32_t linear_slope_[NUM_SCALINGS];

  uint32_t phase_, phase_increment_;

  DISALLOW_COPY_AND_ASSIGN(Envelope);
};

enum EnvelopeRole {
  ENV_ROLE_OSCILLATOR_GAIN,
  ENV_ROLE_OSCILLATOR_TIMBRE,
  ENV_ROLE_DC_AUX_1,
  ENV_ROLE_DC_AUX_2,
  ENV_ROLE_LAST
};

typedef Envelope<ENV_ROLE_LAST> CombinedEnvelope;

}  // namespace yarns

#endif  // YARNS_ENVELOPE_H_
