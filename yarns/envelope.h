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

#include "yarns/resources.h"

namespace yarns {

using namespace stmlib;

enum EnvelopeStage {
  ENV_STAGE_ATTACK,
  ENV_STAGE_DECAY,
  ENV_STAGE_SUSTAIN,
  ENV_STAGE_RELEASE,
  ENV_STAGE_DEAD,
  ENV_NUM_STAGES,
};

struct ADSR {
  uint16_t peak, sustain; // Platonic, unscaled targets
  uint32_t attack, decay, release; // Timing
};

const uint8_t kLutExpoSlopeShiftSizeBits = 4;
STATIC_ASSERT(
  1 << kLutExpoSlopeShiftSizeBits == LUT_EXPO_SLOPE_SHIFT_SIZE,
  expo_slope_shift_size
);

extern uint32_t envelope_render_count;

class Envelope {
 public:
  Envelope() { }
  ~Envelope() { }

  void Init(int16_t zero_value);
  void NoteOff();
  void NoteOn(
    ADSR& adsr,
    int32_t min_target, int32_t max_target // Actual bounds, 16-bit signed
  );
  void Trigger(EnvelopeStage stage);
  void RenderSamples(int16_t* sample_buffer, int32_t bias_target);
  void RenderStageDispatch(
    int16_t* sample_buffer, size_t samples_left, int32_t bias, int32_t bias_slope
  );
  template<bool MOVING, bool POSITIVE_SLOPE>
  void RenderStage(
    int16_t* sample_buffer, size_t samples_left, int32_t bias, int32_t bias_slope
  );

  inline int16_t tremolo(uint16_t strength) const {
    int32_t relative_value = (value_ - stage_target_[ENV_STAGE_RELEASE]) >> (31 - 16);
    return relative_value * -strength >> 16;
  }

  inline int16_t value() const { return value_ >> (31 - 16); }

  static inline uint8_t signed_clz(int32_t x) {
    const uint32_t x_for_clz = static_cast<uint32_t>(abs(x >= 0 ? x : x + 1));
    return __builtin_clzl(x_for_clz) - 1;
  }

 private:
  ADSR* adsr_;

  // 31-bit, so slope increment can skip overflow checks
  int32_t stage_target_[ENV_NUM_STAGES];
  int32_t target_, value_, nominal_start_;
  int32_t expo_slope_lut_[LUT_EXPO_SLOPE_SHIFT_SIZE];

  // 32-bit
  int32_t bias_;

  // Current stage.
  EnvelopeStage stage_;

  uint32_t phase_, phase_increment_;

  DISALLOW_COPY_AND_ASSIGN(Envelope);
};

}  // namespace yarns

#endif  // YARNS_ENVELOPE_H_
