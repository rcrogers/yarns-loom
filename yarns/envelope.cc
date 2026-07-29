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
const uint32_t kSlewTimesPerStageLog2_q5_27 = 2u << 27;

// Base shift is capped so that shift + dither <= 28: keeps `delta >> shift`
// well-defined, and 2^28 samples is already an absurdly long time constant.
const uint32_t kMaxSlewTimeLog2_q5_27 = 27u << 27;

// chiff_amount lives in [0, kChiffAmountMax].
const uint32_t kChiffAmountBits = 7;
const uint32_t kChiffAmountMax = (1u << kChiffAmountBits) - 1;

// Chiff window as a multiple of the ATTACK duration: at kChiffDurationCenter the
// window equals the attack; each side spans +-kChiffOctaves octaves (setting 127
// ~= 8x, setting 0 = 1/8x). Center is the 0..127 setting midpoint, and the
// divisor for the octave map (see ChiffWindowSamples) -- a power of two.
const int32_t kChiffDurationCenter = 64;
const int32_t kChiffOctaves = 3;

// Where a timed stage's slew actually lands: 1 - e^-k for k time constants
// (kSlewTimesPerStageLog2 = 2 -> k = 4). lut_env_expo is normalized to land
// at 1.0, so closed-form means read through it are scaled by this fraction
// to match the true slew (else the mean leads the value near stage ends).
const uint16_t kStageLandingFraction_u16 = 64335;  // round((1 - e^-4) * 2^16)

// The +/- perturbation on the slew input, as a fraction of the note's
// range (sim: k = 0.9).
const int32_t kChiffPerturbFraction_q15 = static_cast<int32_t>(0.9 * (1 << 15));

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
  stage_slew_time_log2_q5_27_ = 0;
  slew_rate_q31_ = 0;
  chiff_slew_rate_decay_q32_ = 0;
  slew_time_log2_q5_27_ = 0;
  chiff_slew_time_log2_step_q5_27_ = 0;
  chiff_slew_time_log2_end_q5_27_ = 0;
  chiff_duration_samples_left_ = 0;
  stage_slew_rate_q31_ = 0;
  chiff_input_perturb_q30_ = 0;
  chiff_input_perturb_step_q30_ = 0;
  int32_t zero_value_q30 = zero_value_s16 << (31 - 16);
  value_q30_ = zero_value_q30;
  stage_start_q30_ = zero_value_q30;
  chiff_floor_q30_ = zero_value_q30;
  chiff_top_q30_ = zero_value_q30;
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
  Trigger(ENV_STAGE_DEAD);
}

void Envelope::NoteOff() {
  Trigger(ENV_STAGE_RELEASE);
}

// Duration -> the slew time that settles within it: log2(samples/4), Q5.27,
// clamped to [0, kMaxSlewTimeLog2]. The /4 is kSlewTimesPerStageLog2 -- four
// slew times per stage. log2 as integer bits plus a linear mantissa fraction
// (max error ~0.09, same approximation spirit as Trigger's).
static uint32_t SlewTimeLog2FromDuration_q5_27(uint32_t samples) {
  if (samples < 4) return 0;  // log2 <= kSlewTimesPerStageLog2
  uint32_t leading_zeros = __builtin_clz(samples);
  uint32_t integer_bits = 31 - leading_zeros;
  uint32_t mantissa_frac_q5_27 =
      ((samples << leading_zeros) & 0x7FFFFFFFu) >> 4;
  uint32_t log2_q5_27 = (integer_bits << 27) + mantissa_frac_q5_27;
  if (log2_q5_27 <= kSlewTimesPerStageLog2_q5_27) return 0;
  return std::min(
      log2_q5_27 - kSlewTimesPerStageLog2_q5_27, kMaxSlewTimeLog2_q5_27);
}

// Integer square root of a u32 (bit-pair method); result is
// sqrt(x) in the halved Q-domain (sqrt of Q31 -> ~Q15.5). Cold path.
static uint32_t Sqrt32(uint32_t x) {
  uint32_t result = 0, bit = 1u << 30;
  while (bit > x) bit >>= 2;
  while (bit) {
    if (x >= result + bit) { x -= result + bit; result = (result >> 1) + bit; }
    else result >>= 1;
    bit >>= 2;
  }
  return result;
}

