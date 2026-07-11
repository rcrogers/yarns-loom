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

#include <algorithm>

#include "stmlib/stmlib.h"
#include "stmlib/utils/dsp.h"
#include "stmlib/dsp/dsp.h"

#include "yarns/drivers/dac.h"

namespace yarns {

using namespace stmlib;

// System-wide PRNG buffer shared by all envelopes' chiff draws. Filled once
// per audio block by FillSharedPrngBuffer(). Shared state keeps per-envelope
// PRNG state out of RAM. Double-length: each envelope reads a block-sized
// window starting at its own offset (assigned round-robin in Init), so no
// two envelopes consume the same word on the same sample -- decorrelation
// without the per-sample XOR that used to cost a register (and a spill)
// in the render loop.
namespace {
  uint32_t shared_prng_buffer[2 * kAudioBlockSize];
  uint32_t shared_prng_state = 0xCAFEBABE;
}  // namespace

// Number of slew time constants a timed stage spans, as log2 in Q5.27.
// log2(4) = 2: the stage hands off with e^-4 ~= 1.8% of its initial delta
// remaining (absorbed by the next stage's slew). Tunable by ear: larger
// front-loads the curve and lands closer to the target; smaller straightens
// the curve but leaves a bigger residual at handoff.
const uint32_t kStageTimeConstantsLog2_q5_27 = 2u << 27;

// Base shift is capped so that shift + dither <= 28: keeps `delta >> shift`
// well-defined, and 2^28 samples is already an absurdly long time constant.
const uint32_t kMaxSlewShift_q5_27 = 27u << 27;

// chiff_amount lives in [0, kChiffAmountMax].
const uint32_t kChiffAmountBits = 7;
const uint32_t kChiffAmountMax = (1u << kChiffAmountBits) - 1;

void Envelope::FillSharedPrngBuffer() {
  uint32_t state = shared_prng_state;
  for (size_t i = 0; i < 2 * kAudioBlockSize; ++i) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    shared_prng_buffer[i] = state;
  }
  shared_prng_state = state;
}

void Envelope::Init(int16_t zero_value_s16) {
  phase_increment_u32_ = 0;
  stage_samples_left_ = 0;
  stage_nominal_slew_shift_q5_27_ = 0;
  slew_alpha_q31_ = 0;
  slew_shift_q5_27_ = 0;
  slew_shift_increment_q5_27_ = 0;
  chiff_duration_samples_left_ = 0;
  chiff_stage_start_q30_ = 0;
  chiff_duty_phase_u32_ = 0;
  int32_t zero_value_q30 = zero_value_s16 << (31 - 16);
  value_q30_ = zero_value_q30;
  std::fill(
    &stage_target_q30_[0],
    &stage_target_q30_[ENV_NUM_STAGES],
    zero_value_q30
  );
  // Round-robin PRNG window offsets: distinct for up to kAudioBlockSize
  // envelope instances (we have ~12), so co-triggered envelopes never draw
  // the same random word on the same sample. TODO: examine whether there
  // is a more efficient architecture for supplying independent random
  // samples to the envelopes (buffer is 2x block size purely for these
  // offsets, and each envelope consumes a full word per sample).
  static uint32_t next_prng_offset = 0;
  prng_offset_u32_ = next_prng_offset++ & (kAudioBlockSize - 1);
  // Address-derived seed phase-offsets the error ripple across instances.
  slew_shift_error_accumulator_q0_32_ = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this));
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
      // Legato: ignore changes to peak target
      break;
    case ENV_STAGE_DECAY:
    case ENV_STAGE_SUSTAIN:
      // Legato: respect changes to sustain target, using decay to transition
      Trigger(ENV_STAGE_DECAY);
      break;
    case ENV_STAGE_RELEASE:
    case ENV_STAGE_DEAD:
    case ENV_NUM_STAGES: {
      // Fresh attack: arm chiff. Trigger first, so the slew shift starts
      // relative to the attack's nominal.
      Trigger(ENV_STAGE_ATTACK);
      // chiff_amount scales the shift drop below stage-nominal. At max the
      // shift starts at ~0: raw sample replacement -- a chiff sample jumps
      // all the way to its random target, flat-spectrum and maximally
      // aggressive. (Each integer shift above 0 halves the bandwidth.)
      // The amount->drop map is warped through lut_env_expo (1 - e^-4x) so
      // low amounts already drop the shift into the audible band, rather
      // than wasting the bottom of the knob in the inaudible near-nominal
      // zone. The kChiffAmountBits-wide amount indexes the LUT_ENV_EXPO
      // table whose usable span is a power of two (LUT_ENV_EXPO_SIZE - 1);
      // the compile-time ratio aligns the widths. amount 0 -> lut 0 ->
      // drop 0 (classic); amount max -> ~full drop.
      uint32_t amount_warp_u16 = lut_env_expo[
        chiff_amount * ((LUT_ENV_EXPO_SIZE - 1) >> kChiffAmountBits)];
      uint32_t drop_q5_27 = static_cast<uint32_t>(
        (static_cast<uint64_t>(stage_nominal_slew_shift_q5_27_) *
         amount_warp_u16) >> 16);
      // The chiff window keeps this timetable even if later stages cut in
      // early; Trigger re-slopes the increment toward each new nominal.
      chiff_duration_samples_left_ = drop_q5_27
        ? (adsr.attack_u32 ? UINT32_MAX / adsr.attack_u32 : 1)
        : 0;
      slew_shift_q5_27_ = stage_nominal_slew_shift_q5_27_ - drop_q5_27;
      ReSlopeSlewShift();
      break;
    }
  }
}

