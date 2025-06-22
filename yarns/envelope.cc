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

void Envelope::Init(int16_t raw_zero_value) {
  phase_ = phase_increment_ = 0;
  int32_t scaled_zero_value = raw_zero_value << (31 - 16);
  value_ = scaled_zero_value;
  std::fill(
    &stage_target_[0],
    &stage_target_[ENV_NUM_STAGES],
    scaled_zero_value
  );
  std::fill(
    &expo_slope_lut_[0],
    &expo_slope_lut_[LUT_EXPO_SLOPE_SHIFT_SIZE],
    0
  );
  Trigger(ENV_STAGE_DEAD);
}

void Envelope::NoteOff() {
  Trigger(ENV_STAGE_RELEASE);
}

void Envelope::NoteOn(
  ADSR& adsr,
  int32_t min_target, int32_t max_target // Actual bounds, 16-bit signed
) {
  adsr_ = &adsr;
  int16_t scale = max_target - min_target;
  min_target <<= 16;
  // NB: sustain level can be higher than peak
  stage_target_[ENV_STAGE_ATTACK] =
    (min_target + scale * adsr.peak) >> 1;
  stage_target_[ENV_STAGE_DECAY] = stage_target_[ENV_STAGE_SUSTAIN] =
    (min_target + scale * adsr.sustain) >> 1;
  stage_target_[ENV_STAGE_RELEASE] = stage_target_[ENV_STAGE_DEAD] =
    min_target >> 1;

  switch (stage_) {
    case ENV_STAGE_ATTACK:
      // Legato: ignore changes to peak target
      break;
    case ENV_STAGE_DECAY:
    case ENV_STAGE_SUSTAIN:
      // Legato: respect changes to sustain target
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
  phase_ = 0;
  target_ = stage_target_[stage]; // Cache against new NoteOn
  switch (stage) {
    case ENV_STAGE_ATTACK : phase_increment_ = adsr_->attack  ; break;
    case ENV_STAGE_DECAY  : phase_increment_ = adsr_->decay   ; break;
    case ENV_STAGE_RELEASE: phase_increment_ = adsr_->release ; break;
    default: phase_increment_ = 0; return;
  }

  int32_t actual_delta = SatSub(target_, value_, 31);
  if (!actual_delta) TRIGGER_NEXT_STAGE; // Already at target

  // Decay always treats the current value as nominal start, because in all
  // scenarios, the peak level doesn't give us useful information:
  // 1. Automatic transition from attack: we know value reached peak level
  // 2. Legato NoteOn: peak level is irrelevant, actual delta is all we have
  // 3. Skipped attack: ^
  int32_t nominal_start = stage == ENV_STAGE_DECAY
    ? value_
    : stage_target_[stmlib::modulo(
        static_cast<int8_t>(stage) - 1,
        static_cast<int8_t>(ENV_NUM_STAGES)
    )];
  int32_t nominal_delta = SatSub(target_, nominal_start, 31);

  // Skip stage if there is a direction disagreement or nowhere to go
  // Cases: NoteOn during release from above peak level
  if (
    // The stage is supposed to have a direction
    nominal_delta != 0 &&
    // It doesn't agree with the actual direction
    (nominal_delta > 0) != (actual_delta > 0)
  ) {
    TRIGGER_NEXT_STAGE;
  }

  int32_t linear_slope;
  if (abs(actual_delta) < abs(nominal_delta)) {
    // Closer to target than expected -- shorten stage duration proportionally, keeping nominal slope
    // Cases: NoteOn during release (of same polarity); NoteOff from below sustain level during attack
    linear_slope = MulS32(nominal_delta, phase_increment_);
    phase_increment_ = static_cast<uint32_t>(
      static_cast<float>(phase_increment_) * abs(
        static_cast<float>(nominal_delta) /
        static_cast<float>(actual_delta)
      )
    );
  } else {
    // Distance is GTE expected -- keep nominal stage duration, but steepen the slope
    // Cases: NoteOff during attack/decay from between sustain/peak levels; NoteOn during release of opposite polarity (hi timbre); normal well-adjusted stages
    linear_slope = MulS32(actual_delta, phase_increment_);
  }
  if (!linear_slope) TRIGGER_NEXT_STAGE; // Too close to target for useful slope

  // Populate dynamic LUT for phase-dependent slope
  const uint32_t max_expo_phase_increment = UINT32_MAX >> (kLutExpoSlopeShiftSizeBits + 1);
  if (phase_increment_ > max_expo_phase_increment) {
    // If we won't get 2+ samples per expo shift, fall back on linear slope
    std::fill(
      &expo_slope_lut_[0],
      &expo_slope_lut_[LUT_EXPO_SLOPE_SHIFT_SIZE],
      linear_slope
    );
  } else {
    const uint8_t max_shift = signed_clz(linear_slope) - 1; // Maintain 31-bit scaling
    for (uint8_t i = 0; i < LUT_EXPO_SLOPE_SHIFT_SIZE; ++i) {
      int8_t shift = lut_expo_slope_shift[i];
      expo_slope_lut_[i] = shift >= 0
        ? linear_slope << std::min(static_cast<uint8_t>(shift), max_shift)
        : linear_slope >> static_cast<uint8_t>(-shift);
      if (!expo_slope_lut_[i]) {
        expo_slope_lut_[i] = linear_slope > 0 ? 1 : -1;
      }
    }
  }
}

void Envelope::RenderSamples(int16_t* sample_buffer, int32_t bias_target) {
  // Bias is unaffected by stage change, thus has distinct lifecycle from other locals
  const int32_t bias_slope = ((bias_target >> 1) - (bias_ >> 1)) >> (kAudioBlockSizeBits - 1);
  size_t samples_left = kAudioBlockSize;
  RenderStageDispatch(sample_buffer, samples_left, bias_, bias_slope);
}

void Envelope::RenderStageDispatch(
  int16_t* sample_buffer, size_t samples_left, int32_t bias, int32_t bias_slope
) {
  if (phase_increment_ == 0) {
    RenderStage<false , false >(sample_buffer, samples_left, bias, bias_slope);
  } else if (expo_slope_lut_[0] > 0) {
    RenderStage<true  , true  >(sample_buffer, samples_left, bias, bias_slope);
  } else {
    RenderStage<true  , false >(sample_buffer, samples_left, bias, bias_slope);
  }
}

#define VALUE_PASSED(x) ( \
  ( POSITIVE_SLOPE && value >= x) || \
  (!POSITIVE_SLOPE && value <= x) \
)

