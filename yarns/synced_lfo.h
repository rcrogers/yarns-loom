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

namespace yarns {

enum LFOShape {
  LFO_SHAPE_TRIANGLE,
  LFO_SHAPE_SAW_DOWN,
  LFO_SHAPE_SAW_UP,
  LFO_SHAPE_SQUARE,

  LFO_SHAPE_LAST
};

template<uint8_t PHASE_ERR_SHIFT, uint8_t FREQ_ERR_SHIFT>
class SyncedLFO {
 public:

  uint8_t clock_division_;

  SyncedLFO() { }
  ~SyncedLFO() { }
  void Init(uint32_t phase = 0) {
    phase_ = phase;
  }

  uint32_t GetPhase() const { return phase_; }
  uint32_t GetPhaseIncrement() const { return phase_increment_; }
  void SetPhaseIncrement(uint32_t i) { phase_increment_ = i; }
  void Refresh() { phase_ += increment; }

  int16_t shape(LFOShape s) const { return shape(s, phase_); }
  int16_t shape(LFOShape shape, uint32_t phase) const {
    switch (shape) {
      case LFO_SHAPE_TRIANGLE:
        return phase < 1UL << 31      // x < 0.5
          ? INT16_MIN + (phase >> 15) // y = -0.5 + 2x = 2(x - 1/4)
          : 0x17fff - (phase >> 15);  // y =  1.5 - 2x = 2(3/4 - x)
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

  void Tap(uint32_t tick_counter, uint16_t period_ticks, uint32_t phase_offset = 0) {
    uint16_t tick_phase = tick_counter % period_ticks;
    uint32_t target_phase = ((tick_phase << 16) / period_ticks) << 16;
    SetTargetPhase(target_phase + phase_offset);
  }

  void SetTargetPhase(uint32_t target_phase) {
    // TODO delta of unsigneds can overflow signed
    uint32_t target_increment = target_phase - previous_target_phase_;
    int32_t d_error = target_increment - (phase_ - previous_phase_);
    int32_t p_error = target_phase - phase_;
    int32_t error = (p_error >> PHASE_ERR_SHIFT) + (d_error >> FREQ_ERR_SHIFT);

    if (error < 0 && abs(error) > phase_increment_) {
      // underflow
      phase_increment_ = 0;
    } else if (error > 0 && (UINT32_MAX - error) < phase_increment_) {
      // overflow
      phase_increment_ = UINT32_MAX;
    } else {
      phase_increment_ += error;
    }

    previous_phase_ = phase_;
    previous_target_phase_ = target_phase;
  }

 private:

  uint32_t phase_;
  uint32_t phase_increment_;
  uint32_t previous_phase_, previous_target_phase_;

  DISALLOW_COPY_AND_ASSIGN(SyncedLFO);
};

} // namespace yarns

#endif // YARNS_SYNCED_LFO_H_
