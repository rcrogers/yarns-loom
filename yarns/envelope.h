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

#include "stmlib/utils/ring_buffer.h"

#include "yarns/resources.h"

namespace yarns {

using namespace stmlib;

const uint8_t kAudioBlockSizeBits = 6;
const size_t kAudioBlockSize = 1 << kAudioBlockSizeBits;

const uint8_t kLutExpoSlopeShiftSizeBits = 32 - __builtin_clz(LUT_EXPO_SLOPE_SHIFT_SIZE - 1);
STATIC_ASSERT(
  1 << kLutExpoSlopeShiftSizeBits == LUT_EXPO_SLOPE_SHIFT_SIZE,
  expo_slope_shift_size
);

enum EnvelopeSegment {
  ENV_SEGMENT_ATTACK,           // manual start, auto/manual end
  ENV_SEGMENT_DECAY,            // auto start, auto/manual end
  ENV_SEGMENT_SUSTAIN,          // no motion
  ENV_SEGMENT_RELEASE_PRELUDE,  // manual start, auto end
  ENV_SEGMENT_RELEASE,          // manual start, auto end
  ENV_SEGMENT_DEAD,             // no motion
  ENV_NUM_SEGMENTS,
};

struct ADSR {
  uint16_t peak, sustain; // Platonic, unscaled targets
  uint32_t attack, decay, release; // Timing
};

struct Motion {
  int32_t target, delta;
  uint32_t phase_increment;

  void Set(int32_t _start, int32_t _target, uint32_t _phase_increment);
  int32_t compute_linear_slope() const; // Divide delta by duration
  static uint8_t max_shift(int32_t n);
  static int32_t compute_expo_slope(
    int32_t linear_slope, int8_t shift, uint8_t max_shift
  );
};

class Envelope {
 public:
  Envelope() { }
  ~Envelope() { }

  void Init(int32_t value);
  // Compute the max damp-ability of the envelope for a given tremolo strength
  int16_t tremolo(uint16_t strength) const;
  void NoteOff();
  void NoteOn(
    ADSR& adsr,
    int32_t min_target, int32_t max_target // Actual bounds, 16-bit signed
  );
  void Trigger(EnvelopeSegment segment); // Populates expo slope table for the new segment
  template<size_t BUFFER_SIZE>
  // void RenderSamples(stmlib::RingBuffer<int16_t, BUFFER_SIZE>* buffer, int32_t value_bias, int32_t slope_bias);
  void RenderSamples(stmlib::RingBuffer<int16_t, BUFFER_SIZE>* buffer, int32_t value_bias, int32_t slope_bias, size_t samples_to_render = kAudioBlockSize);

 private:
  int32_t value_;
  uint32_t phase_;
  Motion attack_, decay_, release_, release_prelude_;

  // Current segment.
  EnvelopeSegment segment_;
  Motion* motion_;

  // Maps slices of the phase to slopes, approximating an exponential curve
  int32_t expo_slope_[LUT_EXPO_SLOPE_SHIFT_SIZE];

  DISALLOW_COPY_AND_ASSIGN(Envelope);
};

}  // namespace yarns

#endif  // YARNS_ENVELOPE_H_