// How much of a +/- perturbation on the slew input survives to the slew's
// output, ~Q15.5 (46341 == 1.0): min(1, 3*sqrt(r/(2*(2-r)))) -- three sigma,
// clamped. Only ever wanted as a RATIO between two rates; see below.
static uint32_t SlewPerturbResponse_q15_5(int32_t slew_rate_q31) {
  int64_t denom = (4LL << 31) - 2 * static_cast<int64_t>(slew_rate_q31);
  uint32_t t_q31 = static_cast<uint32_t>(
    (static_cast<int64_t>(slew_rate_q31) << 31) / denom);
  uint32_t root = 3 * Sqrt32(t_q31);
  const uint32_t kOne_q15_5 = 46341;  // round(2^15.5)
  return root > kOne_q15_5 ? kOne_q15_5 : root;
}

// When the slew rate is clamped up to the stage rate, the perturbation has to
// shrink by the same factor the slew's response grew, or the floor energizes
// the chiff at the stage rate. Q15.5, 1<<15 == 1.0. Cold path: only when the
// floor binds.
static uint32_t ChiffPerturbScaleForClampedRate_q15_5(
    int32_t slew_rate_q31, int32_t clamped_slew_rate_q31) {
  return (SlewPerturbResponse_q15_5(slew_rate_q31) << 15)
    / std::max<uint32_t>(SlewPerturbResponse_q15_5(clamped_slew_rate_q31), 1u);
}

// Defined below (Hacker's Delight divlu); used by the input-base blend.
static uint32_t DivU64ByU32(uint32_t hi, uint32_t lo, uint32_t divisor);

// Defined below; chiff window in samples, scaled off the attack duration.
static uint32_t ChiffWindowSamples(
  uint32_t attack_increment_u32, uint8_t chiff_duration);

// CHIFF AMOUNT -> the slew time the chiff STARTS at, and nothing else. AMOUNT
// does not scale the perturbation: low amounts are quiet because a slow slew
// realizes less of the same perturbation. Warped by lut_env_expo (1 - e^-4x),
// normalized so AMOUNT max lands exactly on the fastest start (the raw table
// tops out just short). Slowest start is the slew time of a 1-second stage.
static uint32_t ChiffSlewTimeLog2FromAmount_q5_27(uint8_t chiff_amount) {
  const uint32_t kWarpStep = (LUT_ENV_EXPO_SIZE - 1) >> kChiffAmountBits;
  const uint32_t warp_max_u16 = lut_env_expo[kChiffAmountMax * kWarpStep];
  const uint32_t warp_u16 =
    (static_cast<uint32_t>(lut_env_expo[chiff_amount * kWarpStep]) << 16)
      / warp_max_u16;
  const uint32_t kFastestStart_q5_27 = 1u << 27;
  const uint32_t slowest_start_q5_27 = SlewTimeLog2FromDuration_q5_27(kFrameHz);
  return slowest_start_q5_27 - static_cast<uint32_t>(
    (static_cast<uint64_t>(slowest_start_q5_27 - kFastestStart_q5_27) *
     warp_u16) >> 16);
}

