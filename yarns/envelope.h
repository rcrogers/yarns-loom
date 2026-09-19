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

// THE RANGE EVERY SAMPLE THE RENDER WRITES FALLS IN, and the reason a consumer
// may read one as UNSIGNED. Both stores saturate to this width off this same
// constant -- the C one and the asm's USAT, which takes it as an immediate.
// Stated in the header because the consumers are not in envelope.cc: timbre
// and gain travel as int16_t only because the buffer is, never because a
// negative one means anything, and the shapes that shift one, cast it to
// uint32_t, or index a table with it would each break differently without this.
const int kEnvelopeSampleBits = 15;
const int16_t kEnvelopeSampleMax = (1 << kEnvelopeSampleBits) - 1;

// Bits per chiff draw, so sixteen levels. A two-level input's output is a
// square once the rate reaches 1, so "unslewed" and "overdriven" collide and
// the drive has nothing to shape; sixteen makes the unslewed end midpoint
// noise, whose rms is the fraction the drive reclaims.
const uint32_t kChiffDrawBits = 4;

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

// The reciprocal of a CHIFF DURATION increment: the audible duration in
// samples.
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
  // ceiling_s16 is the HIGHEST VALUE THE CALLER MAY SPEND, which is not the
  // same as the note's peak: a chiff rides ABOVE the note and must still fit
  // under it. Callers with no tighter bound than this type's own output range
  // pass kEnvelopeSampleMax.
  void NoteOn(
    ADSR& adsr,
    int32_t min_target_s16, int32_t max_target_s16, int32_t ceiling_s16,
    uint32_t chiff_amount_q30, uint32_t chiff_audible_samples
  );
  void Trigger(EnvelopeStage stage);
  // Every sample written is in [0, kEnvelopeSampleMax].
  void RenderSamples(int16_t* sample_buffer, int32_t bias_target_q31);
  // EVERYTHING THE CHIFF CONTRIBUTES TO ONE BLOCK, derived once.
  //
  // NONE OF IT IS STAGE-DEPENDENT. The chiff's schedule is an absolute time off
  // its own table (`028d10d9`), so a stage boundary landing mid-block is no
  // reason to rebuild any of this -- and L14b says stage timing may not drive
  // chiff timing. Derived per RUN, as it was, an attack that expires inside a
  // block rebuilt the drive, the input fraction, the amplitude gain, the clip
  // and all sixteen levels three times over, because the STAGE happened to end.
  //
  // The one thing that genuinely wanted the run boundary was the slew rate's
  // tread, and it does not any more: the render refines the rate every sample,
  // so it crosses a run boundary without noticing one.
  struct ChiffBlock {
    // The only member that moves: the render advances it a sample at a time and
    // carries it from one run to the next.
    int32_t slew_rate_q31;
    uint32_t rate_retained_per_sample_q31;
    uint32_t slew_time_step_q5_27;
    int32_t clip_threshold_q26;
    int32_t mean_min_q30;
    int32_t mean_max_q30;
    int32_t levels_q4_26[1 << kChiffDrawBits];
  };

  void RenderStage(
    int16_t* sample_buffer, size_t block_samples_left,
    int32_t bias_q31, int32_t bias_slope_q31, ChiffBlock* chiff
  );
  // Same arg footprint as RenderStage, so the transition is a sibling call
  // with no per-transition frame.
  void HandOffToNextStage(
    int16_t* sample_buffer, size_t block_samples_left,
    int32_t bias_q31, int32_t bias_slope_q31, ChiffBlock* chiff
  );

  void Rescale(int32_t numerator, int32_t denominator);

  // What one run does to the chiff's decay, and what the render loop needs
  // back from it.
  struct ChiffRunDecay {
    uint32_t slew_time_step_q5_27;   // per sample
    int32_t drive_q4_26;             // at the amount the run STARTS from
  };


  // Steps the bias directly, bypassing RenderSamples' per-block slew, so an
  // instantaneous jump (a pitch-driven timbre step at NoteOn) is not smoothed
  // into an audible glide. Continuous LFO motion still goes through the slew.
  inline void AdjustBias(int32_t delta_q31) {
    // Saturating: the bias spans the whole int32 range, so a step onto one
    // already near a rail wraps, and the wrap is undefined. In range this is a
    // plain add.
    const int64_t stepped =
      static_cast<int64_t>(bias_q31_) + static_cast<int64_t>(delta_q31);
    bias_q31_ = stepped > INT32_MAX ? INT32_MAX
              : stepped < INT32_MIN ? INT32_MIN
              : static_cast<int32_t>(stepped);
  }

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
  inline ChiffRunDecay AdvanceChiffDecay(uint32_t run_samples);
  void AdvanceChiffForBlock(uint32_t block_samples, ChiffBlock* chiff);


  ADSR* adsr_;

  int32_t note_target_q30_[ENV_NUM_STAGES];
  int32_t stage_target_q30_, value_without_bias_q30_;

  // No overshoot: the slope is pre-scaled by the block size.
  // Not cleared by a note, so bias motion stays smooth across one.
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
  // What the caller said it may spend, in the targets' domain. NOT inferred
  // from the targets: a caller's range need not start at its floor, a drone's
  // is empty, and a CV output's runs downward because DAC codes fall as volts
  // rise. Only the caller knows.
  int32_t value_ceiling_q30_;

  DISALLOW_COPY_AND_ASSIGN(Envelope);
};

}  // namespace yarns

#endif  // YARNS_ENVELOPE_H_
