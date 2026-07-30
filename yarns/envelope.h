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
  // correctly by itself: perturbation 0 -> slew input is the target -> output
  // = the nominal value, clamp transparent.
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
  // Re-derive the slew coefficients after a stage change: the classic
  // rate from the new stage's slew time and, if the chiff is live, the
  // chiff's sweep (toward its end slew time, compressed into
  // the remaining stage when the stage is shorter than the chiff).
  void RederiveSlewState();

  // EXPERIMENT: the +/- the chiff puts on the slew input -- half the note's
  // ALLOWED range times the shrink, so it does not follow the realized level.
  int32_t ChiffPerturb_q30() const;

  // EXPERIMENT: where the chiff's slew-time sweep will be after `samples`
  // more samples, capped at its end. The shrink is sized against this.
  uint32_t ChiffSlewTimeAtDeadline_q5_27(uint32_t samples) const;

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

  // The stage's own slew time, log2 samples, Q5.27. Integer part is the base
  // downshift; the fraction dithers to the next integer via the sigma-delta
  // accumulator, interpolating slew times between powers of two.
  uint32_t stage_slew_time_log2_q5_27_;

  // Slew rate 2^-slew_time_log2, Q31 (1.0 == 2^31): value += (input-value)*rate>>31.
  // Positive, <= 0x7FFF8000 < 2^31 (single signed SMULL vs the signed delta).
  int32_t slew_rate_q31_;

  // Sweep of the rate while the chiff runs: decay = 1 - 2^-step (Q32), so
  // rate -= (rate*decay)>>32 each sample == rate *= 2^-step, reproducing the
  // linear slew-time sweep with no per-sample LUT. Zero = hold.
  int32_t chiff_slew_rate_decay_q32_;

  // Per-instance start offset into the double-length shared PRNG buffer.
  // Distinct offsets mean co-triggered envelopes never consume the same
  // random word on the same sample, so their chiff draws are decorrelated
  // without per-sample work. Assigned round-robin in Init().
  uint32_t prng_offset_u32_;

  // CHIFF. The envelope's own slew, sped up and fed a perturbed input.
  //
  //   slew input    what the slew chases: base +/- the perturbation, sign
  //                 drawn per sample from the shared PRNG
  //   perturbation  half the note's ALLOWED range times
  //                 chiff_perturb_shrink_q30_ -- independent of the level the
  //                 note reaches, so a quiet note gets the same exciter. The
  //                 shrink is what decays with time.
  //   sag           near a rail the perturbation does not fit, so the CENTRE
  //                 moves off the rail by the shortfall. Sized from the
  //                 REALIZED excursion, so it vanishes with the noise
  //   slew time     ramped linearly (so the RATE decays exponentially) from a
  //                 start set by AMOUNT to an end set by the window
  //
  // NOTHING NAMES THE OUTPUT. What you hear is the perturbation times the
  // slew's response to it, which falls as sqrt(rate) -- about 3 dB per octave.
  //
  // KEEP THE CAUSAL CHAIN VISIBLE. There are exactly two mechanisms here --
  // the slew slowing and the perturbation shrinking. Everything else described
  // below is downstream of them and is written with its cause attached. An
  // effect may perfectly well cause a further effect; what must not happen is
  // an effect being promoted into a thing that acts on its own, with the chain
  // back to a mechanism lost. That is where the recurring confusions came
  // from: input mistaken for output, and one effect assumed to have one cause
  // when two mechanisms were producing it jointly.
  //
  // TWO THINGS DECAY, and they divide the work by TIME rather than by
  // proportion: the slew slowing, and the perturbation shrinking. MEASURED in
  // the sim by rendering the full curve with the perturbation shrinking and
  // held constant, then subtracting:
  //
  //   THE SLEW SLOWING, on its own, CONTRIBUTES a curve that is straight in
  //   dB -- about -4 dB per 10% of the window -- for the first 80%, and then
  //   goes flat at -30 dB. (That flattening is a consequence, not a thing the
  //   code does.) Amount-independent: -30.7 at AMOUNT 96, -31.0 at 127.
  //
  //   It flattens because two rates compete. Hold the slew rate and the
  //   perturbation fixed and the value's wandering settles to an RMS of
  //   perturbation * sqrt(rate/2). The rate is NOT fixed -- it collapses
  //   exponentially by design -- so where the wandering would settle keeps
  //   dropping, a constant dB per second. Meanwhile the value can only shed
  //   its excess AT the slew rate, which is collapsing too. Early on it keeps
  //   up; past ~80% of the window it cannot, and the curve flattens carrying
  //   excursion it can no longer shed. Nothing here is a "target" -- the
  //   value is not chasing a point, it just arrives at an amplitude.
  //
  //   THE PERTURBATION SHRINKING carries the output from that -30 dB floor to
  //   silence, contributing 20log10(1 - t/window): gentle early, steep at the
  //   edge. The steepening you see at the end is a CONSEQUENCE of that, not an
  //   artifact to smooth away -- it is what the second mechanism finishing the
  //   job looks like. Soften it and the flattening above is left exposed,
  //   which is exactly what shrinking the perturbation exponentially was
  //   measured to do (fast decay, then a plateau near -40 dB).
  //
  // So neither covers for the other, and how the perturbation shrinks is not
  // free to change without re-measuring where the slew gives up.
  //
  // AMOUNT sets the STARTING slew time and nothing else -- it does NOT scale
  // the perturbation. Low amounts are quiet because a slow slew realizes less
  // of the same perturbation. At AMOUNT 0 (or window closed) the input
  // collapses to the stage target: the classic per-sample slew, exactly.
  //
  // The slew input centre is derived so the value's EXPECTED step equals the
  // value's step in every regime: centre = nominal + (target - nominal) *
  // stage_rate/effective_rate (timed stages; holds use the target). Anything
  // else makes the value chase the moving nominal value through its slew -- two
  // cascaded one-poles -- so it trails the envelope (a kink wherever the
  // window ends). The rate is also floored at the stage rate on timed stages
  // (else an early release near the window's slow end hangs); hold stages are
  // exempt so the chiff still closes on its own schedule.
  //
  // The window spans stages (sustain included). Only a stage shorter than the
  // remaining window (in practice the release) compresses it: both the
  // perturbation and the slew time re-sloped to land by stage end.
  //
  // While the chiff runs, slew_rate_q31_/slew_time_log2_q5_27_ are the
  // chiff's (ramping); stage_slew_rate_q31_ carries the stage rate. With the
  // chiff off they are the classic slew and the value is exactly that.
  //
  // Slew time is unsigned: a magnitude, 0..kMaxSlewTimeLog2. The max exceeds
  // 2^31 as Q5.27 (integer part up to 27), so int32 would sign-flip.
  uint32_t slew_time_log2_q5_27_;             // Current slew time, log2 samples
  uint32_t chiff_slew_time_log2_step_q5_27_;  // Per-sample sweep step (>= 0)
  uint32_t chiff_slew_time_log2_end_q5_27_;   // Sweep end, set by the window
  uint32_t chiff_duration_samples_left_;
  uint32_t exp_target_samples_;  // EXPERIMENT ONLY  // 0 = chiff inactive
  // Where the current stage began: with the stage phase (closed-form from
  // the countdown), this anchors the nominal value -- start + (target - start) *
  // lut_env_expo[phase] -- with no iterated level state, the same
  // construction the duty-binary core used for its duty curve.
  int32_t stage_start_q30_;
  int32_t stage_slew_rate_q31_;               // Stage rate (floor/blend)
  // EXPERIMENT: how much of the available slack the perturbation uses, Q30
  // (1<<30 == all of it), shrinking by chiff_perturb_shrink_step octaves per
  // sample. Dimensionless, so unlike the levels it does not rescale.
  int32_t chiff_perturb_shrink_q30_;
  uint32_t chiff_perturb_shrink_step_q5_27_;
  // EXPERIMENT: half the note's ALLOWED range -- the perturbation at full
  // shrink. A LEVEL, so it rescales with the others.
  int32_t chiff_perturb_full_q30_;
  // Ordered clamp bounds over the note's stage targets. The envelope's range
  // may be numerically inverted (CV DAC codes fall as volts rise; a warped
  // timbre target may be negative), so these are min/max, not release/peak.
  int32_t chiff_floor_q30_;
  int32_t chiff_top_q30_;

  DISALLOW_COPY_AND_ASSIGN(Envelope);
};

}  // namespace yarns

#endif  // YARNS_ENVELOPE_H_