void Envelope::NoteOn(
  ADSR& adsr,
  // Bounds stored as s32 but semantically s16
  int32_t min_target_s16, int32_t max_target_s16,
  uint8_t chiff_amount, uint8_t chiff_duration
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
  // The note's range, as ORDERED bounds: the range may be numerically
  // inverted (CV DAC codes fall as volts rise; a warped timbre target may be
  // negative), so min/max over the stage targets, not release/peak.
  int32_t release_q30 = stage_target_q30_[ENV_STAGE_RELEASE];
  chiff_top_q30_ = std::max(release_q30, std::max(
    stage_target_q30_[ENV_STAGE_ATTACK], stage_target_q30_[ENV_STAGE_SUSTAIN]));
  chiff_floor_q30_ = std::min(release_q30, std::min(
    stage_target_q30_[ENV_STAGE_ATTACK], stage_target_q30_[ENV_STAGE_SUSTAIN]));

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
      // Fresh attack: arm the chiff. Trigger first so the stage machinery
      // (nominal shift, dialed rate) is set up for the attack; the slewed
      // value carries across a retrigger for continuity.
      Trigger(ENV_STAGE_ATTACK);
      // The chiff window is a MULTIPLE of the attack, set by CHIFF DURATION
      // (center = 1x the attack, +-kChiffOctaves octaves across the range).
      uint32_t window_samples =
        ChiffWindowSamples(adsr.attack_u32, chiff_duration);
      chiff_duration_samples_left_ = chiff_amount ? window_samples : 0;
      if (!chiff_duration_samples_left_) {
        RederiveSlewState();
        break;
      }
      // The slew time sweeps from a start set by AMOUNT to an end set by the
      // window. The SPAN between them is the whole audible effect.
      uint32_t slew_time_log2_start = ChiffSlewTimeLog2FromAmount_q5_27(chiff_amount);
      chiff_slew_time_log2_end_q5_27_ = SlewTimeLog2FromDuration_q5_27(window_samples);
      // The two ends are anchored to unrelated references, so nothing makes the
      // span positive. A slow start or a short window clamps it to zero and the
      // chiff gets NO sweep at all -- e.g. AMOUNT 16 with any attack up to
      // ~30ms. Known defect, not a degenerate corner.
      if (slew_time_log2_start > chiff_slew_time_log2_end_q5_27_) {
        slew_time_log2_start = chiff_slew_time_log2_end_q5_27_;
      }
      slew_time_log2_q5_27_ = slew_time_log2_start;
      // Perturbation: kChiffPerturbFraction of the note's range, fading
      // linearly to 0 over the window.
      chiff_input_perturb_q30_ = static_cast<int32_t>(
        (static_cast<int64_t>(chiff_top_q30_ - chiff_floor_q30_) *
         kChiffPerturbFraction_q15) >> 15);
      chiff_input_perturb_step_q30_ =
        chiff_input_perturb_q30_ / static_cast<int32_t>(window_samples);
      RederiveSlewState();
      break;
    }
  }
}

// Slew rate = 2^-slew_time_log2, Q31 (2^31 == 1.0). Result fits int32.
static inline int32_t SlewRateFromTimeLog2_q31(uint32_t slew_time_log2_q5_27) {
  uint32_t integer_part = slew_time_log2_q5_27 >> 27;
  uint32_t two_pow_neg_fraction_u16 = Interpolate824(
    lut_expo2_neg, (slew_time_log2_q5_27 & 0x07FFFFFFu) << 5);
  return (two_pow_neg_fraction_u16 << 15) >> integer_part;
}

// Chiff window in samples, as a multiple of the attack duration. The exponent
// (setting - center) * kChiffOctaves / center is octaves relative to the
// attack, Q5.27 signed; the window is attack_samples * 2^exponent.
// SlewRateFromTimeLog2
// gives 2^-magnitude, so the <= attack side (exponent <= 0) is one multiply; the
// > attack side factors 2^e into 2^(int+1) * 2^-(1-frac). Clamped to >= 1 sample
// -- equivalently, the chiff's phase increment capped at UINT32_MAX.
static uint32_t ChiffWindowSamples(
    uint32_t attack_increment_u32, uint8_t chiff_duration) {
  uint32_t attack_samples = attack_increment_u32
    ? (UINT32_MAX / attack_increment_u32) : UINT32_MAX;
  int32_t exponent_q5_27 = static_cast<int32_t>(
    (static_cast<int64_t>(chiff_duration) - kChiffDurationCenter)
      * kChiffOctaves * (1 << 27) / kChiffDurationCenter);
  if (exponent_q5_27 <= 0) {
    int32_t scale_q31 = SlewRateFromTimeLog2_q31(static_cast<uint32_t>(-exponent_q5_27));
    uint32_t window = static_cast<uint32_t>(
      (static_cast<uint64_t>(attack_samples) * scale_q31) >> 31);
    return window ? window : 1;
  }
  uint32_t int_part = static_cast<uint32_t>(exponent_q5_27) >> 27;
  uint32_t frac_q27 = static_cast<uint32_t>(exponent_q5_27) & 0x07FFFFFFu;
  int32_t scale_q31 = SlewRateFromTimeLog2_q31((1u << 27) - frac_q27);
  return static_cast<uint32_t>(
    ((static_cast<uint64_t>(attack_samples) << (int_part + 1)) * scale_q31) >> 31);
}

