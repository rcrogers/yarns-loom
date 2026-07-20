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
    uint8_t chiff_amount, uint8_t chiff_duration
  );
  void Trigger(EnvelopeStage stage);
  void RenderSamples(int16_t* sample_buffer, int32_t bias_target_q31);
  // Single render path: this is a realtime system, so the worst case (chiff
  // live) is the only case that matters; a lean chiff-off variant would only
  // optimize the best case. With the window closed the same loop degenerates
  // correctly by itself: amp = 0 -> dart targets 0 -> pert stays 0 -> output
  // = dialed, clamp transparent.
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
  // Re-derive the slew coefficients after a stage change: the classic/dialed
  // alpha from the new stage's nominal shift and, if the chiff is live, the
  // noise-slew ramp (toward the chiff's own dark endpoint, compressed into
  // the remaining stage when the stage is shorter than the chiff).
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

  // Chiff (dart model): ONE slewed value aims at three ABSOLUTE per-run-held
  // points -- center +- depth (random-sign darts, ~half the samples) and a
  // relax base -- and the output is that value clamped to the note's range.
  // The mean rides the aim statistics; `dialed` (the chiff-free classic
  // slew) is advanced run-exact purely to place the aims.
  //
  // The aim base is derived so the value's EXPECTED step equals the mean's
  // step in every regime: base = dialed + (target - dialed) *
  // alphaStage/alphaEff (timed stages; holds use dialed). Anything else
  // makes the value chase the moving mean through its own slew -- two
  // cascaded one-poles -- and it trails the envelope (a kink wherever the
  // window ends). The noise slew is also floored at the stage rate on timed
  // stages (else an early release near the window's dark end hangs); hold
  // stages are exempt so the LPF still closes on the chiff's own schedule.
  //
  // The dart depth amp = 0.9*(top - floor) fades linearly over the chiff
  // window; the noise slew's shift ramps linearly from an amount-warped
  // bright onset to the chiff's OWN dark endpoint (log2(window) - k), so the
  // burst darkens to ~DC by its own end regardless of stage. Near the
  // acoustic-peak rail the aim center shifts off the rail by the noise's
  // realized reach (amp * lut_chiff_reach_factor[shift]). At amount 0 (or
  // window closed) the aims collapse to the stage target: the classic
  // per-sample exponential slew, exactly.
  //
  // The chiff window spans stages (sustain included). Only a stage shorter
  // than the remaining window (in practice the release) compresses it: fade
  // and shift ramp re-sloped to land by stage end.
  //
  // While the chiff runs, slew_alpha_q31_/slew_shift_q5_27_ describe the
  // NOISE slew (ramping); dialed_alpha_q31_ carries the stage-nominal rate
  // for the dialed slew. With the chiff off they describe the classic slew
  // and the value is the exact classic slew.
  //
  // Shift is unsigned: a magnitude, 0..kMaxSlewShift. The max exceeds 2^31
  // as Q5.27 (integer shift up to 27), so int32 would sign-flip and corrupt
  // derived shifts.
  uint32_t slew_shift_q5_27_;            // Noise-slew shift while chiff runs
  uint32_t slew_shift_increment_q5_27_;  // Per-sample ramp step (>= 0)
  uint32_t chiff_dark_shift_q5_27_;      // Ramp endpoint: the chiff's own dark
  uint32_t chiff_duration_samples_left_;  // 0 = chiff inactive
  // Where the current stage began: with the stage phase (closed-form from
  // the countdown), this anchors the mean -- start + (target - start) *
  // lut_env_expo[phase] -- with no iterated level state, the same
  // construction the duty-binary core used for its duty curve.
  int32_t stage_start_q30_;
  int32_t dialed_alpha_q31_;             // Stage-nominal alpha (floor/blend)
  int32_t chiff_amp_q30_;                // Current dart depth
  int32_t chiff_amp_step_q30_;           // Per-sample depth fade
  // Ordered clamp bounds over the note's stage targets. The envelope's range
  // may be numerically inverted (CV DAC codes fall as volts rise; a warped
  // timbre target may be negative), so these are min/max, not release/peak.
  int32_t chiff_floor_q30_;
  int32_t chiff_top_q30_;
  // Which rail is the acoustic peak (the far side from the release level):
  // the reach fit shifts the dart center off that rail; the other rail keeps
  // the plain output clamp (the sim-validated onset trim).
  bool chiff_fit_at_floor_;

  DISALLOW_COPY_AND_ASSIGN(Envelope);
};

}  // namespace yarns

#endif  // YARNS_ENVELOPE_H_
