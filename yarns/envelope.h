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

const size_t kEnvBlockSize = 2;

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

    target_[ENV_SEGMENT_RELEASE] = 0;
    target_[ENV_SEGMENT_DEAD] = 0;

    increment_[ENV_SEGMENT_SUSTAIN] = 0;
    increment_[ENV_SEGMENT_DEAD] = 0;
  }

  inline void GateOn() {
    if (!gate_) {
      gate_ = true;
      Trigger(ENV_SEGMENT_ATTACK);
      samples_.Flush();
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
        samples_.Flush();
        break;
      default:
        break;
    }
  }

  inline EnvelopeSegment segment() const {
    return static_cast<EnvelopeSegment>(segment_);
  }

  // All params 7-bit
  inline void SetADSR(uint16_t peak, uint8_t a, uint8_t d, uint8_t s, uint8_t r) {
    target_[ENV_SEGMENT_ATTACK] = peak;
    // TODO could interpolate these from 16-bit parameters
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
    phase_increment_ = increment_[segment];
    segment_ = segment;
    phase_ = 0;
  }

  inline void RenderSamples(size_t size = kEnvBlockSize) {
    if (samples_.writable() < size) return;

    while (size--) {
      phase_ += phase_increment_;
      if (phase_ < phase_increment_) {
        value_ = b_;
        Trigger(static_cast<EnvelopeSegment>(segment_ + 1));
      }
      if (phase_increment_) {
        value_ = Mix(a_, b_, Interpolate824(lut_env_expo, phase_));
      }
      samples_.Overwrite(value_);
    }
  }

  inline void ReadSample() {
    value_read_ = samples_.ImmediateRead();
  }

  inline uint16_t value() const { return value_read_; }

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
  uint16_t value_read_;
  uint32_t phase_;
  uint32_t phase_increment_;

  stmlib::RingBuffer<uint16_t, kEnvBlockSize * 2> samples_;

  DISALLOW_COPY_AND_ASSIGN(Envelope);
};

}  // namespace yarns

#endif  // YARNS_ENVELOPE_H_