// decay = 1 - 2^-increment in Q32, via 2-term Taylor of 1 - 2^-x about x = 0
// (u = x*ln2): decay ~ u - u^2/2. Exact enough since `increment` is a tiny
// per-sample shift step. Q32 (small positive) so the ramp step is a single
// SMMUL: rate -= (rate * decay) >> 32.
static inline int32_t DecayFromIncrement_q32(uint32_t increment_q5_27) {
  const uint32_t kLn2_q28 = 186065279u;  // round(ln2 * 2^28)
  int64_t u_q32 = (static_cast<int64_t>(increment_q5_27) * kLn2_q28) >> 23;
  return static_cast<int32_t>(u_q32 - ((u_q32 * u_q32) >> 33));
}

void Envelope::RederiveSlewState() {
  // The dialed (chiff-free mean / classic) slew always runs at the stage's
  // own nominal rate.
  stage_slew_rate_q31_ = SlewRateFromTimeLog2_q31(stage_slew_time_log2_q5_27_);
  if (chiff_duration_samples_left_) {
    // Chiff sweep: from the current slew time up to the chiff's own end,
    // over the remaining window (compressed windows just sweep
    // faster). NoteOn guarantees start <= end; guard anyway.
    if (slew_time_log2_q5_27_ > chiff_slew_time_log2_end_q5_27_) {
      slew_time_log2_q5_27_ = chiff_slew_time_log2_end_q5_27_;
    }
    chiff_slew_time_log2_step_q5_27_ =
      (chiff_slew_time_log2_end_q5_27_ - slew_time_log2_q5_27_)
        / chiff_duration_samples_left_;
    slew_rate_q31_ = SlewRateFromTimeLog2_q31(slew_time_log2_q5_27_);
    chiff_slew_rate_decay_q32_ = DecayFromIncrement_q32(chiff_slew_time_log2_step_q5_27_);
  } else {
    slew_time_log2_q5_27_ = stage_slew_time_log2_q5_27_;
    chiff_slew_time_log2_step_q5_27_ = 0;
    slew_rate_q31_ = stage_slew_rate_q31_;
    chiff_slew_rate_decay_q32_ = 0;
  }
}

