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
  value_ = target_ = stage_target_[ENV_STAGE_RELEASE] = zero_value << (31 - 16);
  Trigger(ENV_STAGE_DEAD);
  std::fill(
    &expo_slope_[0],
    &expo_slope_[LUT_EXPO_SLOPE_SHIFT_SIZE],
    0
  );
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
  // TODO if attack and decay are going same direction because sustain is higher than peak, merge them?
  stage_target_[ENV_STAGE_ATTACK] =
    (min_target + scale * adsr.peak) >> 1;
  stage_target_[ENV_STAGE_DECAY] = stage_target_[ENV_STAGE_SUSTAIN] =
    (min_target + scale * adsr.sustain) >> 1;
  stage_target_[ENV_STAGE_RELEASE] = stage_target_[ENV_STAGE_DEAD] =
    min_target >> 1;

  switch (stage_) {
    case ENV_STAGE_ATTACK:
      // Legato: ignore the updated peak target
      break;
    case ENV_STAGE_DECAY:
    case ENV_STAGE_SUSTAIN:
      // Legato: respect the updated sustain target
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

// Update state of current stage
void Envelope::Trigger(EnvelopeStage stage) {
  multi.PrintDebugByte(0xA0 + stage);
  stage_ = stage;
  phase_ = 0;
  target_ = stage_target_[stage];
  switch (stage) {
    case ENV_STAGE_ATTACK : phase_increment_ = adsr_->attack  ; break;
    case ENV_STAGE_DECAY  : phase_increment_ = adsr_->decay   ; break;
    case ENV_STAGE_RELEASE: phase_increment_ = adsr_->release ; break;
    default: phase_increment_ = 0; return;
  }

  // In case the stage is not starting from its nominal value (e.g. an
  // attack that interrupts a still-high release), adjust its timing and slope
  // to try to match the nominal sound and feel
  int32_t actual_delta = SatSub(target_, value_, 31);
  if (!actual_delta) TRIGGER_NEXT_STAGE; // Already at target

  // Decay always treats the current value as nominal start, because in all
  // scenarios, the peak level doesn't give us useful information:
  // 1. Automatic transition from attack: we know value reached peak level
  // 2. Legato NoteOn: peak level is meaningless, actual delta is all we have
  // 3. Skipped attack: ^
  nominal_start_ = stage == ENV_STAGE_DECAY
    ? value_
    : stage_target_[stmlib::modulo(
        static_cast<int8_t>(stage) - 1,
        static_cast<int8_t>(ENV_NUM_STAGES)
    )];
  int32_t nominal_delta = SatSub(target_, nominal_start_, 31);

  // Skip attack if there is a direction disagreement or nowhere to go
  // Cases: NoteOn during release from above peak level
  // We don't want to skip decay even if it looks like wrong direction, because it's the only way to transition to the (possibly updated) sustain level.  OTOH, this transition may be indefinitely far in the wrong direction
  if (
    stage == ENV_STAGE_ATTACK &&
    // The stage is supposed to have a direction
    nominal_delta != 0 &&
    // It doesn't agree with the actual direction
    (nominal_delta > 0) != (actual_delta > 0)
    // TODO are direction skips good in case of polarity reversals? only if there are non-attack cases
  ) {
    TRIGGER_NEXT_STAGE;
  }

  // If closer to target than expected:
  // Cases: NoteOn during release (of same polarity); NoteOff from below sustain level during attack
  int32_t final_delta;
  if (abs(actual_delta) < abs(nominal_delta)) {
    // Shorten the stage duration
    phase_increment_ = static_cast<uint32_t>(
      static_cast<float>(phase_increment_) * abs(
        static_cast<float>(nominal_delta) /
        static_cast<float>(actual_delta)
      )
    );
    final_delta = actual_delta;
  } else {
    // We're at least as far as expected (possibly farther).
    // Cases: NoteOff during attack/decay from between sustain/peak levels; NoteOn during release of opposite polarity (hi timbre); normal well-adjusted stages
    // NB: we don't lengthen stage time.  See nominal_start_reached
    final_delta = nominal_delta;
  }

  int32_t linear_slope = MulS32(final_delta, phase_increment_);
  if (!linear_slope) TRIGGER_NEXT_STAGE; // Too close to target for useful slope

  // If we won't get 2+ samples per expo shift, fall back on linear slope
  const uint32_t max_expo_phase_increment = UINT32_MAX >> (kLutExpoSlopeShiftSizeBits + 1);
  if (phase_increment_ > max_expo_phase_increment) {
    std::fill(
      &expo_slope_[0],
      &expo_slope_[LUT_EXPO_SLOPE_SHIFT_SIZE],
      linear_slope
    );
  } else {
    const uint8_t max_shift = signed_clz(linear_slope) - 1; // Maintain 31-bit scaling
    for (uint8_t i = 0; i < LUT_EXPO_SLOPE_SHIFT_SIZE; ++i) {
      int8_t shift = lut_expo_slope_shift[i];
      expo_slope_[i] = shift >= 0
        ? linear_slope << std::min(static_cast<uint8_t>(shift), max_shift)
        : linear_slope >> static_cast<uint8_t>(-shift);
      if (!expo_slope_[i]) {
        expo_slope_[i] = linear_slope > 0 ? 1 : -1;
      }
    }
  }
}

#define OUTPUT \
  bias += bias_slope; \
  int32_t overflowing_u16 = (value >> (30 - 16)) + (bias >> (31 - 16)); \
  uint16_t clipped_u16 = ClipU16(overflowing_u16); \
  *samples++ = clipped_u16 >> 1; // 0..INT16_MAX

#define CACHE_STAGE_DATA \
  stage = stage_; \
  value = value_; \
  target = target_; \
  nominal_start = nominal_start_; \
  nominal_start_reached = false; \
  phase = phase_; \
  phase_increment = phase_increment_; \
  std::copy(&expo_slope_[0], &expo_slope_[LUT_EXPO_SLOPE_SHIFT_SIZE], &expo_slope[0]); \
  positive_slope = expo_slope[0] > 0; \

#define VALUE_PASSED(x) ( \
  ( positive_slope && value >= x) || \
  (!positive_slope && value <= x) \
)

