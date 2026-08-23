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

// Envelope instances that can be live at once.
//
// It is stated here rather than derived because Envelope must not depend on
// its owners (voice.h already includes this header). multi.h holds the layout
// map, folds the true maximum out of it, and asserts that fold EQUALS this --
// so the compiler, not a comment, is what keeps the number honest.
const size_t kMaxChiffEnvelopes = 13;

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

// CHIFF DURATION picks an increment off lut_chiff_phase_increments; this is
// the reciprocal that turns it into the audible duration in samples. Called
// ONCE per note, where the table is read -- every Envelope::NoteOn below takes
// the samples. Not file-local because the harnesses report it: it is the
// duration every measurement is expressed against.
uint32_t ChiffAudibleSamples(uint32_t chiff_duration_increment_u32);

class Envelope {
 public:
  Envelope() { }
  ~Envelope() { }

  void Init(int16_t zero_value_s16);
  void NoteOff();
  void NoteOn(
    ADSR& adsr,
    // Bounds stored as s32 but semantically s16
    int32_t min_target_s16, int32_t max_target_s16,
    uint8_t chiff_amount, uint32_t chiff_audible_samples
  );
  void Trigger(EnvelopeStage stage);
  void RenderSamples(int16_t* sample_buffer, int32_t bias_target_q31);
  // Single render path: this is a realtime system, so the worst case (chiff
  // live) is the only case that matters; a lean chiff-off variant would only
  // optimize the best case. At AMOUNT 0 the same loop degenerates by itself:
  // chiff input 0 -> the chiff one-pole holds 0 -> the amplitude is 0, so the
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
  // the +/- the chiff puts on the slew input -- half the note's
  // ALLOWED range times the amount fraction, so it does not follow the realized value.
  int32_t ChiffSlewInput_q30() const;

 public:

  // Step the running bias state directly, bypassing the per-block slew that
  // RenderSamples applies. Used to absorb an instantaneous bias jump (e.g. a
  // pitch-driven timbre step at NoteOn) so it doesn't get smoothed into an
  // audible glide, while continuous (LFO) bias motion stays slewed.
  inline void AdjustBias(int32_t delta_q31) { bias_q31_ += delta_q31; }

  inline int16_t tremolo(uint16_t strength_u16) const {
    int32_t relative_value_q15 = (value_without_bias_q1_30_ - stage_target_q1_30_[ENV_STAGE_RELEASE]) >> (30 - 15);
    return relative_value_q15 * -strength_u16 >> 16;
  }

  inline int16_t value_without_bias() const {
    return value_without_bias_q1_30_ >> (30 - 15);
  }
  inline EnvelopeStage stage() const { return stage_; }

 private:
  ADSR* adsr_;

  // Q30 in int32_t; the top integer bit is headroom for the slew delta
  // (target - value spans up to 2^31 - 1, still within int32).
  int32_t stage_target_q1_30_[ENV_NUM_STAGES];
  int32_t target_q1_30_, value_without_bias_q1_30_;

  // Q31 (full s32; no overshoot, slope is pre-scaled by block size).
  int32_t bias_q31_;

  // Current stage.
  EnvelopeStage stage_;

  // Nonzero for timed stages (attack/decay/release); doubles as the source
  // of the stage's nominal sample count. Zero for hold stages
  // (sustain/dead), which slew toward their target indefinitely.
  uint32_t stage_phase_increment_u32_;

  // Timed stages: samples remaining before handing off to the next stage.
  // The slew ends wherever it is at that point -- no snap to target; the
  // next stage's slew continues seamlessly from the current value.
  uint32_t stage_samples_left_;

  // 2^-(the stage's slew time), capped, Q31. Derived in Trigger rather than
  // per run: it moves only when the stage does, and deriving it costs an exp2
  // table interpolation.
  int32_t stage_slew_rate_q31_;

  // The character axis, as a multiplier on the chiff's filter input, ALREADY
  // DIVIDED by 2^(30 - 26): 1.0 at or below kChiffAmountForDriveBegin, rising to
  // 2^kChiffDriveSpan at full amount. Pre-dividing is what keeps the driven
  // input inside Q30, and it costs nothing because the state is carried
  // scaled down to match.
  int32_t chiff_drive_q4_26_;

  // This envelope's chiff draws: the current word, and how many of its fields
  // are still unspent. The word doubles as the xorshift state -- advancing it
  // is three instructions with no memory traffic -- and both carry across runs,
  // since a block may be rendered in several.
  uint32_t chiff_draws_;
  uint8_t chiff_draws_left_;