void Envelope::ReSlopeSlewShift() {
  uint32_t nominal = stage_nominal_slew_shift_q5_27_;
  // The shift may sit below nominal (chiff: faster, and the duty
  // compensates the mean so it still rides the dialed curve) but never
  // above it. A shift slower than the stage's own nominal only sluggishly
  // slews the envelope with no benefit -- that is what made an early
  // release on a long attack inherit the attack's (much longer) time
  // constant and crawl. Clamp so every stage slews at least at its
  // nominal rate. Safe from pops: shift > nominal means drop < 0, where
  // the chiff mixing is already off (the drop > 0 guard in RenderStage),
  // so this changes only the slew speed, not the chiff amplitude.
  if (slew_shift_q5_27_ > nominal) slew_shift_q5_27_ = nominal;
  if (chiff_duration_samples_left_) {
    // Ramp the (now <= nominal) shift up to nominal over the remaining
    // window; truncation toward zero keeps it from crossing nominal.
    slew_shift_increment_q5_27_ =
      (nominal - slew_shift_q5_27_) / chiff_duration_samples_left_;
  } else {
    slew_shift_q5_27_ = nominal;
    slew_shift_increment_q5_27_ = 0;
  }
}

// Update current stage and its state. The slew always moves from the current
// value toward the stage target at a rate set by the stage's nominal
// duration, so there is no nominal-vs-actual delta bookkeeping: starting
// closer to the target just means arriving (proportionally) closer to it
// when the stage's sample countdown expires.
void Envelope::Trigger(EnvelopeStage stage) {
  // Anchor the new stage's chiff start on where the leaving stage's mean
  // actually was -- its duty line, start + (target - start) * duty --
  // rather than the instantaneous value_q30_. At high chiff the value
  // telegraphs between start and target, so the raw value hands the next
  // stage a random starting energy (most audible as wildly varying
  // early-release levels). Only when chiff is live is the value noisy;
  // with chiff off the value is the exact classic slew, so use it
  // directly (also spares the multiply on the common path).
  int32_t stage_start_q30 = value_q30_;
  if (chiff_duration_samples_left_) {
    uint32_t duty_u16 = Interpolate824(lut_env_expo, chiff_duty_phase_u32_);
    stage_start_q30 = chiff_stage_start_q30_ + static_cast<int32_t>(
      (static_cast<int64_t>(target_q30_ - chiff_stage_start_q30_) * duty_u16)
      >> 16);
  }
  stage_ = stage;
  target_q30_ = stage_target_q30_[stage]; // Cache against new NoteOn
  chiff_stage_start_q30_ = stage_start_q30;
  switch (stage) {
    case ENV_STAGE_ATTACK : phase_increment_u32_ = adsr_->attack_u32  ; break;
    case ENV_STAGE_DECAY  : phase_increment_u32_ = adsr_->decay_u32   ; break;
    case ENV_STAGE_RELEASE: phase_increment_u32_ = adsr_->release_u32 ; break;
    default:
      // Hold stage: no countdown; keep slewing toward the target with the
      // shift inherited from the previous stage, converging asymptotically.
      // Duty pinned to all-target: a hold has nowhere to have started from.
      phase_increment_u32_ = 0;
      chiff_duty_phase_u32_ = UINT32_MAX;
      return;
  }

  if (value_q30_ == target_q30_) {
    // Nothing to do this stage; skip ahead
    return Trigger(static_cast<EnvelopeStage>(stage + 1));
  }

  if (!phase_increment_u32_) {
    // Degenerate zero increment: treat as a hold (also guards the division)
    chiff_duty_phase_u32_ = UINT32_MAX;
    return;
  }

  // Nominal stage duration in samples
  stage_samples_left_ = UINT32_MAX / phase_increment_u32_;

  // Arm the duty phase at 0; it advances toward full over the stage at
  // segment rate in RenderStage (phase_increment_u32_ per sample -- the
  // same increment that spans the stage), reading the duty curve from
  // lut_env_expo. At phase 0 the duty is 0 (all-start), so the envelope
  // begins where the stage began.
  chiff_duty_phase_u32_ = 0;

  // Slew shift from stage duration: with N = 2^32 / increment samples and
  // k = 2^kStageTimeConstantsLog2 time constants per stage, the time
  // constant 2^shift = N / k, i.e. shift = log2(N) - log2(k).
  // log2(N) = 32 - log2(increment); log2(increment) is approximated as
  // (31 - clz) plus a linear mantissa fraction (max error ~0.09, i.e. ~6%
  // of the time constant -- inaudible, and monotone in the increment).
  uint8_t leading_zeros = __builtin_clz(phase_increment_u32_);
  if (leading_zeros >= 30) {
    // Increment <= 3: N >= ~2^30.5, whose shift saturates the cap anyway.
    // Computed separately because (leading_zeros + 1) << 27 would overflow.
    stage_nominal_slew_shift_q5_27_ = kMaxSlewShift_q5_27;
  } else {
    uint32_t mantissa_frac_q5_27 =
        ((phase_increment_u32_ << leading_zeros) & 0x7FFFFFFFu) >> 4;
    uint32_t log2_stage_samples_q5_27 =
        (static_cast<uint32_t>(leading_zeros + 1) << 27) - mantissa_frac_q5_27;
    stage_nominal_slew_shift_q5_27_ = log2_stage_samples_q5_27 <= kStageTimeConstantsLog2_q5_27
      ? 0 // Stage too short for a meaningful slew; jump straight to target
      : std::min(
          log2_stage_samples_q5_27 - kStageTimeConstantsLog2_q5_27,
          kMaxSlewShift_q5_27
        );
  }
  // Exact classic-slew coefficient alpha = 2^-(nominal) for the chiff-
  // inactive loop: 2^-fraction (lut_expo2_neg, u16) promoted to Q31, then
  // downshifted by the integer part. Same LUT and Q5.27 split the chiff beta
  // uses. At fraction 0 this is ~0x7FFF8000 (one LSB under 2^31 == 1.0).
  uint32_t nominal_int = stage_nominal_slew_shift_q5_27_ >> 27;
  uint32_t two_pow_neg_fraction_u16 = Interpolate824(
    lut_expo2_neg, (stage_nominal_slew_shift_q5_27_ & 0x07FFFFFFu) << 5);
  slew_alpha_q31_ = (two_pow_neg_fraction_u16 << 15) >> nominal_int;

  // The nominal may have changed; keep the chiff ramp's original timetable,
  // re-aimed at this stage's nominal.
  ReSlopeSlewShift();
}

