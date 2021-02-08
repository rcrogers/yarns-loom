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

class SyncedLFO {
 public:

  SyncedLFO() { }
  ~SyncedLFO() { }
  inline void Init() {
    counter_ = 0;
    ticks_per_cycle_ = 0;
    phase_ = 0;
  }

  inline uint32_t GetPhase() const {
    return phase_;
  }

  inline uint32_t GetPhaseIncrement() const {
    return phase_increment_;
  }

  inline uint32_t Increment(uint32_t increment) {
    phase_ += increment;
    return GetPhase();
  }

  inline uint32_t Refresh() {
    return Increment(phase_increment_);
  }

  inline int16_t Triangle(uint32_t phase) {
    return phase < 1UL << 31       // x < 0.5
      ?  INT16_MIN + (phase >> 15) // y = -0.5 + 2x = 2(x - 1/4)
      : 0x17fff - (phase >> 15);   // y =  1.5 - 2x = 2(3/4 - x)
  }

  inline void SetPeriod(uint16_t n) {
    if (ticks_per_cycle_) {
      counter_ *= 1 + ((n - 1) / ticks_per_cycle_);
    }
    ticks_per_cycle_ = n;
    counter_ %= ticks_per_cycle_;
  }

  inline void Tap() {
    uint32_t target_phase = ((counter_ % ticks_per_cycle_) * 65536 / ticks_per_cycle_) << 16;
    uint32_t target_increment = target_phase - previous_target_phase_;

    int32_t d_error = target_increment - (phase_ - previous_phase_);
    int32_t p_error = target_phase - phase_;
    int32_t error = (d_error + (p_error >> 1)) >> 13;

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
    counter_ = (counter_ + 1) % ticks_per_cycle_;
  }

 private:

  uint16_t ticks_per_cycle_;
  uint16_t counter_;

  uint32_t phase_;
  uint32_t phase_increment_;
  uint32_t previous_target_phase_;
  uint32_t previous_phase_;

  DISALLOW_COPY_AND_ASSIGN(SyncedLFO);
};

} // namespace yarns

#endif // YARNS_SYNCED_LFO_H_