  // DURATION IS A TIME-BASED MODULATION OF AMOUNT: the whole decay is this one
  // quantity falling to zero, with the rate, the drive and the input read off
  // it by the SAME maps the knob uses. So a chiff started at any amount decays
  // THROUGH the states every smaller amount has as its onset.
  //   - The clock is dB, not knob units: the axis' top half spans a few dB
  //     while its bottom few units span tens, so stepping it evenly would
  //     plateau then collapse.
  //   - Q7.25, carried finely: at knob resolution this would step 128 times
  //     across the duration, coarser than a run for a long chiff.
  //   - The amount is carried rather than re-derived, because this run's end
  //     is the next run's start.
  uint32_t chiff_amount_initial_q7_25_;
  uint32_t chiff_amount_q7_25_;
  uint32_t chiff_phase_q32_;
  uint32_t chiff_phase_step_q32_;

  // CHIFF. A filtered noise added to the envelope, with its own filter state.
  //
  // THREE TERMS MAKE THE OUTPUT, and they are independent:
  //   nominal   the envelope with no chiff. Its own one-pole, running at the
  //             STAGE's rate, chasing the stage's adjusted target.
  //   chiff     this. Its own one-pole, running at the CHIFF's rate, chasing
  //             one of sixteen levels spanning +/- the chiff input, drawn per
  //             sample from this envelope's own PRNG. Symmetric, so zero-mean.
  //   bias      added at the point of use and never integrated.
  // out = saturate(mean + chiff), where mean = nominal + bias held one scaled
  // rms (2.121 sigma) inside each DAC rail so the chiff has room. The chiff is
  // ADDED to a mean that already has it, so it is never clipped, and the clamp
  // does not feed back: value_without_bias_q1_30_ is nominal + chiff and carries no bias.
  //
  // ONE THING DECAYS: THE AMOUNT. DURATION is a time-based modulation of it,
  // and the drive, the slew time and the input are all read off it by the maps
  // the knob itself uses -- so a chiff started at any amount decays THROUGH the
  // states every smaller amount has as its onset, and nothing has to be kept in
  // step with anything.
  //
  // AMPLITUDE IS PROPORTIONAL TO AMOUNT, across the whole knob. The slew input
  // is solved backwards from that, so the filter's own losses cancel. At
  // AMOUNT 0 the input is zero, the one-pole holds zero, and the output is
  // nominal + bias.
  uint32_t chiff_slew_time_log2_q5_27_;             // Current slew time, log2 samples
  uint32_t chiff_slew_time_at_amount_zero_q5_27_;   // Max slew time the chiff's own
                                             // goes, from its duration
  // Where the current stage began. With the stage phase (closed-form from the
  // countdown) this anchors the nominal value -- start + (target - start) *
  // lut_env_expo[phase] -- with no iterated level state.
  int32_t stage_start_q1_30_;
  // How much of chiff_slew_input_max_q30_ is in use, Q30 (1<<30 == all of it). Set
  // per run from the amount reached. Dimensionless, so unlike the
  // levels it does not rescale.
  int32_t chiff_slew_input_fraction_q30_;
  // Half the note's ALLOWED range: the chiff input at fraction 1.0, i.e.
  // before any decay. A LEVEL, so it rescales with the others.
  int32_t chiff_slew_input_max_q30_;
  // Where the render loop measures the value FROM. min(chiff_floor, 0): USAT
  // bounds [0, 2^30) and nothing else, so a note whose range reaches below
  // zero is rendered offset by its floor. Held as state rather than derived in
  // RenderStage because a local stays live across the whole per-run path, and
  // GCC spills it there.
  int32_t value_floor_q1_30_;
  // THE THREE TERMS the output is built from. nominal is the chiff-free
  // envelope -- its own one-pole, running at the STAGE's rate, chasing the
  // stage's adjusted target. The chiff's slew state is the zero-mean filtered chiff input -- its own
  // one-pole, running at the CHIFF's rate. bias is the terminal add.
  // value_without_bias_q1_30_ is kept as nominal + chiff for the consumers that read it.
  int32_t nominal_value_q1_30_;
  int32_t chiff_slew_state_q26_;

  DISALLOW_COPY_AND_ASSIGN(Envelope);
};

}  // namespace yarns

#endif  // YARNS_ENVELOPE_H_