void Envelope::RenderSamples(int16_t* sample_buffer, int32_t bias_target_q31) {
  // Bias is unaffected by stage change, thus has distinct lifecycle from other locals
  const int32_t bias_slope_q31 = ((bias_target_q31 >> 1) - (bias_q31_ >> 1)) >> (kAudioBlockSizeBits - 1);
  RenderStage(sample_buffer, kAudioBlockSize, bias_q31_, bias_slope_q31);
}

// Mix the envelope value (Q30) and the already-advanced bias (Q31) into a
// 0..INT16_MAX output sample. ClipU16(x) >> 1 equals ClipUShifted(x, 15, 1) at
// both saturation boundaries, but folds the final right-shift into the USAT.
static inline int16_t EnvelopeSample(int32_t value_q30, int32_t bias_q31) {
  int32_t sum_s16 = (value_q30 >> (30 - 16)) + (bias_q31 >> (31 - 16));
  return ClipUShifted(sum_s16, 15, 1);
}

// Advance to the next stage and resume rendering this block's remaining
// samples there. The caller saves value_q30_ first (so the re-entrant Trigger
// sees the real start value). Even with no samples left, the re-entry saves
// bias state for us.
void Envelope::HandOffToNextStage(
  int16_t* sample_buffer, size_t block_samples_left,
  int32_t bias_q31, int32_t bias_slope_q31
) {
  Trigger(static_cast<EnvelopeStage>(stage_ + 1));
  RenderStage(sample_buffer, block_samples_left, bias_q31, bias_slope_q31);
}

