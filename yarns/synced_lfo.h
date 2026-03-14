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
#include "yarns/svf.h"
using namespace stmlib;

namespace yarns {

enum LFOShape {
  LFO_SHAPE_TRIANGLE,
  LFO_SHAPE_SAW_DOWN,
  LFO_SHAPE_SAW_UP,
  LFO_SHAPE_SQUARE,
  LFO_SHAPE_RANDOM_STEPPED,
  LFO_SHAPE_NOISE,

  LFO_SHAPE_LAST
};

template<uint8_t PHASE_ERR_DOWNSHIFT, uint8_t FREQ_ERR_DOWNSHIFT>
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
    svf_.Init();
    damp_ = 0;
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
  void SetDamp(int16_t damp) { damp_ = damp; }
  void Refresh() { phase_ += phase_increment_; }

  void Refresh(LFOShape shape) {
    phase_ += phase_increment_;

    // Compute raw shape value
    int16_t raw;
    if (shape == LFO_SHAPE_NOISE) {
      raw = Random::GetSample();
    } else if (shape == LFO_SHAPE_RANDOM_STEPPED) {
      if (phase_ < phase_increment_) {
        held_value_ = next_value_;
        next_value_ = Random::GetSample();
      }
      raw = held_value_;
    } else {
      raw = DeterministicShape(shape, phase_);
    }

    int16_t cutoff = CutoffFromPhaseIncrement();
    svf_.Process(raw, cutoff, damp_);
    output_ = svf_.lp;
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

  inline int16_t CutoffFromPhaseIncrement() const {
    int32_t cutoff = phase_increment_ >> 10;
    if (cutoff > 16383) cutoff = 16383;
    if (cutoff < 1) cutoff = 1;
    return static_cast<int16_t>(cutoff);
  }

  uint32_t phase_;
  uint32_t phase_increment_;
  uint32_t previous_phase_, previous_target_phase_;
  int16_t held_value_;
  int16_t next_value_;
  SVF svf_;
  int16_t damp_; // 0 = bypass, >0 = SVF damping coefficient
  int16_t output_;

  DISALLOW_COPY_AND_ASSIGN(SyncedLFO);
};

} // namespace yarns

#endif // YARNS_SYNCED_LFO_H_
