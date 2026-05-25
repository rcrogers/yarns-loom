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

// System-wide PRNG buffer shared by all envelopes' chiff post-passes.
// Filled once per audio block by FillSharedPrngBuffer().
//
// NB: the perf benefit of sharing this buffer (vs each envelope running
// inline xorshift32) is marginal once the per-envelope decorrelation EOR
// is factored in — roughly 1 cycle/sample/envelope saved, offset by the
// ~448-cycle one-time fill. Breaks even around N=4-5 envelopes per block;
// at the worst-case 8 (unison-paraphonic gain+timbre × 4 voices) saves
// only ~70 cycles per block. The design is kept because shared state
// makes the decorrelation mask + chiff timing reasoning local to one
// place and avoids per-envelope PRNG state in RAM.
namespace {
  uint32_t shared_prng_buffer[kAudioBlockSize];
  uint32_t shared_prng_state = 0xCAFEBABE;
}  // namespace

void Envelope::FillSharedPrngBuffer() {
  uint32_t state = shared_prng_state;
  for (size_t i = 0; i < kAudioBlockSize; ++i) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    shared_prng_buffer[i] = state;
  }
  shared_prng_state = state;
}

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
  chiff_probability_u31_ = 0;
  chiff_prob_decrement_u32_ = 0;
  chiff_offset_q30_ = 0;
  // Address-derived mask: every Envelope instance lives at a distinct
  // address, so each gets a unique XOR mask. Low bits of the address
  // differ across instances within the same parent struct, which is all
  // that's needed to decorrelate sample positions.
  chiff_prng_xor_u32_ = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this));
  Trigger(ENV_STAGE_DEAD);
}

void Envelope::NoteOff() {
  Trigger(ENV_STAGE_RELEASE);
}

void Envelope::NoteOn(
  ADSR& adsr,
  // Bounds stored as s32 but semantically s16
  int32_t min_target_s16, int32_t max_target_s16,
  uint8_t chiff_amount
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
      // Legato: ignore changes to peak target; chiff continues its ramp.
      break;
    case ENV_STAGE_DECAY:
    case ENV_STAGE_SUSTAIN:
      // Legato: respect changes to sustain target, using decay to transition
      Trigger(ENV_STAGE_DECAY);
      break;
    case ENV_STAGE_RELEASE:
    case ENV_STAGE_DEAD:
    case ENV_NUM_STAGES:
      // Fresh attack: arm chiff fire-probability ramp and reset the
      // chiff offset random walk. chiff_amount in [0, 127], so <<24
      // caps prob at 0x7F000000 (fits top-31-bit unsigned space).
      chiff_probability_u31_ = static_cast<uint32_t>(chiff_amount) << 24;
      chiff_offset_q30_ = 0;
      chiff_prob_decrement_u32_ = static_cast<uint32_t>(
        (static_cast<uint64_t>(chiff_probability_u31_) * adsr.attack_u32) >> 32);
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
  // Chiff state persists across stage transitions. Spike amplitude is
  // delta * alpha (delta = target − value), so chiff fades naturally as
  // the envelope approaches each stage's target — including release
  // tails after NoteOff cuts attack short, avoiding an abrupt cutoff.
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
  // Chiff is folded into RenderStage's main loop — no post-pass.
}

void Envelope::RenderStageDispatch(
  int16_t* sample_buffer, size_t samples_left,
  int32_t bias_q31, int32_t bias_slope_q31
) {
  if (phase_increment_u32_ == 0) {
    RenderStage<false , false>(sample_buffer, samples_left, bias_q31, bias_slope_q31);
  } else if (expo_slope_lut_q30_[0] > 0) {
    RenderStage<true  , true >(sample_buffer, samples_left, bias_q31, bias_slope_q31);
  } else {
    RenderStage<true  , false>(sample_buffer, samples_left, bias_q31, bias_slope_q31);
  }
}

#define VALUE_PASSED(x) ( \
  ( POSITIVE_SLOPE && value_q30 >= x) || \
  (!POSITIVE_SLOPE && value_q30 <= x) \
)

// Output composition collapsed to single USAT-with-shift:
//   original: (v >> 14) + (bias >> 15), clip to u16, then >> 1.
//   = ((v << 1) + bias) >> 16, clamped to [0, 32767].
// USAT can fold the arithmetic-shift-right into the same instruction, so
// the whole compose+clip+halve becomes one add + one usat.
//
// The SSAT #31 on v before the <<1 is defensive: chiff may push v
// beyond the natural Q30 range. SSAT clamps to [-2^30, 2^30-1] so the
// <<1 fits int32 without wrap. No-op for non-chiff paths (v already in
// range). 1 cycle.
#define OUTPUT_VALUE(v) \
  bias_q31 += bias_slope_q31; \
  { \
    int32_t v_sat; \
    __asm__ ("ssat %0, #31, %1" : "=r"(v_sat) : "r"(static_cast<int32_t>(v))); \
    int32_t output_composed = bias_q31 + (v_sat << 1); \
    int32_t output_saturated; \
    __asm__ ("usat %0, #15, %1, asr #16" \
             : "=r"(output_saturated) : "r"(output_composed)); \
    *sample_buffer++ = static_cast<int16_t>(output_saturated); \
  }