#define OUTPUT \
  bias += bias_slope; \
  int32_t overflowing_u16 = (value >> (30 - 16)) + (bias >> (31 - 16)); \
  uint16_t clipped_u16 = ClipU16(overflowing_u16); \
  *sample_buffer++ = clipped_u16 >> 1; // 0..INT16_MAX

template<bool MOVING, bool POSITIVE_SLOPE>
void Envelope::RenderStage(
  int16_t* sample_buffer, size_t samples_left, int32_t bias, int32_t bias_slope
) {
  int32_t value = value_;
  int32_t target = target_;
  uint32_t phase = phase_;
  uint32_t phase_increment = phase_increment_;
  EnvelopeStage stage = stage_;
  int32_t expo_slope[LUT_EXPO_SLOPE_SHIFT_SIZE];
  std::copy(
    &expo_slope_lut_[0],
    &expo_slope_lut_[LUT_EXPO_SLOPE_SHIFT_SIZE],
    &expo_slope[0]
  );
  // int32_t nominal_start = nominal_start_;
  // bool nominal_start_reached = false;

  while (samples_left--) {
    if (!MOVING) {
      value = target; // In case we skipped a stage with a tiny but nonzero delta
      OUTPUT;
      continue;
    }

    // Stay at initial (steepest) slope until we reach the nominal start
    // nominal_start_reached = nominal_start_reached || VALUE_PASSED(nominal_start);
    // if (nominal_start_reached) {
    phase += phase_increment;
    if (phase < phase_increment) phase = UINT32_MAX;
    // }

    int32_t slope = expo_slope[phase >> (32 - kLutExpoSlopeShiftSizeBits)];
    value += slope;
    if (VALUE_PASSED(target)) {
      value = target; // Don't overshoot target
      OUTPUT;

      value_ = value; // So Trigger knows actual start value
      Trigger(static_cast<EnvelopeStage>(stage + 1));

      // Even if there are no samples left, this will save bias state for us
      return RenderStageDispatch(sample_buffer, samples_left, bias, bias_slope);
    } else {
      OUTPUT;
    }
  }

  // Render is complete, but stage is not -- save state for next render
  value_ = value;
  phase_ = phase;
  phase_increment_ = phase_increment;

  bias_ = bias;
}

void Envelope::Rescale(float factor) {
  bias_ = static_cast<int32_t>(bias_ * factor);
  value_ = static_cast<int32_t>(value_ * factor);
  target_ = static_cast<int32_t>(target_ * factor);
  for (int i = 0; i < ENV_NUM_STAGES; ++i) {
    stage_target_[i] = static_cast<int32_t>(stage_target_[i] * factor);
  }
  for (int i = 0; i < LUT_EXPO_SLOPE_SHIFT_SIZE; ++i) {
    expo_slope_lut_[i] = static_cast<int32_t>(expo_slope_lut_[i] * factor);
  }
}

}  // namespace yarns
