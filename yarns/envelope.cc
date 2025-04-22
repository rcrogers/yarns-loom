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
#include "yarns/multi.h"

namespace yarns {

using namespace stmlib;

void Envelope::Init(int16_t zero_value) {
  gate_ = false;
  value_ = stage_target_[ENV_STAGE_RELEASE] = zero_value << 16;
  Trigger(ENV_STAGE_DEAD);
  std::fill(
    &expo_slope_[0],
    &expo_slope_[LUT_EXPO_SLOPE_SHIFT_SIZE],
    0
  );
}

void Envelope::NoteOff() {
  gate_ = false;
  switch (stage_) {
    case ENV_STAGE_ATTACK:
    case ENV_STAGE_DECAY:
      Trigger(ENV_STAGE_EARLY_RELEASE); break;
    case ENV_STAGE_SUSTAIN:
      Trigger(ENV_STAGE_EARLY_RELEASE); break;
    default: break;
  }
}

void Envelope::NoteOn(
  ADSR& adsr,
  int32_t min_target, int32_t max_target // Actual bounds, 16-bit signed
) {
  adsr_ = &adsr;
  int16_t scale = max_target - min_target;
  positive_scale_ = scale >= 0;
  min_target <<= 16;
  // TODO if attack and decay are going same direction because sustain is higher than peak, merge them?
  stage_target_[ENV_STAGE_ATTACK] = min_target + scale * adsr.peak;
  stage_target_[ENV_STAGE_DECAY] =
    stage_target_[ENV_STAGE_EARLY_RELEASE] =
    stage_target_[ENV_STAGE_SUSTAIN] =
    min_target + scale * adsr.sustain;
  stage_target_[ENV_STAGE_RELEASE] = min_target;

  if (!gate_) {
    gate_ = true;
    Trigger(ENV_STAGE_ATTACK);
  }
}

void Envelope::Trigger(EnvelopeStage stage) {
  if (gate_ && stage == ENV_STAGE_EARLY_RELEASE) {
    stage = ENV_STAGE_SUSTAIN; // Skip early-release when gate is high
  }
  if (!gate_ && stage == ENV_STAGE_SUSTAIN) {
    stage = ENV_STAGE_RELEASE; // Skip sustain when gate is low
  }
  stage_ = stage;
  switch (stage) {
    case ENV_STAGE_ATTACK : phase_increment_ = adsr_->attack  ; break;
    case ENV_STAGE_DECAY  : phase_increment_ = adsr_->decay   ; break;
    case ENV_STAGE_EARLY_RELEASE : phase_increment_ = adsr_->release ; break;
    case ENV_STAGE_RELEASE: phase_increment_ = adsr_->release ; break;
    default: phase_increment_ = 0; return;
  }
  target_ = stage_target_[stage];

  // In case the stage is not starting from its nominal value (e.g. an
  // attack that interrupts a still-high release), adjust its timing and slope
  // to try to match the nominal sound and feel
  int32_t actual_delta = SatSub(target_, value_);
  int32_t nominal_start_value = stage_target_[
    stmlib::modulo(static_cast<int8_t>(stage) - 1, static_cast<int8_t>(ENV_STAGE_DEAD))
  ];
  int32_t nominal_delta = SatSub(target_, nominal_start_value);
  const bool movement_expected = nominal_delta != 0;
  // Skip stage if there is a direction disagreement or nowhere to go
  if (
    // Already at target (important to skip because 0 disrupts direction checks)
    !actual_delta || (
      // The stage is supposed to have a direction
      movement_expected && (
        // It doesn't agree with the actual direction
        (nominal_delta > 0) != (actual_delta > 0)
      )
      // Cases: NoteOn during release from above peak level
      // TODO are direction skips good in case of polarity reversals? only if there are non-attack cases
    )
  ) {
    return Trigger(static_cast<EnvelopeStage>(stage_ + 1));
  }
  // Pick the larger delta, and thus the steeper slope that reaches the target
  // more quickly.  If actual delta is smaller than nominal (e.g. from
  // re-attacks that begin high), use nominal's steeper slope (e.g. so the
  // attack sounds like a quick catch-up vs a flat, blaring hold stage). If
  // actual is greater (rare in practice), use that
  positive_stage_slope_ = actual_delta >= 0;
  int32_t delta = positive_stage_slope_
    ? std::max(nominal_delta, actual_delta)
    : std::min(nominal_delta, actual_delta);

  int32_t linear_slope = (static_cast<int64_t>(delta) * phase_increment_) >> 32;
  if (!linear_slope) {
    return Trigger(static_cast<EnvelopeStage>(stage_ + 1));
  }

  // Must get at least two samples per tick for expo slope to be accurate
  const uint32_t max_expo_phase_increment = UINT32_MAX >> (kLutExpoSlopeShiftSizeBits + 1);
  if (phase_increment_ > max_expo_phase_increment) {
    // Fall back on linear slope
    std::fill(
      &expo_slope_[0],
      &expo_slope_[LUT_EXPO_SLOPE_SHIFT_SIZE],
      linear_slope
    );
  } else {
    const uint32_t slope_for_clz = static_cast<uint32_t>(abs(linear_slope >= 0 ? linear_slope : linear_slope + 1));
    const uint8_t max_shift = __builtin_clzl(slope_for_clz) - 1;
    for (uint8_t i = 0; i < LUT_EXPO_SLOPE_SHIFT_SIZE; ++i) {
      int8_t shift = lut_expo_slope_shift[i];
      expo_slope_[i] = shift >= 0
        ? linear_slope << std::min(static_cast<uint8_t>(shift), max_shift)
        : linear_slope >> static_cast<uint8_t>(-shift);
    }
  }

  target_overshoot_threshold_ = target_ - expo_slope_[LUT_EXPO_SLOPE_SHIFT_SIZE - 1];
  phase_ = 0;
}

#define OUTPUT \
  bias += bias_slope; \
  int32_t overflowing_16 = (value >> 15) + (bias >> 15); \
  uint16_t clipped_16 = ClipU16(overflowing_16); \
  *samples++ = clipped_16 >> 1; // 0..INT16_MAX

#define FETCH_LOCALS \
  value = value_; \
  target_overshoot_threshold = target_overshoot_threshold_; \
  positive_stage_slope = positive_stage_slope_; \
  phase = phase_; \
  phase_increment = phase_increment_; \
  stage = stage_; \
  std::copy(&expo_slope_[0], &expo_slope_[LUT_EXPO_SLOPE_SHIFT_SIZE], &expo_slope[0]); \

void Envelope::RenderSamples(int16_t* samples, int32_t new_bias) {
  // This is unaffected by stage change, thus has distinct lifecycle from other locals
  int32_t bias = bias_;
  const int32_t bias_slope = ((new_bias >> 1) - (bias >> 1)) >> (kAudioBlockSizeBits - 1);

  int32_t value;
  int32_t target_overshoot_threshold;
  bool positive_stage_slope;
  uint32_t phase;
  uint32_t phase_increment;
  EnvelopeStage stage;
  int32_t expo_slope[LUT_EXPO_SLOPE_SHIFT_SIZE];

  FETCH_LOCALS;
  for (size_t size = kAudioBlockSize; size--; ) {
    // Early-release stage stays at max slope until it reaches target
    if (stage != ENV_STAGE_EARLY_RELEASE) {
      if (!phase_increment) {
        OUTPUT;
        continue;
      }
      phase += phase_increment;
      if (phase < phase_increment) phase = UINT32_MAX;
    }

    if (positive_stage_slope // The slope is about to overshoot the target
      ? value > target_overshoot_threshold
      : value < target_overshoot_threshold
    ) {
      // Because the target is closer than the expo slope would have taken us,
      // this tick, which we spend on jumping to the target, is flatter than
      // nominal.  The alternative would be to, instead of jumping, immediately
      // Tick() again
      value_ = value = target_;
      OUTPUT;

      Trigger(static_cast<EnvelopeStage>(stage + 1));
      FETCH_LOCALS;
      continue;
    }

    value += expo_slope_[phase >> (32 - kLutExpoSlopeShiftSizeBits)];
    OUTPUT;
  }

  value_ = value;
  phase_ = phase;
  phase_increment_ = phase_increment;

  bias_ = bias;
}

}  // namespace yarns