void Envelope::RenderStage(
  int16_t* sample_buffer, size_t block_samples_left,
  int32_t bias_q31, int32_t bias_slope_q31
) {
  int32_t value_q30 = value_q30_;
  const int32_t stage_target_q30 = target_q30_;

  // The slew shift ramps toward stage-nominal while chiff runs (and equals
  // it otherwise). Its Q5.27 fraction is applied by sigma-delta: the error
  // accumulator's carry selects shift + 1, interpolating time constants
  // between powers of two.
  uint32_t slew_shift_q5_27 = slew_shift_q5_27_;
  uint32_t slew_shift_error_accumulator_q0_32 = slew_shift_error_accumulator_q0_32_;
  const int32_t chiff_stage_start_q30 = chiff_stage_start_q30_;

  // Buffer position (plus this instance's decorrelation offset) doubles as
  // the index into the shared PRNG block.
  const uint32_t* prng = &shared_prng_buffer[
    prng_offset_u32_ + (kAudioBlockSize - block_samples_left)];

  const bool timed = phase_increment_u32_ != 0;
  uint32_t stage_samples_left = timed ? stage_samples_left_ : UINT32_MAX;

  // Segmented by the two countdowns (stage, chiff duration); every segment
  // runs the same loop body -- no lean variant, the worst case is the only
  // case that matters. Chiff-inactive segments carry a zero shift
  // increment and an all-target duty (a 16-bit draw is always < 2^16), so
  // they render the classic exact envelope.
  while (block_samples_left) {
    const bool chiff_active = chiff_duration_samples_left_ != 0;
    const uint32_t slew_shift_increment_q5_27 =
      chiff_active ? slew_shift_increment_q5_27_ : 0u;
    // P(stage target) for this segment, as a u17 so 2^16 always beats a
    // 16-bit draw (all-target: classic envelope). While chiff runs it is
    // the exponential duty curve (lut_env_expo over stage progress)
    // whose mixing depth is crossfaded against the residual slew lag by
    // beta = 1 - 2^-(nominal - ramp): the chiff drains to nothing as the
    // shift ramp reaches nominal, and amount 0 (zero drop) stays exactly
    // classic. Evaluated once per segment; segments are stage- and
    // window-bounded, so sub-block stages resolve correctly.
    uint32_t chiff_duty_u17 = 1u << 16;
    if (chiff_active) {
      uint32_t duty_u16 = Interpolate824(lut_env_expo, chiff_duty_phase_u32_);
      // ramp <= nominal always (armed <= nominal, rises toward it), so the
      // drop is a non-negative shift magnitude.
      uint32_t drop_q5_27 = stage_nominal_slew_shift_q5_27_ - slew_shift_q5_27;
      if (drop_q5_27) {
        uint32_t drop_int = drop_q5_27 >> 27;
        uint32_t two_pow_neg_drop_u16 = drop_int >= 16
          ? 0
          : Interpolate824(lut_expo2_neg,
              (drop_q5_27 & 0x07FFFFFFu) << 5) >> drop_int;
        uint32_t beta_u16 = (1u << 16) - two_pow_neg_drop_u16;
        uint32_t effective_gap_u16 =
          ((65535u - duty_u16) * beta_u16) >> 16;
        chiff_duty_u17 = (1u << 16) - effective_gap_u16;
      }
    }
    uint32_t run_samples = std::min<uint32_t>(
      block_samples_left,
      std::min<uint32_t>(
        stage_samples_left,
        chiff_active ? chiff_duration_samples_left_ : UINT32_MAX
      )
    );
    block_samples_left -= run_samples;
    stage_samples_left -= run_samples;
    if (chiff_active) chiff_duration_samples_left_ -= run_samples;

    // Pin the loop invariants into registers. With -fno-move-loop-
    // invariants, GCC 4.8 otherwise reloads them from stack slots every
    // sample (see the blackbox-hoist idiom elsewhere in this codebase).
    uint32_t pinned_duty_u17 = chiff_duty_u17;
    int32_t pinned_stage_start_q30 = chiff_stage_start_q30;
    int32_t pinned_stage_target_q30 = stage_target_q30;
    int32_t pinned_bias_slope_q31 = bias_slope_q31;
    __asm__ volatile ("" : "+r"(pinned_duty_u17),
                           "+r"(pinned_stage_start_q30),
                           "+r"(pinned_stage_target_q30),
                           "+r"(pinned_bias_slope_q31));

    // End-pointer termination: folds the loop test into the buffer
    // pointer instead of a separate countdown register.
    int16_t* const segment_end = sample_buffer + run_samples;
    if (chiff_active) {
      while (sample_buffer != segment_end) {
        // PRNG budget: bits 0-15 are the duty draw; bits 16-31 are spare
        // (reserved for the extremes dial).
        uint32_t chiff_draw_u32 = *prng++;
        slew_shift_q5_27 += slew_shift_increment_q5_27;
        uint32_t shift_fraction_u32 = static_cast<uint32_t>(slew_shift_q5_27) << 5;
        // Sigma-delta the shift fraction: the carry out of the phase
        // accumulation selects shift + 1. ADDS/ADC keeps the carry in the
        // flags; GCC 4.8 would otherwise spend an ITE pair reifying it.
        uint32_t shift;
        __asm__ (
            "adds %1, %1, %2\n\t"
            "lsr %0, %3, #27\n\t"       // flag-preserving (no S suffix)
            "adc %0, %0, #0"
            : "=&r"(shift), "+&r"(slew_shift_error_accumulator_q0_32)
            : "r"(shift_fraction_u32), "r"(static_cast<uint32_t>(slew_shift_q5_27))
            : "cc");
        // Duty-weighted target: the stage target if the draw lands under
        // the duty, else the stage's start value, via a branchless sign-mask
        // blend. The u17 duty means an inactive segment (duty = 2^16) beats
        // every 16-bit draw: pure stage-target slewing. Sign arithmetic is
        // safe: both operands fit 17 bits, so the difference fits int32 and
        // its sign bit is the comparison result.
        int32_t target_select_mask = (
          static_cast<int32_t>(chiff_draw_u32 & 0xFFFF)
          - static_cast<int32_t>(pinned_duty_u17)
        ) >> 31;  // all-ones iff draw < duty: pick the stage target
        int32_t target_q30 = pinned_stage_start_q30
          ^ ((pinned_stage_start_q30 ^ pinned_stage_target_q30)
             & target_select_mask);
        // Never overshoots: |delta >> shift| <= |delta|. Truncation stalls
        // an upward slew once delta < 2^shift, but timed stages end by
        // countdown, and hold stages are content to sit near their target.
        value_q30 += (target_q30 - value_q30) >> shift;
        bias_q31 += pinned_bias_slope_q31;
        *sample_buffer++ = EnvelopeSample(value_q30, bias_q31);
      }
    } else {
      // Chiff-inactive: the classic envelope, and the only place the slew
      // ripple is exposed. Slew by the exact fractional rate (multiply by
      // alpha = 2^-nominal, constant for the stage) instead of the sigma-
      // delta-dithered integer shift, so no periodic ripple rides the moving
      // value for a nonlinear CV destination to amplify. No PRNG or duty
      // draw: the target is always the stage target. alpha in Q31, so the
      // Q30 delta * alpha lands back in Q30 after >> 31; the 64-bit product
      // holds |delta| (< 2^31) * alpha (<= 2^31) without overflow.
      const int32_t alpha_q31 = slew_alpha_q31_;
      while (sample_buffer != segment_end) {
        value_q30 += static_cast<int32_t>(
          (static_cast<int64_t>(pinned_stage_target_q30 - value_q30) *
           alpha_q31) >> 31);
        bias_q31 += pinned_bias_slope_q31;
        *sample_buffer++ = EnvelopeSample(value_q30, bias_q31);
      }
    }

    // Advance the duty phase by the samples just rendered (segment rate).
    // phase_increment_u32_ spans the stage, so phase reaches full near
    // stage end; saturate there. A following stage transition resets it.
    if (chiff_active) {
      uint64_t advanced_phase =
        static_cast<uint64_t>(chiff_duty_phase_u32_) +
        static_cast<uint64_t>(phase_increment_u32_) * run_samples;
      chiff_duty_phase_u32_ = advanced_phase > UINT32_MAX
        ? UINT32_MAX : static_cast<uint32_t>(advanced_phase);
    }

    if (chiff_active && chiff_duration_samples_left_ == 0) {
      // Window closed: land on stage nominal. The step is bounded by the
      // re-slope division's truncation residual (under one integer shift)
      // and occurs at chiff's minimum intensity.
      slew_shift_q5_27 = stage_nominal_slew_shift_q5_27_;
    }

    if (timed && stage_samples_left == 0) {
      // Countdown expired: hand off to the next stage from wherever the
      // slew got to. Save state first -- the re-entrant Trigger re-slopes
      // the chiff ramp from it. Tail call keeps the transition flat.
      value_q30_ = value_q30;
      bias_q31_ = bias_q31;
      slew_shift_error_accumulator_q0_32_ = slew_shift_error_accumulator_q0_32;
      slew_shift_q5_27_ = slew_shift_q5_27;
      stage_samples_left_ = 0;
      return HandOffToNextStage(
        sample_buffer, block_samples_left, bias_q31, bias_slope_q31);
    }
  }

  value_q30_ = value_q30;
  bias_q31_ = bias_q31;
  slew_shift_error_accumulator_q0_32_ = slew_shift_error_accumulator_q0_32;
  slew_shift_q5_27_ = slew_shift_q5_27;
  if (timed) stage_samples_left_ = stage_samples_left;
}

