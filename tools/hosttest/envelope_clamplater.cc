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

// Where a timed stage's slew actually lands: 1 - e^-k for k time constants
// (kStageTimeConstantsLog2 = 2 -> k = 4). lut_env_expo is normalized to land
// at 1.0, so closed-form means read through it are scaled by this fraction
// to match the true slew (else the mean leads the value near stage ends).
const uint16_t kStageLandingFraction_u16 = 64335;  // round((1 - e^-4) * 2^16)

// Dart depth as a fraction of the note's range (sim: k = 0.9).
const int32_t kChiffDartDepth_q15 = static_cast<int32_t>(0.9 * (1 << 15));

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
  slew_alpha_decay_q32_ = 0;
  slew_shift_q5_27_ = 0;
  slew_shift_increment_q5_27_ = 0;
  chiff_dark_shift_q5_27_ = 0;
  chiff_duration_samples_left_ = 0;
  dialed_alpha_q31_ = 0;
  chiff_amp_q30_ = 0;
  chiff_amp_step_q30_ = 0;
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

// Slew shift for a window of `samples`: shift = log2(samples) - log2(k time
// constants), Q5.27, clamped to [0, kMaxSlewShift]. log2 as integer bits plus
// a linear mantissa fraction (max error ~0.09 shifts -- same approximation
// spirit as Trigger's increment-based derivation).
static uint32_t SlewShiftFromSamples_q5_27(uint32_t samples) {
  if (samples < 4) return 0;  // log2 <= kStageTimeConstantsLog2
  uint32_t leading_zeros = __builtin_clz(samples);
  uint32_t integer_bits = 31 - leading_zeros;
  uint32_t mantissa_frac_q5_27 =
      ((samples << leading_zeros) & 0x7FFFFFFFu) >> 4;
  uint32_t log2_q5_27 = (integer_bits << 27) + mantissa_frac_q5_27;
  if (log2_q5_27 <= kStageTimeConstantsLog2_q5_27) return 0;
  return std::min(
      log2_q5_27 - kStageTimeConstantsLog2_q5_27, kMaxSlewShift_q5_27);
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

// Realized reach of the slewed noise as a fraction of the raw dart depth,
// ~Q15.5 (46341 == 1.0): min(1, 3*sqrt(a/(2*(2-a)))). Cold path (used only
// when the noise-slew floor binds).
static uint32_t ReachFactor_q15_5(int32_t alpha_q31) {
  int64_t denom = (4LL << 31) - 2 * static_cast<int64_t>(alpha_q31);
  uint32_t t_q31 = static_cast<uint32_t>(
    (static_cast<int64_t>(alpha_q31) << 31) / denom);
  uint32_t root = 3 * Sqrt32(t_q31);
  const uint32_t kOne_q15_5 = 46341;  // round(2^15.5)
  return root > kOne_q15_5 ? kOne_q15_5 : root;
}

// Defined below (Hacker's Delight divlu); used by the aim-base blend.
static uint32_t DivU64ByU32(uint32_t hi, uint32_t lo, uint32_t divisor);

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
      // The chiff window is its own duration -- independent of the ADSR --
      // from the CHIFF DURATION setting (log map, ~1ms..8s).
      uint32_t window_samples = lut_chiff_duration_samples[chiff_duration];
      chiff_duration_samples_left_ = chiff_amount ? window_samples : 0;
      if (!chiff_duration_samples_left_) {
        ReSlopeSlewShift();
        break;
      }
      // The noise slew ramps from an amount-warped bright onset down to the
      // chiff's OWN dark endpoint (the shift of its window), so the burst
      // closes to ~DC by its own end regardless of stage. The onset's dim
      // anchor is the fixed 1-second shift -- NOT the duration-derived dark
      // -- so onset brightness depends only on the amount. The warp
      // (lut_env_expo, 1 - e^-4x) brings low amounts into the audible band;
      // amount max lands the onset at shift 1 (brightest).
      chiff_dark_shift_q5_27_ = SlewShiftFromSamples_q5_27(window_samples);
      // Warp normalized so amount max lands the onset at exactly shift 1
      // (the sim divides by 1 - e^-4; the raw table tops out just short).
      const uint32_t kWarpStep = (LUT_ENV_EXPO_SIZE - 1) >> kChiffAmountBits;
      const uint32_t warp_max_u16 = lut_env_expo[kChiffAmountMax * kWarpStep];
      uint32_t amount_warp_u16 =
        (static_cast<uint32_t>(lut_env_expo[chiff_amount * kWarpStep]) << 16)
          / warp_max_u16;
      const uint32_t kBrightestShift_q5_27 = 1u << 27;
      uint32_t onset_dim_q5_27 = SlewShiftFromSamples_q5_27(kFrameHz);
      uint32_t bright_q5_27 = onset_dim_q5_27 - static_cast<uint32_t>(
        (static_cast<uint64_t>(onset_dim_q5_27 - kBrightestShift_q5_27) *
         amount_warp_u16) >> 16);
      // Degenerate short-and-quiet chiff: keep the ramp upward (darkening).
      if (bright_q5_27 > chiff_dark_shift_q5_27_) {
        bright_q5_27 = chiff_dark_shift_q5_27_;
      }
      slew_shift_q5_27_ = bright_q5_27;
      // Dart depth: kChiffDartDepth of the note's range, fading linearly to
      // 0 over the window.
      chiff_amp_q30_ = static_cast<int32_t>(
        (static_cast<int64_t>(chiff_top_q30_ - chiff_floor_q30_) *
         kChiffDartDepth_q15) >> 15);
      chiff_amp_step_q30_ =
        chiff_amp_q30_ / static_cast<int32_t>(window_samples);
      ReSlopeSlewShift();
      break;
    }
  }
}

