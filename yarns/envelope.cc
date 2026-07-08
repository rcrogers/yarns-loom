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

// At full chiff amount, stages start at this downshift (time constant of
// 4 samples): fast enough to nearly track the per-sample random targets,
// i.e. maximum noise. Tunable.
const uint32_t kChiffFastestShift_q5_27 = 2u << 27;

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
  phase_samples_left_ = 0;
  slew_shift_q5_27_ = 0;
  chiff_shift_ramp_q5_27_ = 0;
  chiff_ramp_increment_q5_27_ = 0;
  chiff_samples_left_ = 0;
  chiff_gate_u16_ = 0;
  chiff_floor_q30_ = 0;
  chiff_span_q14_ = 0;
  int32_t zero_value_q30 = zero_value_s16 << (31 - 16);
  value_q30_ = zero_value_q30;
  std::fill(
    &stage_target_q30_[0],
    &stage_target_q30_[ENV_NUM_STAGES],
    zero_value_q30
  );
  // Round-robin PRNG window offsets: distinct for up to kAudioBlockSize
  // envelope instances (we have ~12), so co-triggered envelopes never draw
  // the same random word on the same sample.
  static uint32_t next_prng_offset = 0;
  prng_offset_u32_ = next_prng_offset++ & (kAudioBlockSize - 1);
  // Address-derived dither seed: phase-offsets the sigma-delta ripple
  // pattern across instances.
  dither_phase_u32_ = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this));
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
      // Fresh attack: arm chiff. Trigger first, so the ramp starts relative
      // to the attack's nominal slew shift.
      Trigger(ENV_STAGE_ATTACK);
      uint32_t full_drop_q5_27 = slew_shift_q5_27_ > kChiffFastestShift_q5_27
        ? slew_shift_q5_27_ - kChiffFastestShift_q5_27
        : 0;
      // chiff_amount in [0, 127] scales the drop below stage-nominal
      uint32_t drop_q5_27 = (full_drop_q5_27 >> 7) * chiff_amount;
      // The chiff window keeps this timetable even if later stages cut in
      // early; Trigger re-slopes the increment toward each new nominal.
      chiff_samples_left_ = drop_q5_27
        ? (adsr.attack_u32 ? UINT32_MAX / adsr.attack_u32 : 1)
        : 0;
      chiff_shift_ramp_q5_27_ =
        static_cast<int32_t>(slew_shift_q5_27_ - drop_q5_27);
      ReSlopeChiffRamp();
      chiff_gate_u16_ = static_cast<uint32_t>(chiff_amount) << 9;
      chiff_floor_q30_ = stage_target_q30_[ENV_STAGE_DEAD];
      chiff_span_q14_ =
        (stage_target_q30_[ENV_STAGE_ATTACK] - chiff_floor_q30_) >> 16;
      break;
    }
  }
}

void Envelope::ReSlopeChiffRamp() {
  if (chiff_samples_left_) {
    // Signed: the ramp may sit above or below the new nominal (early
    // release from a slow attack vs a longer next stage). Truncation
    // toward zero means the ramp never crosses the nominal; the residual
    // at expiry is under one integer shift, landed at minimum intensity.
    chiff_ramp_increment_q5_27_ =
      (static_cast<int32_t>(slew_shift_q5_27_) - chiff_shift_ramp_q5_27_)
      / static_cast<int32_t>(chiff_samples_left_);
  } else {
    chiff_shift_ramp_q5_27_ = static_cast<int32_t>(slew_shift_q5_27_);
    chiff_ramp_increment_q5_27_ = 0;
  }
}

