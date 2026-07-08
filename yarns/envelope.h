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
  // Refill the system-wide PRNG buffer consumed by the slew-shift dither;
  // must be called once per audio block (before any envelope renders) so
  // all envelopes share the same random words this block.
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
  // One render loop for all envelopes, chiff or not: this is a realtime
  // system, so the worst case is the only case that matters, and a lean
  // variant would only flatter the average.
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
  // Point the chiff ramp's increment at the current stage-nominal shift,
  // spread over the remaining chiff window; with no window left, the ramp
  // simply is the nominal.
  void ReSlopeChiffRamp();

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
  uint32_t phase_samples_left_;

  // Per-sample slew: value += (target - value) >> shift. The shift is a
  // Q5.27 fixed-point value; the integer part is the base downshift, and
  // the fraction dithers to the next integer shift via the sigma-delta
  // accumulator below, interpolating time constants between powers of two.
  uint32_t slew_shift_q5_27_;

  // Sigma-delta state for the fractional shift: the fraction (as Q32) is
  // accumulated per sample, and the carry selects shift + 1. Deterministic
  // first-order noise shaping: the same average coefficient as random
  // dither, but the error is a periodic high-frequency ripple instead of
  // white noise + random walk. Free-running across stages; seeded from the
  // instance address in Init() so co-triggered envelopes' ripple patterns
  // are phase-offset rather than correlated.
  uint32_t dither_phase_u32_;

  // Per-instance start offset into the double-length shared PRNG buffer.
  // Distinct offsets mean co-triggered envelopes never consume the same
  // random word on the same sample, so their chiff draws are decorrelated
  // without per-sample work. Assigned round-robin in Init().
  uint32_t prng_offset_u32_;

  // Chiff: while the timer runs, the slew shift follows a ramp that starts
  // below stage-nominal (i.e. faster) and each sample has a gate
  // probability of slewing toward a random target instead of the stage
  // target. Chiff intensity fades via the shift itself: as the ramp rises,
  // random targets are tracked ever more sluggishly. The timer is armed to
  // the attack's nominal duration at NoteOn and keeps its original
  // timetable through stage transitions; each Trigger re-slopes the
  // increment toward the new stage's nominal over the remaining window
  // (signed: the ramp may sit above or below the new nominal). This keeps
  // the shift -- and thus the chiff perturbation amplitude, 2^-shift --
  // free of discontinuities at early release, while still landing on the
  // release's correct slew when the window closes.
  int32_t chiff_shift_ramp_q5_27_;      // == nominal when timer is 0
  int32_t chiff_ramp_increment_q5_27_;  // Signed per-sample step
  uint32_t chiff_samples_left_;         // 0 = chiff inactive
  uint32_t chiff_gate_u16_;             // P(random target), 16-bit
  int32_t chiff_floor_q30_;             // Random target range: floor...
  int32_t chiff_span_q14_;              // ...+ (span >> 16) * rand16

  // Snap-to-bound: gate rolls below this threshold (a sub-range of the
  // firing range, so the decision costs no extra entropy and stays
  // independent of the value draw) snap the random target to floor or
  // peak, side picked by the value draw's top bit. The target
  // distribution becomes point masses at the bounds plus a uniform body.
  // Engages above half chiff amount, full-scale at 127; the underlying
  // fraction fades to zero over the chiff window, refreshed at block rate
  // in RenderSamples -- smooth at control rate without a per-sample cost.
  uint32_t chiff_snap_threshold_u16_;
  uint32_t chiff_snap_fraction_u32_;
  uint32_t chiff_snap_block_decrement_u32_;

  DISALLOW_COPY_AND_ASSIGN(Envelope);
};

}  // namespace yarns

#endif  // YARNS_ENVELOPE_H_