// alpha = 2^-shift in Q31 (2^31 == 1.0). Result <= 0x7FFF8000, fits int32.
static inline int32_t AlphaFromShift_q31(uint32_t shift_q5_27) {
  uint32_t shift_int = shift_q5_27 >> 27;
  uint32_t two_pow_neg_fraction_u16 = Interpolate824(
    lut_expo2_neg, (shift_q5_27 & 0x07FFFFFFu) << 5);
  return (two_pow_neg_fraction_u16 << 15) >> shift_int;
}

// decay = 1 - 2^-increment in Q32, via 2-term Taylor of 1 - 2^-x about x = 0
// (u = x*ln2): decay ~ u - u^2/2. Exact enough since `increment` is a tiny
// per-sample shift step. Q32 (small positive) so the ramp step is a single
// SMMUL: alpha -= (alpha * decay) >> 32.
static inline int32_t DecayFromIncrement_q32(uint32_t increment_q5_27) {
  const uint32_t kLn2_q28 = 186065279u;  // round(ln2 * 2^28)
  int64_t u_q32 = (static_cast<int64_t>(increment_q5_27) * kLn2_q28) >> 23;
  return static_cast<int32_t>(u_q32 - ((u_q32 * u_q32) >> 33));
}

void Envelope::ReSlopeSlewShift() {
  // The dialed (chiff-free mean / classic) slew always runs at the stage's
  // own nominal rate.
  dialed_alpha_q31_ = AlphaFromShift_q31(stage_nominal_slew_shift_q5_27_);
  if (chiff_duration_samples_left_) {
    // Noise-slew ramp: from the current shift up to the chiff's own dark
    // endpoint over the remaining window (compressed windows just ramp
    // faster). NoteOn guarantees shift <= dark; guard anyway.
    if (slew_shift_q5_27_ > chiff_dark_shift_q5_27_) {
      slew_shift_q5_27_ = chiff_dark_shift_q5_27_;
    }
    slew_shift_increment_q5_27_ =
      (chiff_dark_shift_q5_27_ - slew_shift_q5_27_)
        / chiff_duration_samples_left_;
    slew_alpha_q31_ = AlphaFromShift_q31(slew_shift_q5_27_);
    slew_alpha_decay_q32_ = DecayFromIncrement_q32(slew_shift_increment_q5_27_);
  } else {
    slew_shift_q5_27_ = stage_nominal_slew_shift_q5_27_;
    slew_shift_increment_q5_27_ = 0;
    slew_alpha_q31_ = dialed_alpha_q31_;
    slew_alpha_decay_q32_ = 0;
  }
}

