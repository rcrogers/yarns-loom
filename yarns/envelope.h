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

class Envelope {
 public:
  Envelope() { }
  ~Envelope() { }

  void Init(int16_t zero_value_s16);
  // Refill the system-wide PRNG buffer consumed by the chiff draws; must
  // be called once per audio block, before any envelope renders.
  static void FillSharedPrngBuffer();
  void NoteOff();
  void NoteOn(
    ADSR& adsr,
    // Bounds stored as s32 but semantically s16
    int32_t min_target_s16, int32_t max_target_s16,
    uint8_t chiff_amount
  );
  void Trigger(EnvelopeStage stage);
  void RenderSamples(int16_t* sample_buffer, int32_t bias_target_q31);
  // Dispatch on chiff activity so the chiff-inactive render carries none of
  // the chiff machinery (draw, duty, coefficient ramp). Transitions happen
  // once, at a re-dispatch point, not per sample.
  void RenderStageDispatch(
    int16_t* sample_buffer, size_t block_samples_left,
    int32_t bias_q31, int32_t bias_slope_q31
  );
  template<bool Chiff>
  void RenderStage(
    int16_t* sample_buffer, size_t block_samples_left,
    int32_t bias_q31, int32_t bias_slope_q31
  );
  // Trimmed to the same arg footprint as RenderStage so the transition
  // tail-call stays flat (sibling call, no per-transition frame).
  void HandOffToNextStage(
    int16_t* sample_buffer, size_t block_samples_left,
    int32_t bias_q31, int32_t bias_slope_q31
  );

  void Rescale(int32_t numerator, int32_t denominator);

 private:
  // Point the slew shift's increment at the current stage-nominal shift,
  // spread over the remaining chiff duration; with no duration left, the
  // shift simply is the nominal.
  void ReSlopeSlewShift();

 public:

  // Step the running bias state directly, bypassing the per-block slew that
  // RenderSamples applies. Used to absorb an instantaneous bias jump (e.g. a
  // pitch-driven timbre step at NoteOn) so it doesn't get smoothed into an
  // audible glide, while continuous (LFO) bias motion stays slewed.
  inline void AdjustBias(int32_t delta_q31) { bias_q31_ += delta_q31; }

  inline int16_t tremolo(uint16_t strength_u16) const {
    int32_t relative_value_q15 = (value_q30_ - stage_target_q30_[ENV_STAGE_RELEASE]) >> (30 - 15);
    return relative_value_q15 * -strength_u16 >> 16;
  }

  inline int16_t value() const { return value_q30_ >> (30 - 15); }
  inline EnvelopeStage stage() const { return stage_; }

 private:
  ADSR* adsr_;

  // Q30 in int32_t; the top integer bit is headroom for the slew delta
  // (target - value spans up to 2^31 - 1, still within int32).
  int32_t stage_target_q30_[ENV_NUM_STAGES];
  int32_t target_q30_, value_q30_;

  // Q31 (full s32; no overshoot, slope is pre-scaled by block size).
  int32_t bias_q31_;

  // Current stage.
  EnvelopeStage stage_;

  // Nonzero for timed stages (attack/decay/release); doubles as the source
  // of the stage's nominal sample count. Zero for hold stages
  // (sustain/dead), which slew toward their target indefinitely.
  uint32_t phase_increment_u32_;

  // Timed stages: samples remaining before handing off to the next stage.
  // The slew ends wherever it is at that point -- no snap to target; the
  // next stage's slew continues seamlessly from the current value.
  uint32_t stage_samples_left_;

  // Per-sample slew: value += (target - value) >> shift. The shift is a
  // Q5.27 fixed-point value; the integer part is the base downshift, and
  // the fraction dithers to the next integer shift via the sigma-delta
  // accumulator below, interpolating time constants between powers of two.
  uint32_t stage_nominal_slew_shift_q5_27_;

  // Slew rate 2^-shift in Q31 (2^31 == 1.0): value += (target-value)*alpha>>31.
  // Positive, <= 0x7FFF8000 < 2^31 (single signed SMULL vs the signed delta).
  int32_t slew_alpha_q31_;

  // Geometric ramp of the coefficient while chiff runs: decay = 1 - 2^-increment
  // (Q32), so alpha -= (alpha*decay)>>32 each sample == alpha *= 2^-increment,
  // reproducing the linear-shift ramp with no per-sample LUT. Zero = hold.
  int32_t slew_alpha_decay_q32_;

  // Per-instance start offset into the double-length shared PRNG buffer.
  // Distinct offsets mean co-triggered envelopes never consume the same
  // random word on the same sample, so their chiff draws are decorrelated
  // without per-sample work. Assigned round-robin in Init().
  uint32_t prng_offset_u32_;

  // Chiff: while the chiff duration runs, the slew shift starts below
  // stage-nominal (i.e. faster) and each sample has a probability of
  // slewing toward a random target instead of the stage target. Chiff
  // intensity fades via the shift itself: as the shift rises toward
  // nominal, random targets are tracked ever more sluggishly. The duration
  // is armed to the attack's nominal length at NoteOn and keeps its
  // original timetable through stage transitions; each Trigger re-slopes
  // the increment toward the new stage's nominal over the remaining
  // duration (signed: the shift may sit above or below the new nominal).
  // This keeps the shift -- and thus the chiff perturbation amplitude,
  // 2^-shift -- free of discontinuities at early release, while still
  // landing on the release's correct slew when the duration ends.
  // Unsigned: a shift magnitude, 0..kMaxSlewShift. The max exceeds 2^31
  // (integer shift up to 27), so int32 would sign-flip on long attacks
  // (>= ~5.8s, nominal shift ~16 = 2^31) and corrupt `>> shift`.
  uint32_t slew_shift_q5_27_;            // == nominal when chiff is over
  uint32_t slew_shift_increment_q5_27_;  // Per-sample step (>= 0 post-clamp)
  uint32_t chiff_duration_samples_left_;  // 0 = chiff inactive

  // While the chiff duration runs, each sample's slew target is one of
  // exactly two levels: the stage's start value or the stage's target,
  // picked by a coin whose weight is the duty -- P(stage target), an
  // exponential 1 - e^(-4*phi) over the stage's progress phi, read from
  // lut_env_expo (the same curve, k = 4, that the slew itself traces, so
  // the mixture's mean reproduces the dialed trajectory). The mixing
  // depth is then crossfaded against the residual slew lag by
  // beta = 1 - 2^-(nominal - ramp) (from lut_expo2_neg), which drains the
  // chiff to nothing as the shift ramp reaches nominal and makes chiff
  // amount continuous from bit-exact classic at 0. phi advances at
  // segment rate (see RenderStage); phase == UINT32_MAX pins the duty to
  // all-target for hold stages and once the stage sweep completes.
  int32_t chiff_off_target_q30_;         // Value captured at Trigger
  uint32_t chiff_duty_phase_u32_;         // Stage progress phi, Q0.32

  DISALLOW_COPY_AND_ASSIGN(Envelope);
};

}  // namespace yarns

#endif  // YARNS_ENVELOPE_H_
