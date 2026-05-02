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
  uint16_t peak_u16, sustain_u16; // Platonic, unscaled targets
  uint32_t attack_u32, decay_u32, release_u32; // Phase increments
};

const uint8_t kLutExpoSlopeShiftSizeBits = 4;
STATIC_ASSERT(
  1 << kLutExpoSlopeShiftSizeBits == LUT_EXPO_SLOPE_SHIFT_SIZE,
  expo_slope_shift_size
);

class Envelope {
 public:
  Envelope() { }
  ~Envelope() { }

  void Init(int16_t zero_value_s16);
  void NoteOff();
  void NoteOn(
    ADSR& adsr,
    // Bounds stored as s32 but semantically s16
    int32_t min_target_s16, int32_t max_target_s16
  );
  void Trigger(EnvelopeStage stage);
  void RenderSamples(int16_t* sample_buffer, int32_t bias_target_q31);
  void RenderStageDispatch(
    int16_t* sample_buffer, size_t samples_left,
    int32_t bias_q31, int32_t bias_slope_q31
  );
  template<bool MOVING, bool POSITIVE_SLOPE>
  void RenderStage(
    int16_t* sample_buffer, size_t samples_left,
    int32_t bias_q31, int32_t bias_slope_q31
  );

  void Rescale(float scaling_factor);

  inline int16_t tremolo(uint16_t strength_u16) const {
    int32_t relative_value_q15 = (value_q30_ - stage_target_q30_[ENV_STAGE_RELEASE]) >> (30 - 15);
    return relative_value_q15 * -strength_u16 >> 16;
  }

  inline int16_t value() const { return value_q30_ >> (30 - 15); }
  inline EnvelopeStage stage() const { return stage_; }

  static inline uint8_t signed_clz(int32_t x) {
    const uint32_t x_for_clz = static_cast<uint32_t>(abs(x >= 0 ? x : x + 1));
    return __builtin_clzl(x_for_clz) - 1;
  }

 private:
  ADSR* adsr_;

  // Q30 in int32_t; the top integer bit is saturation headroom for
  // `value += slope` overshoot and for SatSub deltas (range [-2, 2)).
  int32_t stage_target_q30_[ENV_NUM_STAGES];
  int32_t target_q30_, value_q30_;
  int32_t expo_slope_lut_q30_[LUT_EXPO_SLOPE_SHIFT_SIZE];

  // Q31 (full s32; no overshoot, slope is pre-scaled by block size).
  int32_t bias_q31_;

  // Current stage.
  EnvelopeStage stage_;

  uint32_t phase_u32_, phase_increment_u32_;

  DISALLOW_COPY_AND_ASSIGN(Envelope);
};

}  // namespace yarns

#endif  // YARNS_ENVELOPE_H_