// Exact unsigned division of a 64-bit dividend (hi:lo) by a 32-bit divisor,
// valid when the quotient fits 32 bits (hi < divisor; the caller saturates
// otherwise). 32-bit hardware ops only -- no 64-bit divide library, no
// soft-float. Hacker's Delight "divlu" (Knuth Algorithm D, base 2^16): the
// num_hi*b and q*divisor intermediates overflow 32 bits but cancel under
// modular wraparound, and the two correction loops each run at most twice.
static uint32_t DivU64ByU32(uint32_t hi, uint32_t lo, uint32_t divisor) {
  const uint32_t b = 1u << 16;
  int shift = __builtin_clz(divisor);
  divisor <<= shift;
  uint32_t divisor_hi = divisor >> 16;
  uint32_t divisor_lo = divisor & 0xFFFF;
  uint32_t num_hi = (hi << shift) | (shift == 0 ? 0 : (lo >> (32 - shift)));
  uint32_t num_lo = lo << shift;
  uint32_t num_lo_hi = num_lo >> 16;
  uint32_t num_lo_lo = num_lo & 0xFFFF;

  uint32_t q1 = num_hi / divisor_hi;
  uint32_t rhat = num_hi - q1 * divisor_hi;
  while (q1 >= b || q1 * divisor_lo > b * rhat + num_lo_hi) {
    --q1;
    rhat += divisor_hi;
    if (rhat >= b) break;
  }

  uint32_t num_mid = num_hi * b + num_lo_hi - q1 * divisor;
  uint32_t q0 = num_mid / divisor_hi;
  rhat = num_mid - q0 * divisor_hi;
  while (q0 >= b || q0 * divisor_lo > b * rhat + num_lo_lo) {
    --q0;
    rhat += divisor_hi;
    if (rhat >= b) break;
  }
  return q1 * b + q0;
}

