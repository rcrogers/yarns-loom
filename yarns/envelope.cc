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

#include "yarns/envelope.h"

#include "stmlib/stmlib.h"
#include "stmlib/utils/dsp.h"
#include "stmlib/dsp/dsp.h"

#include "yarns/drivers/dac.h"

namespace yarns {

using namespace stmlib;

void Envelope::Init(int16_t zero_value_s16) {
  phase_u32_ = phase_increment_u32_ = 0;
  int32_t zero_value_q30 = zero_value_s16 << (31 - 16);
  value_q30_ = zero_value_q30;
  std::fill(
    &stage_target_q30_[0],
    &stage_target_q30_[ENV_NUM_STAGES],
    zero_value_q30
  );
  std::fill(
    &expo_slope_lut_q30_[0],
    &expo_slope_lut_q30_[LUT_EXPO_SLOPE_SHIFT_SIZE],
    0
  );
  Trigger(ENV_STAGE_DEAD);
}

void Envelope::NoteOff() {
  Trigger(ENV_STAGE_RELEASE);
}

void Envelope::NoteOn(
  ADSR& adsr,
  // Bounds stored as s32 but semantically s16
  int32_t min_target_s16, int32_t max_target_s16
) {
  adsr_ = &adsr;
  int16_t scale_s16 = max_target_s16 - min_target_s16;
  int32_t min_target_q31 = min_target_s16 << 16;
  // NB: sustain level can be higher than peak
  stage_target_q30_[ENV_STAGE_ATTACK] =
    (min_target_q31 + scale_s16 * adsr.peak_u16) >> 1;
  stage_target_q30_[ENV_STAGE_DECAY] = stage_target_q30_[ENV_STAGE_SUSTAIN] =
    (min_target_q31 + scale_s16 * adsr.sustain_u16) >> 1;
  stage_target_q30_[ENV_STAGE_RELEASE] = stage_target_q30_[ENV_STAGE_DEAD] =
    min_target_q31 >> 1;

  switch (stage_) {
    case ENV_STAGE_ATTACK:
      // Legato: ignore changes to peak target
      break;
    case ENV_STAGE_DECAY:
    case ENV_STAGE_SUSTAIN:
      // Legato: respect changes to sustain target, using decay to transition
      Trigger(ENV_STAGE_DECAY);
      break;
    case ENV_STAGE_RELEASE:
    case ENV_STAGE_DEAD:
    case ENV_NUM_STAGES:
      // Start new attack
      Trigger(ENV_STAGE_ATTACK);
      break;
  }
}

#define TRIGGER_NEXT_STAGE \
  return Trigger(static_cast<EnvelopeStage>(stage + 1));

// Update current stage and its state
void Envelope::Trigger(EnvelopeStage stage) {
  stage_ = stage;
  phase_u32_ = 0;
  target_q30_ = stage_target_q30_[stage]; // Cache against new NoteOn
  switch (stage) {
    case ENV_STAGE_ATTACK : phase_increment_u32_ = adsr_->attack_u32  ; break;
    case ENV_STAGE_DECAY  : phase_increment_u32_ = adsr_->decay_u32   ; break;
    case ENV_STAGE_RELEASE: phase_increment_u32_ = adsr_->release_u32 ; break;
    default: phase_increment_u32_ = 0; return;
  }

  int32_t actual_delta_q30 = SatSub(target_q30_, value_q30_, 31);
  if (!actual_delta_q30) TRIGGER_NEXT_STAGE; // Already at target

  // Decay always treats the current value as nominal start, because in all
  // scenarios, the peak level doesn't give us useful information:
  // 1. Automatic transition from attack: we know value reached peak level
  // 2. Legato NoteOn: peak level is irrelevant, actual delta is all we have
  // 3. Skipped attack: ^
  int32_t nominal_start_q30 = stage == ENV_STAGE_DECAY
    ? value_q30_
    : stage_target_q30_[stmlib::modulo(
        static_cast<int8_t>(stage) - 1,
        static_cast<int8_t>(ENV_NUM_STAGES)
    )];
  int32_t nominal_delta_q30 = SatSub(target_q30_, nominal_start_q30, 31);

  // Skip stage if there is a direction disagreement or nowhere to go
  // Cases: NoteOn during release from above peak level
  if (
    // The stage is supposed to have a direction
    nominal_delta_q30 != 0 &&
    // It doesn't agree with the actual direction
    (nominal_delta_q30 > 0) != (actual_delta_q30 > 0)
  ) {
    TRIGGER_NEXT_STAGE;
  }

  int32_t linear_slope_q30;
  if (abs(actual_delta_q30) < abs(nominal_delta_q30)) {
    // Closer to target than expected -- shorten stage duration proportionally, keeping nominal slope
    // Cases: NoteOn during release (of same polarity); NoteOff from below sustain level during attack
    linear_slope_q30 = MulS32(nominal_delta_q30, phase_increment_u32_);
    phase_increment_u32_ = static_cast<uint32_t>(
      static_cast<float>(phase_increment_u32_) * abs(
        static_cast<float>(nominal_delta_q30) /
        static_cast<float>(actual_delta_q30)
      )
    );
  } else {
    // Distance is GTE expected -- keep nominal stage duration, but steepen the slope
    // Cases: NoteOff during attack/decay from between sustain/peak levels; NoteOn during release of opposite polarity (hi timbre); normal well-adjusted stages
    linear_slope_q30 = MulS32(actual_delta_q30, phase_increment_u32_);
  }
  if (!linear_slope_q30) TRIGGER_NEXT_STAGE; // Too close to target for useful slope

  // Populate dynamic LUT for phase-dependent slope
  const uint32_t max_expo_phase_increment_u32 = UINT32_MAX >> (kLutExpoSlopeShiftSizeBits + 1);
  if (phase_increment_u32_ > max_expo_phase_increment_u32) {
    // If we won't get 2+ samples per expo shift, fall back on linear slope
    std::fill(
      &expo_slope_lut_q30_[0],
      &expo_slope_lut_q30_[LUT_EXPO_SLOPE_SHIFT_SIZE],
      linear_slope_q30
    );
  } else {
    const uint8_t max_shift = signed_clz(linear_slope_q30) - 1; // Maintain 31-bit scaling
    for (uint8_t i = 0; i < LUT_EXPO_SLOPE_SHIFT_SIZE; ++i) {
      int8_t shift = lut_expo_slope_shift[i];
      expo_slope_lut_q30_[i] = shift >= 0
        ? linear_slope_q30 << std::min(static_cast<uint8_t>(shift), max_shift)
        : linear_slope_q30 >> static_cast<uint8_t>(-shift);
      if (!expo_slope_lut_q30_[i]) {
        expo_slope_lut_q30_[i] = linear_slope_q30 > 0 ? 1 : -1;
      }
    }
  }
}

void Envelope::RenderSamples(int16_t* sample_buffer, int32_t bias_target_q31) {
  // Bias is unaffected by stage change, thus has distinct lifecycle from other locals
  const int32_t bias_slope_q31 = ((bias_target_q31 >> 1) - (bias_q31_ >> 1)) >> (kAudioBlockSizeBits - 1);
  size_t samples_left = kAudioBlockSize;
  RenderStageDispatch(sample_buffer, samples_left, bias_q31_, bias_slope_q31);
}

void Envelope::RenderStageDispatch(
  int16_t* sample_buffer, size_t samples_left,
  int32_t bias_q31, int32_t bias_slope_q31
) {
  if (phase_increment_u32_ == 0) {
    RenderStage<false , false >(sample_buffer, samples_left, bias_q31, bias_slope_q31);
  } else if (expo_slope_lut_q30_[0] > 0) {
    RenderStage<true  , true  >(sample_buffer, samples_left, bias_q31, bias_slope_q31);
  } else {
    RenderStage<true  , false >(sample_buffer, samples_left, bias_q31, bias_slope_q31);
  }
}

#define VALUE_PASSED(x) ( \
  ( POSITIVE_SLOPE && value_q30 >= x) || \
  (!POSITIVE_SLOPE && value_q30 <= x) \
)