// Update current stage and its state. The slew always moves from the current
// value toward the stage target at a rate set by the stage's nominal
// duration, so there is no nominal-vs-actual delta bookkeeping: starting
// closer to the target just means arriving (proportionally) closer to it
// when the stage's sample countdown expires.
void Envelope::Trigger(EnvelopeStage stage) {
  stage_ = stage;
  target_q30_ = stage_target_q30_[stage]; // Cache against new NoteOn
  switch (stage) {
    case ENV_STAGE_ATTACK : phase_increment_u32_ = adsr_->attack_u32  ; break;
    case ENV_STAGE_DECAY  : phase_increment_u32_ = adsr_->decay_u32   ; break;
    case ENV_STAGE_RELEASE: phase_increment_u32_ = adsr_->release_u32 ; break;
    default:
      // Hold stage: no countdown; keep slewing toward the target with the
      // shift inherited from the previous stage, converging asymptotically.
      phase_increment_u32_ = 0;
      return;
  }

  if (value_q30_ == target_q30_) {
    // Nothing to do this stage; skip ahead
    return Trigger(static_cast<EnvelopeStage>(stage + 1));
  }

  if (!phase_increment_u32_) {
    // Degenerate zero increment: treat as a hold (also guards the division)
    return;
  }

  // Nominal stage duration in samples
  phase_samples_left_ = UINT32_MAX / phase_increment_u32_;

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
    slew_shift_q5_27_ = kMaxSlewShift_q5_27;
  } else {
    uint32_t mantissa_frac_q5_27 =
        ((phase_increment_u32_ << leading_zeros) & 0x7FFFFFFFu) >> 4;
    uint32_t log2_stage_samples_q5_27 =
        (static_cast<uint32_t>(leading_zeros + 1) << 27) - mantissa_frac_q5_27;
    slew_shift_q5_27_ = log2_stage_samples_q5_27 <= kStageTimeConstantsLog2_q5_27
      ? 0 // Stage too short for a meaningful slew; jump straight to target
      : std::min(
          log2_stage_samples_q5_27 - kStageTimeConstantsLog2_q5_27,
          kMaxSlewShift_q5_27
        );
  }
  // The nominal may have changed; keep the chiff ramp's original timetable,
  // re-aimed at this stage's nominal.
  ReSlopeChiffRamp();
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

  // The slew shift follows the chiff ramp (== stage nominal once the chiff
  // window has closed). Its Q5.27 fraction is dithered by a sigma-delta
  // accumulation (as Q32) whose carry selects shift + 1, interpolating
  // time constants between powers of two.
  int32_t ramp_q5_27 = chiff_shift_ramp_q5_27_;
  uint32_t dither_phase_u32 = dither_phase_u32_;
  const int32_t chiff_floor_q30 = chiff_floor_q30_;
  const int32_t chiff_span_q14 = chiff_span_q14_;

  // Buffer position (plus this instance's decorrelation offset) doubles as
  // the index into the shared PRNG block.
  const uint32_t* prng = &shared_prng_buffer[
    prng_offset_u32_ + (kAudioBlockSize - block_samples_left)];

  const bool timed = phase_increment_u32_ != 0;
  uint32_t stage_samples_left = timed ? phase_samples_left_ : UINT32_MAX;

  // Segmented by the two countdowns (stage, chiff window); every segment
  // runs the same loop body -- no lean variant, the worst case is the only
  // case that matters. Chiff-inactive segments just carry a zero ramp
  // increment and a zero gate (a 16-bit draw is never < 0).
  while (block_samples_left) {
    const bool chiff = chiff_samples_left_ != 0;
    const int32_t ramp_increment_q5_27 = chiff ? chiff_ramp_increment_q5_27_ : 0;
    const uint32_t chiff_gate_u16 = chiff ? chiff_gate_u16_ : 0;
    uint32_t run_samples = std::min<uint32_t>(
      block_samples_left,
      std::min<uint32_t>(
        stage_samples_left, chiff ? chiff_samples_left_ : UINT32_MAX
      )
    );
    block_samples_left -= run_samples;
    stage_samples_left -= run_samples;
    if (chiff) chiff_samples_left_ -= run_samples;

    // Pin the loop invariants into registers. With -fno-move-loop-
    // invariants, GCC 4.8 otherwise reloads them from stack slots every
    // sample (see the blackbox-hoist idiom elsewhere in this codebase).
    uint32_t pinned_gate_u16 = chiff_gate_u16;
    int32_t pinned_floor_q30 = chiff_floor_q30;
    int32_t pinned_span_q14 = chiff_span_q14;
    int32_t pinned_stage_target_q30 = stage_target_q30;
    int32_t pinned_bias_slope_q31 = bias_slope_q31;
    __asm__ volatile ("" : "+r"(pinned_gate_u16), "+r"(pinned_floor_q30),
                           "+r"(pinned_span_q14),
                           "+r"(pinned_stage_target_q30),
                           "+r"(pinned_bias_slope_q31));

    // End-pointer termination: folds the loop test into the buffer
    // pointer instead of a separate countdown register.
    int16_t* const segment_end = sample_buffer + run_samples;
    while (sample_buffer != segment_end) {
      // PRNG budget: 16 bits per consumer -- bits 16-31 random-target
      // gate, bits 0-15 random target value.
      uint32_t random = *prng++;
      ramp_q5_27 += ramp_increment_q5_27;
      uint32_t frac_u32 = static_cast<uint32_t>(ramp_q5_27) << 5;
      // Sigma-delta the shift fraction: the carry out of the phase
      // accumulation selects shift + 1. ADDS/ADC keeps the carry in the
      // flags; GCC 4.8 would otherwise spend an ITE pair reifying it.
      uint32_t shift;
      __asm__ (
          "adds %1, %1, %2\n\t"
          "lsr %0, %3, #27\n\t"       // flag-preserving (no S suffix)
          "adc %0, %0, #0"
          : "=&r"(shift), "+&r"(dither_phase_u32)
          : "r"(frac_u32), "r"(static_cast<uint32_t>(ramp_q5_27))
          : "cc");
      // Branchless target select: compute the random target
      // unconditionally (mla is 2 cycles on M3), then blend via an
      // arithmetic mask -- all-ones iff the 16-bit gate draw fires. Sign
      // arithmetic is safe: both operands are < 2^16, so the difference
      // fits int32 and its sign bit is the comparison result.
      int32_t random_target_q30 = pinned_floor_q30
        + pinned_span_q14 * static_cast<int32_t>(random & 0xFFFF);
      int32_t gate_mask = (
        static_cast<int32_t>(random >> 16) - static_cast<int32_t>(pinned_gate_u16)
      ) >> 31;
      int32_t target_q30 = pinned_stage_target_q30
        ^ ((pinned_stage_target_q30 ^ random_target_q30) & gate_mask);
      // Never overshoots: |delta >> shift| <= |delta|. Truncation stalls
      // an upward slew once delta < 2^shift, but timed stages end by
      // countdown, and hold stages are content to sit near their target.
      value_q30 += (target_q30 - value_q30) >> shift;
      bias_q31 += pinned_bias_slope_q31;
      *sample_buffer++ = EnvelopeSample(value_q30, bias_q31);
    }

    if (chiff && chiff_samples_left_ == 0) {
      // Window closed: land on stage nominal. The step is bounded by the
      // re-slope division's truncation residual (under one integer shift)
      // and occurs at chiff's minimum intensity.
      ramp_q5_27 = static_cast<int32_t>(slew_shift_q5_27_);
    }

    if (timed && stage_samples_left == 0) {
      // Countdown expired: hand off to the next stage from wherever the
      // slew got to. Save state first -- the re-entrant Trigger re-slopes
      // the chiff ramp from it. Tail call keeps the transition flat.
      value_q30_ = value_q30;
      bias_q31_ = bias_q31;
      dither_phase_u32_ = dither_phase_u32;
      chiff_shift_ramp_q5_27_ = ramp_q5_27;
      phase_samples_left_ = 0;
      return HandOffToNextStage(
        sample_buffer, block_samples_left, bias_q31, bias_slope_q31);
    }
  }

  value_q30_ = value_q30;
  bias_q31_ = bias_q31;
  dither_phase_u32_ = dither_phase_u32;
  chiff_shift_ramp_q5_27_ = ramp_q5_27;
  if (timed) phase_samples_left_ = stage_samples_left;
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
  chiff_floor_q30_ = ScaleRatio(chiff_floor_q30_, num, den);
  chiff_span_q14_ = ScaleRatio(chiff_span_q14_, num, den);
  for (int i = 0; i < ENV_NUM_STAGES; ++i) {
    stage_target_q30_[i] = ScaleRatio(stage_target_q30_[i], num, den);
  }
}

}  // namespace yarns