// Scale value by numerator/denominator, exact and in 32-bit hardware ops only.
// A hardware umull forms the 64-bit product, which DivU64ByU32 divides; the
// result is saturated into the signed 32-bit range. Unlike a fixed-point
// factor, this keeps full precision even for extreme scale ratios (where a
// small numerator or denominator would collapse a Q15 factor to a few bits).
static int32_t ScaleRatio(int32_t value, uint32_t numerator, uint32_t denominator) {
  uint32_t magnitude = value < 0
      ? 0u - static_cast<uint32_t>(value)
      : static_cast<uint32_t>(value);
  uint32_t product_hi = MulU32(magnitude, numerator);
  uint32_t product_lo = magnitude * numerator;
  uint32_t quotient = product_hi >= denominator
      ? UINT32_MAX
      : DivU64ByU32(product_hi, product_lo, denominator);
  if (quotient > static_cast<uint32_t>(INT32_MAX)) quotient = INT32_MAX;
  return value < 0 ? -static_cast<int32_t>(quotient)
                   : static_cast<int32_t>(quotient);
}

// Rescale all envelope levels by numerator/denominator (both non-negative,
// from WarpTimbre). Cold path (shape change), so exact per-field division is
// fine. The slew shift is a pure rate and thus scale-invariant -- no
// adjustment needed.
void Envelope::Rescale(int32_t numerator, int32_t denominator) {
  if (denominator <= 0) return; // Degenerate scale; leave levels unchanged
  uint32_t num = static_cast<uint32_t>(numerator);
  uint32_t den = static_cast<uint32_t>(denominator);
  bias_q31_ = ScaleRatio(bias_q31_, num, den);
  value_q30_ = ScaleRatio(value_q30_, num, den);
  target_q30_ = ScaleRatio(target_q30_, num, den);
  chiff_stage_start_q30_ = ScaleRatio(chiff_stage_start_q30_, num, den);
  for (int i = 0; i < ENV_NUM_STAGES; ++i) {
    stage_target_q30_[i] = ScaleRatio(stage_target_q30_[i], num, den);
  }
}

}  // namespace yarns