#define OUTPUT \
  bias_q31 += bias_slope_q31; \
  int32_t overflowing_u16 = (value_q30 >> (30 - 16)) + (bias_q31 >> (31 - 16)); \
  uint16_t clipped_u16 = ClipU16(overflowing_u16); \
  *sample_buffer++ = clipped_u16 >> 1; // 0..INT16_MAX

template<bool MOVING, bool POSITIVE_SLOPE>
void Envelope::RenderStage(
  int16_t* sample_buffer, size_t samples_left,
  int32_t bias_q31, int32_t bias_slope_q31
) {
  int32_t value_q30 = value_q30_;
  int32_t target_q30 = target_q30_;
  uint32_t phase_u32 = phase_u32_;
  uint32_t phase_increment_u32 = phase_increment_u32_;
  EnvelopeStage stage = stage_;
  int32_t expo_slope_q30[LUT_EXPO_SLOPE_SHIFT_SIZE];
  std::copy(
    &expo_slope_lut_q30_[0],
    &expo_slope_lut_q30_[LUT_EXPO_SLOPE_SHIFT_SIZE],
    &expo_slope_q30[0]
  );
  // int32_t nominal_start = nominal_start_;
  // bool nominal_start_reached = false;

  while (samples_left--) {
    if (!MOVING) {
      value_q30 = target_q30; // In case we skipped a stage with delta that was 1) nonzero and 2) too small to produce a nonzero slope
      OUTPUT;
      continue;
    }

    // Stay at initial (steepest) slope until we reach the nominal start
    // nominal_start_reached = nominal_start_reached || VALUE_PASSED(nominal_start);
    // if (nominal_start_reached) {
    phase_u32 += phase_increment_u32;
    if (phase_u32 < phase_increment_u32) phase_u32 = UINT32_MAX;
    // }

    int32_t slope_q30 = expo_slope_q30[phase_u32 >> (32 - kLutExpoSlopeShiftSizeBits)];
    value_q30 += slope_q30;
    if (VALUE_PASSED(target_q30)) {
      value_q30 = target_q30; // Don't overshoot target
      OUTPUT;

      value_q30_ = value_q30; // So Trigger knows actual start value
      Trigger(static_cast<EnvelopeStage>(stage + 1));

      // Even if there are no samples left, this will save bias state for us
      return RenderStageDispatch(sample_buffer, samples_left, bias_q31, bias_slope_q31);
    } else {
      OUTPUT;
    }
  }

  // Render is complete, but stage is not -- save state for next render
  value_q30_ = value_q30;
  phase_u32_ = phase_u32;
  phase_increment_u32_ = phase_increment_u32;

  bias_q31_ = bias_q31;
}

void Envelope::Rescale(float factor) {
  bias_q31_ = static_cast<int32_t>(bias_q31_ * factor);
  value_q30_ = static_cast<int32_t>(value_q30_ * factor);
  target_q30_ = static_cast<int32_t>(target_q30_ * factor);
  for (int i = 0; i < ENV_NUM_STAGES; ++i) {
    stage_target_q30_[i] = static_cast<int32_t>(stage_target_q30_[i] * factor);
  }
  for (int i = 0; i < LUT_EXPO_SLOPE_SHIFT_SIZE; ++i) {
    expo_slope_lut_q30_[i] = static_cast<int32_t>(expo_slope_lut_q30_[i] * factor);
  }
}

}  // namespace yarns