// Update current stage and its state. The slew always moves from the current
// value toward the stage target at a rate set by the stage's nominal
// duration, so there is no nominal-vs-actual delta bookkeeping: starting
// closer to the target just means arriving (proportionally) closer to it
// when the stage's sample countdown expires. The dialed level (the mean)
// carries across the transition untouched -- the dart model needs no anchor
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
  // Chiff compression: a stage shorter than the remaining window compresses
  // the burst so it lands at nothing by the stage's end instead of being cut
  // off mid-fizz. Release-only: the window deliberately spans attack/decay/
  // sustain on its own timetable (a fresh attack re-arms its window in
  // NoteOn, after this Trigger returns).
  if (stage == ENV_STAGE_RELEASE
      && chiff_duration_samples_left_ > stage_samples_left_) {
    chiff_duration_samples_left_ = stage_samples_left_;
    chiff_amp_step_q30_ = chiff_amp_q30_
      / static_cast<int32_t>(chiff_duration_samples_left_);
  }
  // Re-derive slew coefficients for the new stage (and, if live, the chiff's
  // ramp over its possibly-compressed window).
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
  int32_t slew_alpha_q31 = slew_alpha_q31_;

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
    // rate, else the value hangs on a moving stage near the window's dark
    // end. Monotone (alpha only darkens), so flooring freezes the ramp.
    int32_t decay_q32 = slew_alpha_decay_q32_;
    // The floor exists to TRACK the mean, not to energize darts: floored,
    // full-depth noise would ride the stage rate and amount 1 would sound
    // like attack-speed noise (a 0 -> 1 discontinuity). Scale the dart span
    // by the realized-reach ratio so the noise carries only what the
    // un-floored chiff alpha affords -- continuous at the floor boundary,
    // and amount -> 0 sends the noise to 0 smoothly.
    //
    // Both the floor and the scale are PER-RUN scratch: neither may be
    // written back into the persistent state. The chiff's own alpha keeps
    // ramping on its own schedule (recovered from the shift below), and the
    // dart depth keeps fading linearly -- persisting either one compounds it
    // every block and collapses the burst in a few blocks.
    // Sentinel 1<<15 means "scale is exactly 1.0, skip the multiply" -- which
    // is also what the ratio computes to when both reaches cap at 1.0.
    uint32_t dart_scale_q15_5 = 1u << 15;
    if (timed && slew_alpha_q31 < dialed_alpha_q31_) {
      dart_scale_q15_5 = (ReachFactor_q15_5(slew_alpha_q31) << 15)
        / std::max<uint32_t>(ReachFactor_q15_5(dialed_alpha_q31_), 1u);
      slew_alpha_q31 = dialed_alpha_q31_;
      decay_q32 = 0;
    }
    // Aim base: the mean is CLOSED-FORM from the stage phase -- start +
    // (target - start) * lut_env_expo[phase], no iterated level state --
    // blended toward the stage target by alphaStage/alphaEff so the value's
    // expected step equals the mean's step in every regime (else it trails
    // the envelope: the kink at the window's end). Holds and a closed
    // window aim at the target (the classic asymptotic slew).
    int32_t base_q30 = stage_target_q30;
    if (chiff_live && timed) {
      uint32_t phase_u32 = 0u - stage_samples_left_ * phase_increment_u32_;
      const uint32_t expo_u16 = (Interpolate824(lut_env_expo, phase_u32) *
        static_cast<uint32_t>(kStageLandingFraction_u16)) >> 16;
      const int32_t mean_q30 = stage_start_q30_ + static_cast<int32_t>(
        (static_cast<int64_t>(stage_target_q30 - stage_start_q30_) *
         expo_u16) >> 16);
      uint32_t ratio_q31 = slew_alpha_q31 == dialed_alpha_q31_
        ? static_cast<uint32_t>(INT32_MAX)
        : DivU64ByU32(static_cast<uint32_t>(dialed_alpha_q31_) >> 1,
                      static_cast<uint32_t>(dialed_alpha_q31_) << 31,
                      static_cast<uint32_t>(slew_alpha_q31));
      base_q30 = mean_q30 + static_cast<int32_t>(
        (static_cast<int64_t>(stage_target_q30 - mean_q30) * ratio_q31)
        >> 31);
    }
    // The aim center stays off the acoustic-peak rail by the guarded tail
    // excursion tailF * amp, tailF = 1 - (1 - alpha)^(2^kChiffRunGuardLog2):
    // a run of that many same-direction draws covers only tailF of the way
    // to its aim, so full dart span fits all the way to the peak by
    // construction (no noise dip approaching it), and the sag converges on
    // two fading factors (depth and darkness). Relax aims (half the draws)
    // damp dwell at both rails; the floor side keeps the output clamp (the
    // loud onset trim).
    const int32_t amp_q30 = chiff_amp_q30_;
    int32_t dart_q30 = amp_q30;
    if (dart_scale_q15_5 != (1u << 15)) {
      dart_q30 = static_cast<int32_t>(
        (static_cast<int64_t>(amp_q30) * dart_scale_q15_5) >> 15);
    }
    // No rail guard: the aim centre IS the base. The guard used to hold the
    // centre a guarded run's excursion off the acoustic-peak rail, costing
    // sag everywhere to protect against a tail event the output clamp already
    // handles. Removing it was checked against the sim by ear (no banding, no
    // excursions) and by measurement: rail contact cannot exceed the clamp,
    // and floor dwell at low sustain is no worse than with the guard present.
    const int32_t center_q30 = base_q30;
    const int32_t aim_up_q30 = center_q30 + dart_q30;
    const int32_t aim_down_q30 = center_q30 - dart_q30;

    // Buffer position (plus this instance's decorrelation offset) doubles as
    // the index into the shared PRNG block.
    const uint32_t* prng = &shared_prng_buffer[
      prng_offset_u32_ + (kAudioBlockSize - block_samples_left)];
    while (sample_buffer != segment_end) {
      // PRNG budget: bit 15 = dart-or-relax, bit 16 = dart sign.
      uint32_t chiff_draw_u32 = *prng++;
      // Geometric coefficient ramp: alpha *= 2^-increment. Explicit SMULL
      // high word so GCC 4.8 keeps the product 32-bit (else it spills).
      int32_t ramp_hi = (int32_t)(
        ((int64_t)slew_alpha_q31 * (int32_t)decay_q32) >> 32);
      slew_alpha_q31 -= ramp_hi;
      // Every draw is a dart, up or down: one branchless sign-mask blend.
      // There is no relax aim -- it existed to damp dwell at the rails, and
      // measurement showed it does not (removing it leaves floor dwell no
      // worse), so it was costing per-sample work for nothing.
      int32_t sign_mask = static_cast<int32_t>(chiff_draw_u32 << 15) >> 31;
      int32_t aim_q30 =
        aim_up_q30 ^ ((aim_up_q30 ^ aim_down_q30) & sign_mask);
      // Never overshoots: alpha <= 1, so |step| <= |delta|.
      value_q30 += static_cast<int32_t>(
        (static_cast<int64_t>(aim_q30 - value_q30) * slew_alpha_q31) >> 31);
      // VARIANT C: clamp LATER. The internal state winds up freely; only the
      // value written out is clamped to the note rails. Output cannot exceed
      // the note peak, but the integrator is not prevented from accumulating.
      int32_t out_value_q30 = value_q30;
      if (out_value_q30 < floor_q30) out_value_q30 = floor_q30;
      if (out_value_q30 > top_q30) out_value_q30 = top_q30;
      bias_q31 += bias_slope_q31;
      *sample_buffer++ = EnvelopeSample(out_value_q30, bias_q31);
    }
    if (chiff_live) {
      chiff_amp_q30_ = amp_q30 - chiff_amp_step_q30_
        * static_cast<int32_t>(run_samples);
      if (chiff_amp_q30_ < 0) chiff_amp_q30_ = 0;
      uint32_t shift_end =
        slew_shift_q5_27_ + slew_shift_increment_q5_27_ * run_samples;
      if (shift_end > chiff_dark_shift_q5_27_) {
        shift_end = chiff_dark_shift_q5_27_;
      }
      chiff_duration_samples_left_ -= run_samples;
      if (chiff_duration_samples_left_ == 0) {
        // Window closed: aims collapse to the stage target from here on
        // (exact classic slew); the value carries, no snap.
        chiff_amp_q30_ = 0;
        slew_shift_q5_27_ = stage_nominal_slew_shift_q5_27_;
        slew_alpha_q31 = dialed_alpha_q31_;
        slew_alpha_decay_q32_ = 0;
      } else {
        slew_shift_q5_27_ = shift_end;
        // If the floor bound, the loop ran at the stage rate, so the register
        // no longer carries the chiff's own alpha. Re-derive it from the
        // shift -- which ramped untouched -- so the two stay two encodings of
        // ONE rate. The floor test is re-read from the members rather than
        // remembered in a flag: both are untouched until this run's final
        // store, and a flag would pin a register across the sample loop.
        if (timed && slew_alpha_q31_ < dialed_alpha_q31_) {
          slew_alpha_q31 = AlphaFromShift_q31(shift_end);
        }
      }
    }
  }

  block_samples_left -= run_samples;
  value_q30_ = value_q30;
  bias_q31_ = bias_q31;
  slew_alpha_q31_ = slew_alpha_q31;

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
    // (same path; the dart state is now inert).
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
  chiff_amp_q30_ = ScaleRatio(chiff_amp_q30_, num, den);
  chiff_amp_step_q30_ = ScaleRatio(chiff_amp_step_q30_, num, den);
  chiff_floor_q30_ = ScaleRatio(chiff_floor_q30_, num, den);
  chiff_top_q30_ = ScaleRatio(chiff_top_q30_, num, den);
  for (int i = 0; i < ENV_NUM_STAGES; ++i) {
    stage_target_q30_[i] = ScaleRatio(stage_target_q30_[i], num, den);
  }
}

}  // namespace yarns
