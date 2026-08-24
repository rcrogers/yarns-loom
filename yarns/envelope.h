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

// Envelope instances that can be live at once. Stated here because Envelope
// must not depend on its owners; multi.h folds the true maximum out of the
// layout map and asserts it EQUALS this.
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
// once per note, where the table is read. Exported because the harnesses
// report the duration every measurement is expressed against.
uint32_t ChiffAudibleSamples(uint32_t chiff_duration_increment_u32);

// The output is three independent terms:
//   nominal  the chiff-free envelope; its own slew at the STAGE's rate,
//            chasing the stage's adjusted target.
//   chiff    its own slew at the CHIFF's rate, chasing one of sixteen
//            levels spanning +/- the slew input, drawn per sample. Zero-mean.
//   bias     added at the point of use, outside both slews.
//
// out = saturate(mean + chiff), with mean = nominal + bias held two chiff
// amplitudes inside each DAC rail. value_without_bias() is nominal + chiff,
// carrying no bias.
class Envelope {
 public:
  Envelope() { }
  ~Envelope() { }

  void Init(int16_t zero_value_s16);
  void NoteOff();
  void NoteOn(
    ADSR& adsr,
    int32_t min_target_s16, int32_t max_target_s16,
    uint32_t chiff_amount_q30, uint32_t chiff_audible_samples
  );
  void Trigger(EnvelopeStage stage);
  void RenderSamples(int16_t* sample_buffer, int32_t bias_target_q31);
  void RenderStage(
    int16_t* sample_buffer, size_t block_samples_left,
    int32_t bias_q31, int32_t bias_slope_q31
  );
  // Same arg footprint as RenderStage, so the transition is a sibling call
  // with no per-transition frame.
  void HandOffToNextStage(
    int16_t* sample_buffer, size_t block_samples_left,
    int32_t bias_q31, int32_t bias_slope_q31
  );

  void Rescale(int32_t numerator, int32_t denominator);

  // Steps the bias directly, bypassing RenderSamples' per-block slew, so an
  // instantaneous jump (a pitch-driven timbre step at NoteOn) is not smoothed
  // into an audible glide. Continuous LFO motion still goes through the slew.
  inline void AdjustBias(int32_t delta_q31) { bias_q31_ += delta_q31; }

  inline int16_t tremolo(uint16_t strength_u16) const {
    int32_t relative_value_q15 =
      (value_without_bias_q30_
       - note_target_q30_[ENV_STAGE_RELEASE]) >> (30 - 15);
    return relative_value_q15 * -strength_u16 >> 16;
  }

  inline int16_t value_without_bias() const {
    return value_without_bias_q30_ >> (30 - 15);
  }
  inline EnvelopeStage stage() const { return stage_; }

 private:
  int32_t ChiffSlewInput_q30() const;

  ADSR* adsr_;

  int32_t note_target_q30_[ENV_NUM_STAGES];
  int32_t stage_target_q30_, value_without_bias_q30_;

  // No overshoot: the slope is pre-scaled by the block size.
  int32_t bias_q31_;

  EnvelopeStage stage_;

  // Nonzero for timed stages (attack/decay/release); doubles as the source
  // of the stage's nominal sample count. Zero for hold stages
  // (sustain/dead), which slew toward their target indefinitely.
  uint32_t stage_phase_increment_u32_;

  // Timed stages: samples remaining before handing off to the next stage.
  // The slew ends wherever it is at that point -- no snap to target; the
  // next stage's slew continues seamlessly from the current value.
  uint32_t stage_samples_left_;

  // 2^-(the stage's slew time), capped, Q31. Derived in Trigger, where the
  // stage moves; it costs an exp2 table interpolation.
  int32_t stage_slew_rate_q31_;

  // The current draw word and how many of its fields are unspent. The word IS
  // the xorshift state, and both carry across runs.
  uint32_t chiff_draws_;
  uint8_t chiff_draws_left_;

  // DURATION is a time-based modulation of AMOUNT: the whole decay is this one
  // quantity falling to zero.
  uint32_t chiff_amount_initial_q30_;
  uint32_t chiff_amount_q30_;
  uint32_t chiff_phase_q32_;
  uint32_t chiff_phase_step_q32_;

  uint32_t chiff_slew_time_log2_q5_27_;
  uint32_t chiff_slew_time_at_amount_zero_q5_27_;
  // Dimensionless, so Rescale leaves it alone.
  int32_t chiff_slew_input_fraction_q30_;
  // Half the note's ALLOWED range. A level, so it rescales with the others.
  int32_t chiff_slew_input_max_q30_;
  int32_t chiff_slew_state_q26_;

  // Where the current stage began. With the stage phase this anchors the
  // nominal value in closed form.
  int32_t stage_start_q30_;
  int32_t nominal_value_q30_;
  // min(note floor, 0). USAT bounds [0, 2^30), so a note reaching below zero is
  // rendered offset by its floor. State, not a local: GCC spills it there.
  int32_t value_floor_q30_;

  DISALLOW_COPY_AND_ASSIGN(Envelope);
};

}  // namespace yarns

#endif  // YARNS_ENVELOPE_H_
