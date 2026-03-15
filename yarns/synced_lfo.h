// Copyright 2013 Emilie Gillet.
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
//
// -----------------------------------------------------------------------------
//
// Synced LFO.

#ifndef YARNS_SYNCED_LFO_H_
#define YARNS_SYNCED_LFO_H_

#include "stmlib/stmlib.h"
#include "stmlib/utils/random.h"
using namespace stmlib;

namespace yarns {

enum LFOShape {
  LFO_SHAPE_TRIANGLE,
  LFO_SHAPE_SAW_DOWN,
  LFO_SHAPE_SAW_UP,
  LFO_SHAPE_SQUARE,
  LFO_SHAPE_RANDOM_LINEAR,
  LFO_SHAPE_RANDOM_EXPO_SLEW,
  LFO_SHAPE_RANDOM_STEP,
  LFO_SHAPE_NOISE,

  LFO_SHAPE_LAST
};

template<
  uint8_t PHASE_ERR_DOWNSHIFT, uint8_t FREQ_ERR_DOWNSHIFT,
  uint8_t REFRESH_HZ_TO_OUTPUT_HZ_RATIO_BITS = 0
>
class SyncedLFO {
 public:

  SyncedLFO() { }
  ~SyncedLFO() { }
  void Init() {
    phase_ = 0;
    phase_increment_ = 0;
    previous_phase_ = 0;
    previous_target_phase_ = 0;
    held_value_ = 0;
    next_value_ = 0;
    lp_value_ = 0;
    stepped_random_sample_is_dirty_ = false;
  }
  void SetPhase(uint32_t phase) { phase_ = phase; }
  void RegisterPhase(uint32_t phase, bool force) {
    if (force) {
      phase_ = phase;
    } else {
      SetTargetPhase(phase);
    }
  }
  uint32_t GetPhase() const { return phase_; }
  uint32_t GetTargetPhase() const { return previous_target_phase_; }
  uint32_t GetPhaseIncrement() const { return phase_increment_; }
  void SetPhaseIncrement(uint32_t i) { phase_increment_ = i; }
  void Refresh() {
    phase_ += phase_increment_;
    if (phase_ < phase_increment_) stepped_random_sample_is_dirty_ = true;
  }

  void RefreshShape(LFOShape shape) {
    // Always consume a random sample for deterministic timing
    int16_t random_sample = Random::GetSample();

    if (shape < LFO_SHAPE_RANDOM_LINEAR) {
      output_ = DeterministicShape(shape, phase_);
      return;
    }

    if (shape == LFO_SHAPE_NOISE) {
      output_ = random_sample;
      return;
    }

    // S&H-based shapes: update on phase wrap
    if (stepped_random_sample_is_dirty_) {
      held_value_ = next_value_;
      next_value_ = random_sample;
      stepped_random_sample_is_dirty_ = false;
    }

    switch (shape) {
      case LFO_SHAPE_RANDOM_STEP:
        output_ = held_value_;
        break;
      case LFO_SHAPE_RANDOM_LINEAR: {
        int16_t interpolated = held_value_ +
          ((static_cast<int32_t>(next_value_ - held_value_) *
            static_cast<int32_t>(phase_ >> 17)) >> 15);
        output_ = interpolated;
        break;
      }
      case LFO_SHAPE_RANDOM_EXPO_SLEW:
        OnePoleFilter(held_value_);
        output_ = static_cast<int16_t>(lp_value_ >> 16);
        break;
      default:
        break;
    }
  }

  int16_t shape(LFOShape s) const { return output_; }
  int16_t shape(LFOShape s, uint32_t phase) const { return output_; }

  uint32_t ComputeTargetPhase(int32_t tick_counter, uint16_t period_ticks, uint32_t phase_offset = 0) const {
    uint16_t tick_phase = modulo(tick_counter, period_ticks);
    uint32_t target_phase = ((tick_phase << 16) / period_ticks) << 16;
    return target_phase + phase_offset;
  }

  void Tap(int32_t tick_counter, uint16_t period_ticks, uint32_t phase_offset = 0) {
    SetTargetPhase(ComputeTargetPhase(tick_counter, period_ticks, phase_offset));
  }

  void SetTargetPhase(uint32_t target_phase) {
    // TODO delta of unsigneds can overflow signed
    uint32_t target_increment = target_phase - previous_target_phase_;
    int32_t d_error = target_increment - (phase_ - previous_phase_);
    int32_t p_error = target_phase - phase_;
    int32_t error = (p_error >> PHASE_ERR_DOWNSHIFT) + (d_error >> FREQ_ERR_DOWNSHIFT);

    phase_increment_ = SaturatingIncrement(phase_increment_, error);

    previous_phase_ = phase_;
    previous_target_phase_ = target_phase;
  }

 private:
  static int16_t DeterministicShape(LFOShape s, uint32_t phase) {
    switch (s) {
      case LFO_SHAPE_TRIANGLE:
        return phase < 1UL << 31
          ? INT16_MIN + (phase >> 15)
          : 0x17fff - (phase >> 15);
      case LFO_SHAPE_SAW_DOWN:
        return INT16_MAX - (phase >> 16);
      case LFO_SHAPE_SAW_UP:
        return INT16_MIN + (phase >> 16);
      case LFO_SHAPE_SQUARE:
        return phase < 1UL << 31 ? INT16_MAX : INT16_MIN;
      default:
        return 0;
    }
  }

  // 1-pole LPF with coefficient derived from phase_increment_.
  // Settlings per LFO cycle = 2^kOnePoleSettlingsPerCycleBits.
  // 0 = just barely settles once per cycle. 2 = four times per cycle.
  static const uint8_t kOnePoleSettlingsPerCycleBits = 2;

  // error (16b) * alpha must fit int32_t, so alpha < 2^15.
  // alpha = phase_increment >> kAlphaShift, clamped to INT16_MAX.
  // At low LFO rates, alpha is small and the clamp doesn't engage.
  // At high LFO rates, the clamp limits settling speed — but the LFO
  // cycle is so short that even clamped alpha settles fast enough.
  static const uint8_t kAlphaShift =
    32 - 15 - kOnePoleSettlingsPerCycleBits - REFRESH_HZ_TO_OUTPUT_HZ_RATIO_BITS;

  inline void OnePoleFilter(int16_t input) {
    int32_t alpha = phase_increment_ >> kAlphaShift;
    if (alpha > INT16_MAX) alpha = INT16_MAX;
    // Error in 16-bit signal space avoids Q15.16 overflow.
    // Product: error (16b) * alpha (15b) = 31b, fits int32_t.
    int32_t error = input - static_cast<int16_t>(lp_value_ >> 16);
    lp_value_ += error * alpha;
  }

  uint32_t phase_;
  uint32_t phase_increment_;
  uint32_t previous_phase_, previous_target_phase_;
  int16_t held_value_;
  int16_t next_value_;
  int32_t lp_value_; // Q15.16
  bool stepped_random_sample_is_dirty_;
  int16_t output_;

  DISALLOW_COPY_AND_ASSIGN(SyncedLFO);
};

} // namespace yarns

#endif // YARNS_SYNCED_LFO_H_