#define OUTPUT OUTPUT_VALUE(value_q30 + chiff_offset)

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
  const int32_t* const slope_lut = expo_slope_q30;
  // Drive the loop by buffer-end pointer instead of a samples_left
  // counter, freeing a register for the LUT base hoist.
  int16_t* const buffer_end = sample_buffer + samples_left;

  // Chiff: additive random-walk offset (chiff_offset), independent of
  // the envelope's clean trajectory. Per sample in MOVING, when the
  // roll fires (roll < prob), perturb chiff_offset by ±(slope << shift),
  // then SSAT-clamp to ±2^28 to bound the walk. Output = value +
  // chiff_offset (further SSAT-clamped in OUTPUT_VALUE). VALUE_PASSED
  // sees the clean value, so trajectory and stage transitions are
  // unaffected by chiff. In !MOVING, chiff_offset is decayed toward 0
  // (1-pole, alpha=1/16) so a non-zero walk left over from attack/decay
  // settles smoothly into clean sustain output.
  //
  // shift = max(0, 7 - clz(margin)/4) in [0, 7]
  // perturbation = ±(slope << shift); variance per fire = 2^(2·shift),
  // up to 16384 ({-127, +129} multipliers).
  uint32_t prob = chiff_probability_u31_;
  const uint32_t dec = chiff_prob_decrement_u32_;
  const uint32_t prng_xor = chiff_prng_xor_u32_;
  int32_t chiff_offset = chiff_offset_q30_;
  const uint32_t* prng_ptr = &shared_prng_buffer[kAudioBlockSize - samples_left];

  while (sample_buffer < buffer_end) {
    // Leaky integrator on chiff_offset: decay every sample (1-pole LPF,
    // alpha=1/16, time constant ~16 samples). Bounds the random walk
    // by variance rather than by walls — prior SSAT-only design pinned
    // the walk to ±2^28 because kicks were comparable to the box size.
    chiff_offset -= chiff_offset >> 4;

    if (!MOVING) {
      value_q30 = target_q30; // In case we skipped a stage with delta that was 1) nonzero and 2) too small to produce a nonzero slope
      OUTPUT;
      continue;
    }

    // Phase advance. Wrap saturation removed: when phase wraps near
    // end-of-stage, lut_index resets to 0 (steepest slope), which makes
    // value jump past target → VALUE_PASSED fires within 1–2 samples
    // anyway. Saves ~2 cycles/sample in the hot loop.
    phase_u32 += phase_increment_u32;

    uint8_t lut_index = phase_u32 >> (32 - kLutExpoSlopeShiftSizeBits);
    int32_t slope_q30 = slope_lut[lut_index];

    // Chiff perturbation with margin-derived intensity.
    uint32_t prng = *prng_ptr++ ^ prng_xor;
    uint32_t roll = prng >> 1;
    if (roll < prob) {
      uint32_t margin = prob - roll;
      int32_t shift_signed = 5 -
          static_cast<int32_t>(__builtin_clz(margin) >> 2);
      uint32_t shift = shift_signed < 0 ? 0u :
          static_cast<uint32_t>(shift_signed);
      // SSAT #24 caps |slope_for_chiff| ≤ 2^23 before the shift, so
      // (slope << 7) fits int31 — no overflow into bit 31. For fast
      // attacks where slope > 2^23, perturbation magnitude is capped
      // at 2^30 (sufficient since short attacks don't need huge kicks).
      // For typical slopes (< 2^23), SSAT is a no-op.
      int32_t slope_for_chiff;
      __asm__ ("ssat %0, #24, %1"
               : "=r"(slope_for_chiff) : "r"(slope_q30));
      int32_t scaled = slope_for_chiff << shift;
      int32_t perturbed = (prng & 1u) ? (chiff_offset + scaled)
                                      : (chiff_offset - scaled);
      // Clamp chiff_offset magnitude to ±2^28 (SSAT #29 → signed 29-bit).
      __asm__ ("ssat %0, #29, %1"
               : "=r"(chiff_offset) : "r"(perturbed));
    }
    // Saturating decrement via SUBS + USAT: clamps signed (prob − dec)
    // to [0, 2^31), giving 0 on underflow.
    int32_t signed_prob;
    __asm__ (
        "subs %0, %1, %2\n\t"
        "usat %0, #31, %0"
        : "=r"(signed_prob)
        : "r"(prob), "r"(dec)
        : "cc");
    prob = static_cast<uint32_t>(signed_prob);

    // Clean trajectory: value advances on the unperturbed slope.
    value_q30 += slope_q30;
    if (VALUE_PASSED(target_q30)) {
      value_q30 = target_q30; // Don't overshoot target
      OUTPUT;

      value_q30_ = value_q30; // So Trigger knows actual start value
      chiff_probability_u31_ = prob;
      chiff_offset_q30_ = chiff_offset;
      Trigger(static_cast<EnvelopeStage>(stage + 1));

      // Even if there are no samples left, this will save bias state for us
      return RenderStageDispatch(sample_buffer, buffer_end - sample_buffer, bias_q31, bias_slope_q31);
    } else {
      OUTPUT;
    }
  }

  // Render is complete, but stage is not -- save state for next render
  value_q30_ = value_q30;
  phase_u32_ = phase_u32;
  phase_increment_u32_ = phase_increment_u32;
  chiff_probability_u31_ = prob;
  chiff_offset_q30_ = chiff_offset;

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