void Envelope::RenderSamples(int16_t* samples, int32_t new_bias) {
  // This is unaffected by stage change, thus has distinct lifecycle from other locals
  int32_t bias = bias_;
  const int32_t bias_slope = ((new_bias >> 1) - (bias >> 1)) >> (kAudioBlockSizeBits - 1);

  int32_t value;
  int32_t target;
  uint32_t phase;
  uint32_t phase_increment;
  EnvelopeStage stage;
  int32_t expo_slope[LUT_EXPO_SLOPE_SHIFT_SIZE];
  int32_t nominal_start;
  bool nominal_start_reached;
  bool positive_slope;

  CACHE_STAGE_DATA;
  for (size_t size = kAudioBlockSize; size--; ) {
    if (!phase_increment) { // No motion
      OUTPUT;
      continue;
    }

    // Stay at initial (steepest) slope until we reach the nominal start
    nominal_start_reached = nominal_start_reached || VALUE_PASSED(nominal_start);
    if (nominal_start_reached) {
      phase += phase_increment;
      if (phase < phase_increment) phase = UINT32_MAX;
    }

    int32_t slope = expo_slope[phase >> (32 - kLutExpoSlopeShiftSizeBits)];
    value += slope;
    if (VALUE_PASSED(target)) {
      value = target; // Don't overshoot target
      OUTPUT;

      value_ = value; // So Trigger knows actual start value
      Trigger(static_cast<EnvelopeStage>(stage + 1));
      CACHE_STAGE_DATA;
    } else {
      OUTPUT;
    }
  }

  value_ = value;
  phase_ = phase;
  phase_increment_ = phase_increment;

  bias_ = bias;
}

}  // namespace yarns
