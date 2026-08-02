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
  // optimize the best case. At AMOUNT 0 the same loop degenerates by itself:
  // chiff input 0 -> the chiff one-pole holds 0 -> the scaled rms is 0, so the
  // mean clamp is a no-op and the output is nominal + bias.
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
  // chiff's own slew slowing (toward its max slew time, compressed into
  // the remaining stage when the stage is shorter than the chiff).
  void RederiveSlewState();

  // the +/- the chiff puts on the slew input -- half the note's
  // ALLOWED range times the shrink, so it does not follow the realized level.
  int32_t ChiffInput_q30() const;

  // how slow the chiff's slew will have got after `samples` more
  // samples, never past its max. The shrink is sized against this.
  uint32_t ChiffSlewTimeAtDeadline_q5_27(uint32_t samples) const;

  // the MAX slew time the chiff reaches: the one its own duration
  // implies, and nothing else. No stage term -- the chiff's filter is its own,
  // so how fast the stage runs has no claim on how slow the chiff may get.
  uint32_t ChiffMaxSlewTime_q5_27() const;

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

  // How the rate falls while the chiff runs: decay = 1 - 2^-step (Q32), so
  // rate -= (rate*decay)>>32 each sample == rate *= 2^-step, reproducing the
  // slew time rising linearly, with no per-sample LUT. Zero = hold.
  int32_t chiff_slew_rate_decay_q32_;

  // Per-instance start offset into the double-length shared PRNG buffer.
  // Distinct offsets mean co-triggered envelopes never consume the same
  // random word on the same sample, so their chiff draws are decorrelated
  // without per-sample work. Assigned round-robin in Init().
  uint32_t prng_offset_u32_;

  // CHIFF. A filtered noise added to the envelope, with its own filter state.
  //
  // THREE TERMS MAKE THE OUTPUT, and they are independent:
  //   nominal   the envelope with no chiff. Its own one-pole, running at the
  //             STAGE's rate, chasing the stage's aim.
  //   chiff     this. Its own one-pole, running at the CHIFF's rate, chasing
  //             +/- the chiff input with the sign drawn per sample from the
  //             shared PRNG. Zero-mean.
  //   bias      added at the point of use and never integrated.
  // out = saturate(mean + chiff), where mean = nominal + bias held one scaled
  // rms (2.121 sigma) inside each DAC rail so the chiff has room. The chiff is
  // ADDED to a mean that already has it, so it is never clipped, and the clamp
  // does not feed back: value_q30_ is nominal + chiff and carries no bias.
  //
  // THE CHIFF INPUT is half the note's ALLOWED range times
  // chiff_input_fraction_q30_, so it does not follow the level the note
  // reaches and a quiet note gets the same exciter.
  //
  // TWO THINGS DECAY, and they divide the work by TIME rather than by
  // proportion, so neither covers for the other:
  //   the SLEW SLOWING contributes a curve straight in dB, about -4 dB per 10%
  //   of the chiff's duration. Amount-independent.
  //   the CHIFF INPUT SHRINKING contributes 20log10(1 - t/duration): gentle
  //   early, steep at the end. The steepening is that mechanism finishing, not
  //   an artifact.
  // How the chiff input shrinks is not free to change without re-measuring
  // what the slew is doing at the same time.
  //
  // AMOUNT SETS THE STARTING SLEW TIME AND NOTHING ELSE. It does not scale the
  // chiff input. Low amounts are quiet because a slow filter realizes less of
  // the same chiff input. At AMOUNT 0 the chiff input is zero, the chiff
  // one-pole holds zero, and the output is nominal + bias.
  //
  // NOTHING NAMES THE OUTPUT. What you hear is the chiff input times the
  // filter's response, which falls as sqrt(rate) -- about 3 dB per octave.
  // KEEP THE CAUSAL CHAIN VISIBLE: an effect may cause a further effect, but an
  // effect must not be promoted into a thing that acts on its own with the
  // chain back to a mechanism lost. Every recurring confusion here was that --
  // input mistaken for output, or one effect assumed to have one cause.
  //
  // ONLY THE SLEW TIME IS STORED. The rate is 2^-slew_time, one quantity in two
  // encodings; keeping both meant two accumulators that could drift apart.
  // RenderStage derives the rate once per run. Slew time is unsigned: a
  // magnitude, 0..kMaxSlewTimeLog2, whose max exceeds 2^31 as Q5.27.
  //
  // Things that did not work: per-block alpha (cb68505b), the two-point
  // mixture, an absolute slew-rate floor with a chiff input rescale, and the
  // sag -- a level move to make room for the chiff, which could not be made to
  // track a moving bias because the correction went through the integrator
  // (f5eee4b6 replaced it with the mean clamp above).
  uint32_t slew_time_log2_q5_27_;             // Current slew time, log2 samples
  uint32_t chiff_slew_time_log2_step_q5_27_;  // Per-sample rise, i.e. how fast
                                             // the slew slows (>= 0)
  uint32_t chiff_slew_time_log2_end_q5_27_;   // Max slew time the chiff's own
                                             // goes, from its duration
  // The NOMINAL chiff duration, in samples: a sizing reference for how fast
  // the slew slows and the chiff input shrinks. NOT a countdown -- nothing
  // observes it elapsing, and there is no window to be inside of.
  // 0 = no chiff on this note (AMOUNT 0).
  uint32_t chiff_target_samples_;
  // Where the current stage began: with the stage phase (closed-form from
  // the countdown), this anchors the nominal value -- start + (target - start) *
  // lut_env_expo[phase] -- with no iterated level state, the same
  // construction the duty-binary core used for its duty curve.
  int32_t stage_start_q30_;
  // How much of chiff_input_full_q30_ is in use, Q30 (1<<30 == all of it),
  // decaying by chiff_input_fraction_step octaves per sample. Dimensionless, so
  // unlike the levels it does not rescale.
  // NOT a fraction of the SLACK between the level and the rails: that sizing
  // was tried and rejected, because the slack vanishes at the peak and the
  // excursion notched there. See ChiffInput_q30.
  int32_t chiff_input_fraction_q30_;
  uint32_t chiff_input_fraction_step_q5_27_;
  // Half the note's ALLOWED range: the chiff input at fraction 1.0, i.e.
  // before any decay. A LEVEL, so it rescales with the others.
  int32_t chiff_input_full_q30_;
  // Ordered clamp bounds over the note's stage targets. The envelope's range
  // may be numerically inverted (CV DAC codes fall as volts rise; a warped
  // timbre target may be negative), so these are min/max, not release/peak.
  int32_t chiff_floor_q30_;
  int32_t chiff_top_q30_;
  // Where the render loop measures the value FROM. min(chiff_floor, 0): USAT
  // bounds [0, 2^30) and nothing else, so a note whose range reaches below
  // zero is rendered offset by its floor. Held as state rather than derived in
  // RenderStage because a local stays live across the whole per-run path and
  // GCC spills it -- MEASURED 383 -> 413 instructions, 58 -> 72 spills.
  int32_t clamp_base_q30_;
  // THE THREE TERMS the output is built from. nominal is the chiff-free
  // envelope -- its own one-pole, running at the STAGE's rate, chasing the
  // stage's aim. chiff_state is the zero-mean filtered chiff input -- its own
  // one-pole, running at the CHIFF's rate. bias is the terminal add.
  // value_q30_ is kept as nominal + chiff for the consumers that read it.
  int32_t nominal_q30_;
  int32_t chiff_state_q30_;

  DISALLOW_COPY_AND_ASSIGN(Envelope);
};

}  // namespace yarns

#endif  // YARNS_ENVELOPE_H_