// Update current stage and its state. The slew always moves from the current
// value toward the stage target at a rate set by the stage's nominal
// duration, so there is no nominal-vs-actual delta bookkeeping: starting
// closer to the target just means arriving (proportionally) closer to it
// when the stage's sample countdown expires. The dialed level (the mean)
// carries across the transition untouched -- the chiff needs no anchor
// bookkeeping; the noise rides wherever dialed goes.
void Envelope::Trigger(EnvelopeStage stage) {
  // Anchor the new stage's start on the leaving stage's MEAN: with the chiff
  // off the value is the exact classic slew, so use it directly; a timed
  // stage's mean is closed-form from its phase (the same lut_env_expo curve
  // the slew traces); a hold's mean has converged to its target.
  if (!chiff_duration_samples_left_) {
    stage_start_q30_ = value_q30_;
  } else if (phase_increment_u32_) {
    // Phase runs 0 -> ~UINT32_MAX across the stage, but a stage that ran to
    // completion leaves stage_samples_left_ == 0, which WRAPS the product back
    // to phase 0 -- aliasing "fully elapsed" onto "not started" and anchoring
    // the new stage at the old stage's START instead of where it landed. That
    // collapses the next stage's mean (and yanks the value down with it)
    // whenever the chiff is still live at a handoff. Saturate instead.
    uint32_t phase_u32 = stage_samples_left_
      ? 0u - stage_samples_left_ * phase_increment_u32_
      : UINT32_MAX;
    uint32_t expo_u16 = (Interpolate824(lut_env_expo, phase_u32) *
      static_cast<uint32_t>(kStageLandingFraction_u16)) >> 16;
    stage_start_q30_ += static_cast<int32_t>(
      (static_cast<int64_t>(target_q30_ - stage_start_q30_) * expo_u16) >> 16);
  } else {
    stage_start_q30_ = target_q30_;
  }
  stage_ = stage;
  target_q30_ = stage_target_q30_[stage]; // Cache against new NoteOn
  switch (stage) {
    case ENV_STAGE_ATTACK : phase_increment_u32_ = adsr_->attack_u32  ; break;
    case ENV_STAGE_DECAY  : phase_increment_u32_ = adsr_->decay_u32   ; break;
    case ENV_STAGE_RELEASE: phase_increment_u32_ = adsr_->release_u32 ; break;
    default:
      // Hold stage: no countdown; keep slewing toward the target with the
      // rate inherited from the previous stage, converging asymptotically.
      // The chiff window, if live, keeps running on its own timetable.
      phase_increment_u32_ = 0;
      return;
  }

  if (stage_start_q30_ == target_q30_) {
    // Nothing to do this stage; skip ahead
    return Trigger(static_cast<EnvelopeStage>(stage + 1));
  }

  if (!phase_increment_u32_) {
    // Degenerate zero increment: treat as a hold
    return;
  }

  // Nominal stage duration in samples
  stage_samples_left_ = UINT32_MAX / phase_increment_u32_;

  // Slew shift from stage duration: with N = 2^32 / increment samples and
  // k = 2^kSlewTimesPerStageLog2 time constants per stage, the time
  // constant 2^shift = N / k, i.e. shift = log2(N) - log2(k).
  // log2(N) = 32 - log2(increment); log2(increment) is approximated as
  // (31 - clz) plus a linear mantissa fraction (max error ~0.09, i.e. ~6%
  // of the time constant -- inaudible, and monotone in the increment).
  uint8_t leading_zeros = __builtin_clz(phase_increment_u32_);
  if (leading_zeros >= 30) {
    // Increment <= 3: N >= ~2^30.5, whose shift saturates the cap anyway.
    // Computed separately because (leading_zeros + 1) << 27 would overflow.
    stage_slew_time_log2_q5_27_ = kMaxSlewTimeLog2_q5_27;
  } else {
    uint32_t mantissa_frac_q5_27 =
        ((phase_increment_u32_ << leading_zeros) & 0x7FFFFFFFu) >> 4;
    uint32_t log2_stage_samples_q5_27 =
        (static_cast<uint32_t>(leading_zeros + 1) << 27) - mantissa_frac_q5_27;
    stage_slew_time_log2_q5_27_ = log2_stage_samples_q5_27 <= kSlewTimesPerStageLog2_q5_27
      ? 0 // Stage too short for a meaningful slew; jump straight to target
      : std::min(
          log2_stage_samples_q5_27 - kSlewTimesPerStageLog2_q5_27,
          kMaxSlewTimeLog2_q5_27
        );
  }
  // Chiff compression: a stage shorter than the remaining window compresses
  // the burst so it lands at nothing by the stage's end instead of being cut
  // off mid-fizz. Release-only: the window deliberately spans attack/decay/
  // sustain on its own timetable (a fresh attack re-arms its window in
  // NoteOn, after this Trigger returns).
  if (stage == ENV_STAGE_RELEASE
      && chiff_duration_samples_left_ > stage_samples_left_) {
    chiff_duration_samples_left_ = stage_samples_left_;
    chiff_input_perturb_step_q30_ = chiff_input_perturb_q30_
      / static_cast<int32_t>(chiff_duration_samples_left_);
  }
  // Re-derive slew coefficients for the new stage (and, if live, the chiff's
  // ramp over its possibly-compressed window).
  RederiveSlewState();
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
  int32_t slew_rate_q31 = slew_rate_q31_;

  // One straight run, bounded by the block, the stage countdown, and (while
  // live) the chiff window. Whichever expires hands off or re-enters -- once,
  // not re-checked per sample.
  const bool timed = phase_increment_u32_ != 0;
  const bool chiff_live = chiff_duration_samples_left_ != 0;
  uint32_t run_samples = block_samples_left;
  if (timed) run_samples = std::min<uint32_t>(run_samples, stage_samples_left_);
  if (chiff_live) {
    run_samples = std::min<uint32_t>(run_samples, chiff_duration_samples_left_);
  }
  int16_t* const segment_end = sample_buffer + run_samples;
  const int32_t stage_target_q30 = target_q30_;

  {
    const int32_t floor_q30 = chiff_floor_q30_;
    const int32_t top_q30 = chiff_top_q30_;
    // Noise-slew floor (timed stages): never slower than the stage's own
    // rate, else the value hangs on a moving stage near the window's slow
    // end. Monotone (the rate only falls), so flooring freezes the sweep.
    int32_t decay_q32 = chiff_slew_rate_decay_q32_;
    // The floor exists to TRACK the dialed level, not to energize the chiff:
    // floored, a full perturbation would ride the stage rate and AMOUNT 1
    // would sound like attack-speed noise (a 0 -> 1 discontinuity). Shrinking
    // the perturbation by the response ratio keeps the output continuous
    // across the floor boundary, and sends AMOUNT -> 0 to silence smoothly.
    //
    // Both the floor and the scale are PER-RUN scratch: neither may be
    // written back into the persistent state. The chiff's own rate keeps
    // ramping on its own schedule (recovered from the slew time below), and
    // the perturbation keeps fading linearly -- persisting either compounds
    // it every block and collapses the burst in a few blocks.
    // Sentinel 1<<15 means "scale is exactly 1.0, skip the multiply" -- also
    // what the ratio computes to when both responses cap at 1.0.
    uint32_t chiff_perturb_scale_q15_5 = 1u << 15;
    if (timed && slew_rate_q31 < stage_slew_rate_q31_) {
      chiff_perturb_scale_q15_5 = ChiffPerturbScaleForClampedRate_q15_5(
        slew_rate_q31, stage_slew_rate_q31_);
      slew_rate_q31 = stage_slew_rate_q31_;
      decay_q32 = 0;
    }
    // Input base: the dialed level is CLOSED-FORM from the stage phase -- start +
    // (target - start) * lut_env_expo[phase], no iterated level state --
    // blended toward the stage target by alphaStage/alphaEff so the value's
    // expected step equals the mean's step in every regime (else it trails
    // the envelope: the kink at the window's end). Holds and a closed
    // window feed the slew the stage target (the classic asymptotic slew).
    int32_t base_q30 = stage_target_q30;
    if (chiff_live && timed) {
      uint32_t phase_u32 = 0u - stage_samples_left_ * phase_increment_u32_;
      const uint32_t expo_u16 = (Interpolate824(lut_env_expo, phase_u32) *
        static_cast<uint32_t>(kStageLandingFraction_u16)) >> 16;
      const int32_t mean_q30 = stage_start_q30_ + static_cast<int32_t>(
        (static_cast<int64_t>(stage_target_q30 - stage_start_q30_) *
         expo_u16) >> 16);
      uint32_t ratio_q31 = slew_rate_q31 == stage_slew_rate_q31_
        ? static_cast<uint32_t>(INT32_MAX)
        : DivU64ByU32(static_cast<uint32_t>(stage_slew_rate_q31_) >> 1,
                      static_cast<uint32_t>(stage_slew_rate_q31_) << 31,
                      static_cast<uint32_t>(slew_rate_q31));
      base_q30 = mean_q30 + static_cast<int32_t>(
        (static_cast<int64_t>(stage_target_q30 - mean_q30) * ratio_q31)
        >> 31);
    }
    const int32_t perturb_q30 = chiff_input_perturb_q30_;
    int32_t scaled_perturb_q30 = perturb_q30;
    if (chiff_perturb_scale_q15_5 != (1u << 15)) {
      scaled_perturb_q30 = static_cast<int32_t>(
        (static_cast<int64_t>(perturb_q30) * chiff_perturb_scale_q15_5) >> 15);
    }
    // No rail guard: the input centre IS the base. The guard used to hold the
    // centre a guarded run's excursion off the acoustic-peak rail, costing
    // sag everywhere to protect against a tail event the output clamp already
    // handles. Removing it was checked against the sim by ear (no banding, no
    // excursions) and by measurement: rail contact cannot exceed the clamp,
    // and floor dwell at low sustain is no worse than with the guard present.
    const int32_t center_q30 = base_q30;
    const int32_t slew_input_up_q30 = center_q30 + scaled_perturb_q30;
    const int32_t slew_input_down_q30 = center_q30 - scaled_perturb_q30;

    // Buffer position (plus this instance's decorrelation offset) doubles as
    // the index into the shared PRNG block.
    const uint32_t* prng = &shared_prng_buffer[
      prng_offset_u32_ + (kAudioBlockSize - block_samples_left)];
    // 32-bit ARMv7+ only (Thumb-2: smull / sbfx / usat / IT). Gate on __arm__,
    // NOT bare __ARM_ARCH: the build host is arm64 (Apple Silicon), which
    // defines __ARM_ARCH == 8 but not __arm__ -- so the host harness and the
    // Emscripten sim take the C #else (the reference). The firmware (Cortex-M3)
    // and the off-hardware QEMU harness use the same arm-none-eabi Cortex-M3
    // build, which defines __arm__ && __ARM_ARCH == 7, and take this asm.
#if defined(__arm__) && __ARM_ARCH >= 7
    // HAND-ALLOCATED loop. The 12 values live across this loop fit in r0-r11
    // with ip/lr as scratch, but GCC 4.8 spills 5 of them from poor
    // allocation, paying ~5 reload ldrs per sample. Presenting them all as asm
    // operands forces GCC to pin them; the loop then runs with 0 spills. The
    // behaviour is the C loop in #else (kept as the host reference, golden-
    // verified) -- this block must be flash-verified bit-identical.
    //
    // Per sample: geometric rate sweep (rate -= (rate*decay)>>32), input
    // select (up ^ (xor & sign)), one-pole slew (value +=
    // (input-value)*rate>>31), rail clamp, bias ramp, and the EnvelopeSample
    // mix+usat. `end == buf` (run_samples 0) is handled by the leading guard.
    const int32_t slew_input_xor_q30 = slew_input_up_q30 ^ slew_input_down_q30;
    int16_t* const segment_end_asm = segment_end;
    __asm__ volatile(
      "  cmp   %[buf], %[end]\n"
      "  beq   2f\n"
      "1:\n"
      "  smull ip, lr, %[rate], %[decay]\n"     // (rate*decay), lr = hi word
      "  sub   %[rate], %[rate], lr\n"         // rate -= (rate*decay)>>32
      "  ldr   ip, [%[prng]], #4\n"              // draw = *prng++
      "  sbfx  ip, ip, #16, #1\n"                // sign mask from bit 16
      "  and   ip, %[inputxor], ip\n"
      "  eor   ip, ip, %[inputup]\n"               // input = up ^ (xor & sign)
      "  sub   ip, ip, %[value]\n"               // delta = input - value
      "  smull ip, lr, ip, %[rate]\n"           // delta*rate (ip=lo, lr=hi)
      "  add   %[value], %[value], lr, lsl #1\n" // value += (product>>31): hi<<1
      "  add   %[value], %[value], ip, lsr #31\n"//               + lo>>31
      "  cmp   %[value], %[floor]\n"
      "  it    lt\n"
      "  movlt %[value], %[floor]\n"             // clamp to floor
      "  cmp   %[value], %[top]\n"
      "  it    gt\n"
      "  movgt %[value], %[top]\n"               // clamp to top
      "  add   %[bias], %[bias], %[slope]\n"     // bias += bias_slope
      "  asr   ip, %[bias], #15\n"               // bias >> 15
      "  add   ip, ip, %[value], asr #14\n"      // + value >> 14
      "  usat  ip, #15, ip, asr #1\n"            // EnvelopeSample saturate
      "  strh  ip, [%[buf]], #2\n"               // *sample_buffer++
      "  cmp   %[buf], %[end]\n"
      "  bne   1b\n"
      "2:\n"
      : [value] "+r"(value_q30), [rate] "+r"(slew_rate_q31),
        [bias] "+r"(bias_q31), [prng] "+r"(prng), [buf] "+r"(sample_buffer)
      : [decay] "r"(decay_q32), [inputup] "r"(slew_input_up_q30),
        [inputxor] "r"(slew_input_xor_q30), [floor] "r"(floor_q30), [top] "r"(top_q30),
        [slope] "r"(bias_slope_q31), [end] "r"(segment_end_asm)
      : "ip", "lr", "cc", "memory");
#else
    while (sample_buffer != segment_end) {
      // PRNG budget: bit 16 = perturbation sign.
      uint32_t chiff_draw_u32 = *prng++;
      int32_t ramp_hi = static_cast<int32_t>(
        (static_cast<int64_t>(slew_rate_q31) * decay_q32) >> 32);
      slew_rate_q31 -= ramp_hi;
      int32_t sign_mask = static_cast<int32_t>(chiff_draw_u32 << 15) >> 31;
      int32_t slew_input_q30 =
        slew_input_up_q30 ^ ((slew_input_up_q30 ^ slew_input_down_q30) & sign_mask);
      value_q30 += static_cast<int32_t>(
        (static_cast<int64_t>(slew_input_q30 - value_q30) * slew_rate_q31) >> 31);
      if (value_q30 < floor_q30) value_q30 = floor_q30;
      if (value_q30 > top_q30) value_q30 = top_q30;
      bias_q31 += bias_slope_q31;
      *sample_buffer++ = EnvelopeSample(value_q30, bias_q31);
    }
#endif
    if (chiff_live) {
      chiff_input_perturb_q30_ = perturb_q30 - chiff_input_perturb_step_q30_
        * static_cast<int32_t>(run_samples);
      if (chiff_input_perturb_q30_ < 0) chiff_input_perturb_q30_ = 0;
      uint32_t slew_time_log2_end =
        slew_time_log2_q5_27_ + chiff_slew_time_log2_step_q5_27_ * run_samples;
      if (slew_time_log2_end > chiff_slew_time_log2_end_q5_27_) {
        slew_time_log2_end = chiff_slew_time_log2_end_q5_27_;
      }
      chiff_duration_samples_left_ -= run_samples;
      if (chiff_duration_samples_left_ == 0) {
        // Window closed: the slew input collapses to the stage target
        // (exact classic slew); the value carries, no snap.
        chiff_input_perturb_q30_ = 0;
        slew_time_log2_q5_27_ = stage_slew_time_log2_q5_27_;
        slew_rate_q31 = stage_slew_rate_q31_;
        chiff_slew_rate_decay_q32_ = 0;
      } else {
        slew_time_log2_q5_27_ = slew_time_log2_end;
        // If the floor bound, the loop ran at the stage rate, so the register
        // no longer carries the chiff's own rate. Re-derive it from the
        // shift -- which ramped untouched -- so the two stay two encodings of
        // ONE rate. The floor test is re-read from the members rather than
        // remembered in a flag: both are untouched until this run's final
        // store, and a flag would pin a register across the sample loop.
        if (timed && slew_rate_q31_ < stage_slew_rate_q31_) {
          slew_rate_q31 = SlewRateFromTimeLog2_q31(slew_time_log2_end);
        }
      }
    }
  }

  block_samples_left -= run_samples;
  value_q30_ = value_q30;
  bias_q31_ = bias_q31;
  slew_rate_q31_ = slew_rate_q31;

  if (timed) {
    stage_samples_left_ -= run_samples;
    if (stage_samples_left_ == 0) {
      // Countdown expired: hand off to the next stage. Tail call stays flat.
      return HandOffToNextStage(
        sample_buffer, block_samples_left, bias_q31, bias_slope_q31);
    }
  }
  if (block_samples_left) {
    // The chiff window expired within this block: render the remainder
    // (same path; the chiff state is now inert).
    return RenderStage(
      sample_buffer, block_samples_left, bias_q31, bias_slope_q31);
  }
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
  stage_start_q30_ = ScaleRatio(stage_start_q30_, num, den);
  chiff_input_perturb_q30_ = ScaleRatio(chiff_input_perturb_q30_, num, den);
  chiff_input_perturb_step_q30_ = ScaleRatio(chiff_input_perturb_step_q30_, num, den);
  chiff_floor_q30_ = ScaleRatio(chiff_floor_q30_, num, den);
  chiff_top_q30_ = ScaleRatio(chiff_top_q30_, num, den);
  for (int i = 0; i < ENV_NUM_STAGES; ++i) {
    stage_target_q30_[i] = ScaleRatio(stage_target_q30_[i], num, den);
  }
}

}  // namespace yarns
