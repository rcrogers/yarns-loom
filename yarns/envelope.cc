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
// with no per-sample XOR, which would cost a register and a spill in the
// render loop.
namespace {
  uint32_t shared_prng_buffer[2 * kAudioBlockSize];
  uint32_t shared_prng_state = 0xCAFEBABE;
}  // namespace

// The DAC range in Q30: the s16 output 32767 is 32767 << 15, and
// (2^30 - 1) >> 15 is 32767 exactly. It bounds the envelope's own integrator
// (anti-windup) and, separately, the biased output.
const int32_t kValueMax_q30 = (1 << 30) - 1;

// The output sample is the s16 range, which USAT #15 states directly.
const int kSampleBits = 15;

// How far a mean must move to sit inside [lo, hi]; 0 when it already does.
inline int32_t ClampOffset(int32_t mean, int32_t lo, int32_t hi) {
  if (mean < lo) return lo - mean;
  if (mean > hi) return hi - mean;
  return 0;
}

// 1.0 for the slew's response to a chiff input, Q15.5.
const uint32_t kResponseOne_q15_5 = 46341;  // 2^15.5 == 1.0

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

// A timed stage runs for kSlewTimesPerStageLog2 = 2, i.e. FOUR time constants,
// and a one-pole covers only 1 - e^-4 = 98.17% of its span in that time. So the
// slew AIMS PAST its target by the reciprocal: aim = start + (target - start) /
// (1 - e^-4). It then lands EXACTLY on the target as the stage's countdown
// expires, instead of handing off 1.8% short and cornering there.
//
// The aim overshoots the target by 1.9% of the stage's span, which is a level
// the note may not have -- that is fine, because the value never reaches the
// aim: it arrives at the target precisely when the stage ends. It does mean the
// slew INPUT can sit outside the note's range. Nothing clamps it: the mean is
// held clear of the rails at the point of use, and no integrator carries it.
//
// It also makes lut_env_expo read straight. The table is normalized to land at
// 1.0, so it already describes (1 - e^-4t/T)/(1 - e^-4) -- exactly the true
// slew once the aim carries the 1/(1 - e^-4). No landing-fraction scaling is
// needed on the closed-form nominal value; it cancels.
const uint32_t kStageAimOvershoot_u16 = 66759;  // round(2^16 / (1 - e^-4))

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
  chiff_slew_rate_decay_q32_ = 0;
  slew_time_log2_q5_27_ = 0;
  chiff_slew_time_log2_step_q5_27_ = 0;
  chiff_slew_time_log2_end_q5_27_ = 0;
  chiff_target_samples_ = 0;
  chiff_input_fraction_q30_ = 0;
  chiff_input_fraction_step_q5_27_ = 0;
  chiff_input_full_q30_ = 0;
  // Bias is a CONTINUOUS control, not note state -- nothing else resets it,
  // because it must survive NoteOn/NoteOff to stay smooth. But Init means
  // "from a known state", and it was the one thing Init left alone: in the
  // firmware it is whatever the object was constructed with, and anywhere the
  // envelope is reused (the sim renders every note through one static) the
  // previous note's bias leaked into the next one's FIRST BLOCK, which then
  // ramped from the wrong place. Found as a 63-sample sim-vs-native mismatch.
  bias_q31_ = 0;
  int32_t zero_value_q30 = zero_value_s16 << (31 - 16);
  value_q30_ = zero_value_q30;
  nominal_q30_ = zero_value_q30;
  chiff_state_q30_ = 0;
  stage_start_q30_ = zero_value_q30;
  chiff_floor_q30_ = zero_value_q30;
  chiff_top_q30_ = zero_value_q30;
  clamp_base_q30_ = std::min<int32_t>(zero_value_q30, 0);
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

// how slow the chiff's slew will have got after `samples` more
// samples, never past its max. The slew slows on a schedule set by
// the NOMINAL duration, so at a release deadline it is part-way down: its rate
// there is neither the one it started at nor the one it ends at. That rate is
// what decides how much of the chiff input is audible, so the shrink has to
// be sized against it.
// the MAX slew time the chiff reaches -- the larger of the slew
// time its own duration implies and the STAGE's slew time (larger = slower).
// Stopping at the stage's is what returns the envelope to exactly its nominal
// level curve once the chiff has gone quiet; without it a short chiff leaves
// the slew running fast for the rest of the note, because the slew time its
// duration implies is short and a short slew time is a fast slew.
//
// THE CAP IS THE CHIFF'S OWN, from its duration, and nothing about the stages
// enters it. Duration being attack-relative is a control convenience in how the
// duration is DECIDED; once decided, nothing further about stage timing may
// touch the chiff (user, 2026-08-02), and the release compression below is the
// single stated exception.
//
// A CAP IS REQUIRED, and not for the reason this once carried ("so the rate
// lands back on the envelope's own rate" -- a tracking argument the dedicated
// chiff filter deleted). With no cap the rate falls to zero, and a one-pole at
// rate zero HOLDS its state rather than decaying: chiff_state stops moving and
// the shrinking input can no longer pull it down. MEASURED uncapped: a +716 LSB
// DC offset left on the envelope, permanently.
uint32_t Envelope::ChiffMaxSlewTime_q5_27() const {
  return chiff_slew_time_log2_end_q5_27_;
}

uint32_t Envelope::ChiffSlewTimeAtDeadline_q5_27(uint32_t samples) const {
  const uint64_t swept = static_cast<uint64_t>(chiff_slew_time_log2_step_q5_27_)
    * samples;
  const uint64_t at_deadline =
    static_cast<uint64_t>(slew_time_log2_q5_27_) + swept;
  const uint32_t end = ChiffMaxSlewTime_q5_27();
  return at_deadline > end ? end : static_cast<uint32_t>(at_deadline);
}

// the +/- the chiff puts on the slew input. Sized from the note's
// ALLOWED range (set in NoteOn) and NOT from the level the note actually
// reaches, so the noise does not thin out at a low sustain or a low peak --
// the exciter hits the same way however hard the note is played.
//
// Making room is the MEAN's job (RenderStage holds it clear of the rails), not
// the chiff input's. Sizing the chiff input from the room between the level
// and the rails was tried and rejected: the room vanishes as the level reaches
// the peak, so the excursion notched there.
int32_t Envelope::ChiffInput_q30() const {
  return static_cast<int32_t>(
    (static_cast<int64_t>(chiff_input_full_q30_) * chiff_input_fraction_q30_)
    >> 30);
}

// Duration -> the slew time that settles within it: log2(samples/4), Q5.27,
// clamped to [0, kMaxSlewTimeLog2]. The /4 is kSlewTimesPerStageLog2 -- four
// slew times per stage. log2 as integer bits plus a linear mantissa fraction
// (max error ~0.09, same approximation spirit as Trigger's).
static uint32_t SlewTimeLog2FromDuration_q5_27(uint32_t samples) {
  if (samples < 4) return 0;  // the fastest slew, not a jump; see kMaxSlewRate
  uint32_t leading_zeros = __builtin_clz(samples);
  uint32_t integer_bits = 31 - leading_zeros;
  uint32_t mantissa_frac_q5_27 =
      ((samples << leading_zeros) & 0x7FFFFFFFu) >> 4;
  uint32_t log2_q5_27 = (integer_bits << 27) + mantissa_frac_q5_27;
  if (log2_q5_27 <= kSlewTimesPerStageLog2_q5_27) return 0;
  return std::min(
      log2_q5_27 - kSlewTimesPerStageLog2_q5_27, kMaxSlewTimeLog2_q5_27);
}

// Slew rate 2^-slew_time, Q31; defined below.
static inline int32_t SlewRateFromTimeLog2_q31(uint32_t slew_time_log2_q5_27);

// How much of a +/- chiff input on the slew input survives to the slew's
// output, ~Q15.5 (46341 == 1.0). Three sigma of the wandering the slew settles
// to, clamped at 1.0 -- it cannot realize more than it is given.
//
// TAKEN FROM THE SLEW TIME, which is the only encoding stored, so 2^(-t/2) is
// sqrt(rate) for free through the same exp2 table the rate itself comes from:
//   response = min(1, 1.5 * 2^(-t/2))
// The exact form is min(1, 3*sqrt(r/(2*(2-r)))), which needed an integer sqrt
// AND a 64-bit division -- 39 instructions plus a loop, ESTIMATED 250-400
// cycles, once or twice EVERY RUN. This is about ten.
//
// approx/exact is exactly sqrt((2-r)/2), so the shortfall is CORRECTED by
// multiplying by sqrt(2/(2-r)) = (1 - r/2)^(-1/2), taken to two terms:
//   1 + r/4 + 3r^2/32
// r is root^2, already in hand, so this is two multiplies and no sqrt.
// Residual against the exact form, MEASURED: 0.031 dB at slew time 1.2 and
// better everywhere slower -- against 0.87 dB uncorrected.
//
// THE UNCORRECTED ERROR WAS NOT A CORNER CASE, which is why this is worth two
// multiplies. Its old note argued the band "is transited in a chiff's first
// moments and never returned to". True, and beside the point: the first
// moments are where the ENERGY is. MEASURED, weighting the error by the chiff
// energy emitted at each slew time -- 62% to 97% of it lands where the error
// exceeds 0.3 dB, energy-weighted -0.32 to -0.69 dB at AMOUNT 64..127.
// Judged by TIME it looked negligible; judged by ENERGY it is most of the
// chiff.
static uint32_t ChiffFilterResponseFromTime_q15_5(
    uint32_t slew_time_log2_q5_27) {
  // 2^(-t/2) in Q31, then x1.5 and into Q15.5: 69512 == 1.5 * 2^15.5.
  const uint32_t root_q31 = static_cast<uint32_t>(
    SlewRateFromTimeLog2_q31(slew_time_log2_q5_27 >> 1));
  // r = 2^-t = root^2, then 1 + r/4 + 3r^2/32 in Q31.
  const uint64_t rate_q31 =
    (static_cast<uint64_t>(root_q31) * root_q31) >> 31;
  const uint64_t rate_sq_q31 = (rate_q31 * rate_q31) >> 31;
  const uint64_t correction_q31 =
    (1ull << 31) + (rate_q31 >> 2) + ((3ull * rate_sq_q31) >> 5);
  const uint64_t uncorrected_q15_5 =
    (static_cast<uint64_t>(root_q31) * 69512u) >> 31;
  const uint32_t response_q15_5 = static_cast<uint32_t>(
    (uncorrected_q15_5 * correction_q31) >> 31);
  return response_q15_5 > kResponseOne_q15_5
    ? kResponseOne_q15_5 : response_q15_5;
}


// Defined below (Hacker's Delight divlu); used by Rescale.
static uint32_t DivU64ByU32(uint32_t hi, uint32_t lo, uint32_t divisor);

// Defined below; chiff window in samples, scaled off the attack duration.

// log2(x) in Q5.27 for x >= 1: integer bits plus a linear mantissa fraction,
// the same approximation SlewTimeLog2FromDuration uses. Cold path.
static uint32_t Log2_q5_27(uint64_t x) {
  const uint32_t leading = __builtin_clzll(x);
  const uint32_t integer_bits = 63 - leading;
  const uint64_t shifted = x << leading;
  const uint32_t mantissa_frac_q5_27 =
      static_cast<uint32_t>((shifted & 0x7FFFFFFFFFFFFFFFull) >> 36);
  return (integer_bits << 27) + mantissa_frac_q5_27;
}

// below 2^-13 of full scale the chiff is inaudible (-78 dBFS).
const uint32_t kChiffInaudibleShift = 13;

// octaves the chiff input must shrink for the OUTPUT to reach
// inaudibility, given the slew time the chiff will have at the deadline.
// Q5.27. Adapts to the target (via that rate) and to the note's range (via the
// chiff input), which is what a constant octave count cannot do.
//
// PASS THE CHIFF'S OWN SLEW TIME AT THE DEADLINE, never the stage's: the output
// is chiff input x the response at the chiff's own rate, and nothing about the
// stage enters it.
// Passing min(chiff end, stage) instead cost 5 octaves on the COMPRESSION path.
// A release compresses the deadline without re-sloping how fast the filter
// slows, so the chiff is still fast when the deadline arrives while the release
// stage's own slew time is slow; sizing against the stage assumed an output 32x
// smaller than the one produced, the shrink stopped ~34 dB short, and the
// remainder was chopped off at the deadline.
static uint32_t ChiffInputFractionOctaves_q5_27(
    int32_t input_q30, uint32_t end_slew_time_log2_q5_27) {
  const uint32_t response_q15_5 =
    ChiffFilterResponseFromTime_q15_5(end_slew_time_log2_q5_27);
  const uint64_t reached = static_cast<uint64_t>(input_q30)
    * (response_q15_5 ? response_q15_5 : 1u);
  const uint64_t inaudible =
    (1ull << (30 - kChiffInaudibleShift)) * kResponseOne_q15_5;
  return reached > inaudible
    ? Log2_q5_27(reached) - Log2_q5_27(inaudible) : 0u;
}

static uint32_t ChiffWindowSamples(
  uint32_t attack_increment_u32, uint8_t chiff_duration);

// AMOUNT places the chiff's START slew time, interpolating between the END
// slew time (no motion at all) and the fastest the slew can run (most motion).
// It does NOT scale the chiff input: low amounts are quiet because a slow
// slew realizes less of the same chiff input.
//
// Anchoring the slow side on the END is what keeps the start a fixed
// PROPORTION of the window. An absolute anchor put the start at a quarter of
// a 30ms window, so the chiff spent its whole duration still rising and was
// then truncated -- a click, not a chiff. Interpolating from the end also
// makes start <= end by construction, so no clamp is needed.
//
// Warped by lut_env_expo (1 - e^-4x), normalized so AMOUNT max lands exactly
// on the fastest slew time (the raw table tops out just short).
static uint32_t ChiffStartSlewTimeLog2_q5_27(
    uint8_t chiff_amount, uint32_t end_slew_time_log2_q5_27) {
  const uint32_t kFastestSlewTimeLog2_q5_27 = 1u << 27;
  // Window too short for the slew to move at all: start where it ends.
  if (end_slew_time_log2_q5_27 <= kFastestSlewTimeLog2_q5_27) {
    return end_slew_time_log2_q5_27;
  }
  const uint32_t kWarpStep = (LUT_ENV_EXPO_SIZE - 1) >> kChiffAmountBits;
  const uint32_t warp_max_u16 = lut_env_expo[kChiffAmountMax * kWarpStep];
  const uint32_t warp_u16 =
    (static_cast<uint32_t>(lut_env_expo[chiff_amount * kWarpStep]) << 16)
      / warp_max_u16;
  return end_slew_time_log2_q5_27 - static_cast<uint32_t>(
    (static_cast<uint64_t>(
       end_slew_time_log2_q5_27 - kFastestSlewTimeLog2_q5_27) * warp_u16) >> 16);
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
  clamp_base_q30_ = std::min<int32_t>(chiff_floor_q30_, 0);
  // half the note's ALLOWED range, in the stage targets' Q30
  // domain (a target is s16 << 15, so half the range is |scale| << 14). The
  // range may be numerically inverted, hence the magnitude.
  chiff_input_full_q30_ =
    (scale_s16 < 0 ? -static_cast<int32_t>(scale_s16) : scale_s16) << 14;

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
      // (slew time and rate) is set up for the attack; the slewed
      // value carries across a retrigger for continuity.
      Trigger(ENV_STAGE_ATTACK);
      // The chiff window is a MULTIPLE of the attack, set by CHIFF DURATION
      // (center = 1x the attack, +-kChiffOctaves octaves across the range).
      uint32_t window_samples =
        ChiffWindowSamples(adsr.attack_u32, chiff_duration);
      // The nominal duration is a SIZING REFERENCE, not a countdown: it sets
      // how fast the chiff input shrinks and how fast the slew slows, and
      // nothing observes it elapsing. AMOUNT 0 arms nothing, which is the one
      // case that still degenerates to the classic slew by construction.
      chiff_target_samples_ = chiff_amount ? window_samples : 0;
      if (!chiff_target_samples_) {
        chiff_input_fraction_q30_ = 0;
        RederiveSlewState();
        break;
      }
      // The slew time moves from a start set by AMOUNT to an end set by the
      // window; how far it travels is the whole audible effect. The end is
      // computed first because AMOUNT interpolates the start from it.
      chiff_slew_time_log2_end_q5_27_ = SlewTimeLog2FromDuration_q5_27(window_samples);
      slew_time_log2_q5_27_ = ChiffStartSlewTimeLog2_q5_27(
        chiff_amount, chiff_slew_time_log2_end_q5_27_);
      // the chiff input is HALF THE NOTE'S ALLOWED RANGE, times
      // the shrink. Half the range is the largest symmetric +/- that can ever
      // fit inside the range -- a bound, not a tuned fraction, which is what
      // the old 0.9 x range was. Sized from the ALLOWED range rather than the
      // realized one so a quiet note gets the same exciter as a loud one.
      chiff_input_fraction_q30_ = 1 << 30;
      // size the shrink so the OUTPUT reaches the inaudibility
      // threshold exactly at the target, whatever the target and whatever the
      // note's range. Output at the target, with no shrink, would be
      // chiff input * the slew's response at the END rate; the octaves needed
      // are log2 of that over the threshold. Adapts where a constant cannot: a
      // long target needs fewer octaves (its slew has already done more of the
      // work), a short target more, a quiet note fewer.
      //
      // Sized to reach the max slew time at the nominal duration, so the slew
      // time at that moment IS that end.
      chiff_input_fraction_step_q5_27_ =
        ChiffInputFractionOctaves_q5_27(chiff_input_full_q30_,
          chiff_slew_time_log2_end_q5_27_)
        / window_samples;
      // Set ONCE, here, so the slow-down finishes at the nominal duration
      // however many stages the note passes through.
      chiff_slew_time_log2_step_q5_27_ =
        (ChiffMaxSlewTime_q5_27() - slew_time_log2_q5_27_) / window_samples;
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

// THE FASTEST THE SLEW MAY RUN: 1 - e^-1, the true one-pole coefficient for a
// time constant of one sample.
//
// rate = 2^-t is the small-rate approximation of the true coefficient
// 1 - e^(-1/tau). It is exact enough everywhere the module runs -- MEASURED
// error 0.0% at slew time 13, +0.5% at 6.7, +2.5% at 4.25 -- and it hits its
// ceiling at t = 0, where it says 1.0 while the truth is 0.632. A rate of 1.0
// is not a slew at all: the value arrives in ONE sample. Capping the rate is
// what removes the need for a "stage too short to slew" special case.
//
// Capping here removes the special case instead, and lands the short stage
// exactly where every other stage lands: 1 - 0.632 IS e^-1, so a 4-sample
// stage covers 1 - (1 - 0.632)^4 = 1 - e^-4 = 98.17%, the same four time
// constants as the rest. 4 samples is also the SHORTEST STAGE THAT EXISTS --
// modulate_7_13 clamps its result to [0, 8191] so the increment table cannot
// be indexed below entry 0, and that entry is UINT32_MAX/4 -- so nothing falls
// off the bottom of this.
//
// NOT applied inside SlewRateFromTimeLog2_q31 itself: that is a general 2^-x,
// and its other callers (the centre blend's ratio, the chiff input shrink,
// and 2^(-t/2) in the response) all need to reach 1.0.
const int32_t kMaxSlewRate_q31 = 1357468564;  // round((1 - e^-1) * 2^31)

static inline int32_t SlewRateFromSlewTime_q31(uint32_t slew_time_log2_q5_27) {
  const int32_t rate_q31 = SlewRateFromTimeLog2_q31(slew_time_log2_q5_27);
  return rate_q31 > kMaxSlewRate_q31 ? kMaxSlewRate_q31 : rate_q31;
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
  if (chiff_target_samples_) {
    // THE STEP IS NOT RECOMPUTED HERE. It is set once, in NoteOn, from the
    // nominal duration. Respreading the remaining octaves over the FULL duration
    // at every stage handoff made the chiff's schedule depend on how many stage
    // transitions it lived through and when they fell -- stage timing driving
    // chiff timing after the duration was decided. Only the release compression
    // may shorten it, and it does that by taking a FASTER step, below.
    const uint32_t max_slew_time_q5_27 = ChiffMaxSlewTime_q5_27();
    if (slew_time_log2_q5_27_ > max_slew_time_q5_27) {
      slew_time_log2_q5_27_ = max_slew_time_q5_27;
    }
    chiff_slew_rate_decay_q32_ = DecayFromIncrement_q32(chiff_slew_time_log2_step_q5_27_);
  } else {
    slew_time_log2_q5_27_ = stage_slew_time_log2_q5_27_;
    chiff_slew_time_log2_step_q5_27_ = 0;
    chiff_slew_rate_decay_q32_ = 0;
  }
}

// Update current stage and its state. The slew always moves from the current
// value toward the stage target at a rate set by the stage's nominal
// duration, so there is no nominal-vs-actual delta bookkeeping: starting
// closer to the target just means arriving (proportionally) closer to it
// when the stage's sample countdown expires. The nominal value
// carries across the transition untouched -- the chiff needs no anchor
// bookkeeping; the chiff input is applied about wherever the nominal value goes.
void Envelope::Trigger(EnvelopeStage stage) {
  // Anchor the new stage's start on where the leaving stage's NOMINAL VALUE
  // reached: with the chiff
  // off the value is the exact classic slew, so use it directly; a timed
  // stage's nominal value is closed-form from its phase (the same lut_env_expo
  // curve the slew traces); a hold's has converged to its target.
  if (!chiff_input_fraction_q30_) {
    stage_start_q30_ = nominal_q30_;
  } else if (phase_increment_u32_) {
    // Phase runs 0 -> ~UINT32_MAX across the stage, but a stage that ran to
    // completion leaves stage_samples_left_ == 0, which WRAPS the product back
    // to phase 0 -- aliasing "fully elapsed" onto "not started" and anchoring
    // the new stage at the old stage's START instead of where it landed. That
    // collapses the next stage's nominal value (and yanks the value with it)
    // whenever the chiff is still live at a handoff. Saturate instead.
    uint32_t phase_u32 = stage_samples_left_
      ? 0u - stage_samples_left_ * phase_increment_u32_
      : UINT32_MAX;
    // No landing fraction: the slew aims past its target, so lut_env_expo's own
    // normalization already describes where the value is.
    uint32_t expo_u16 = Interpolate824(lut_env_expo, phase_u32);
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
      ? 0 // Floor, not a special case: slew time 0 is one sample per time
          // constant, the fastest the slew runs (see kMaxSlewRate_q31). The
          // subtraction below is unsigned, so it needs this anyway.
      : std::min(
          log2_stage_samples_q5_27 - kSlewTimesPerStageLog2_q5_27,
          kMaxSlewTimeLog2_q5_27
        );
  }
  // A RELEASE CAN ONLY MAKE THE SHRINK FASTER, NEVER SLOWER. The chiff's own
  // schedule is sovereign -- it is what CHIFF DURATION dials -- but a note
  // that ends before the chiff has gone quiet would leave audible noise with
  // nothing left to produce it, so the release imposes a DEADLINE: reach
  // inaudibility by the end of this stage.
  //
  // Expressed as octaves rather than as a countdown, which is what lets the
  // window go: ask how many octaves are still owed at the stage's end rate,
  // divide by the samples available, and take that step only if it is FASTER
  // than the one already running. A short release therefore compresses the
  // shrink; a long one changes nothing.
  // Re-derive slew coefficients for the new stage, then let a release shorten
  // the chiff's remaining time (below) -- in that order, because that work
  // adjusts what RederiveSlewState just computed.
  RederiveSlewState();
  if (stage == ENV_STAGE_RELEASE && stage_samples_left_ && chiff_target_samples_) {
    // BOTH mechanisms get the same deadline. Speeding up only the chiff input
    // leaves the note ending with the slew still running at chiff speed, and
    // the ~2% of the stage's span that a slew has left at handoff is then
    // consumed in a fraction of a millisecond instead of gliding away over the
    // stage's own time constant -- a click at the end of every note. MEASURED
    // before this was added: a burst to -53 dBFS at the release/DEAD boundary
    // on a long chiff.
    const uint32_t fraction_step_q5_27 =
      ChiffInputFractionOctaves_q5_27(ChiffInput_q30(),
        ChiffSlewTimeAtDeadline_q5_27(stage_samples_left_))
      / stage_samples_left_;
    if (fraction_step_q5_27 > chiff_input_fraction_step_q5_27_) {
      chiff_input_fraction_step_q5_27_ = fraction_step_q5_27;
    }
    const uint32_t max_slew_time_q5_27 = ChiffMaxSlewTime_q5_27();
    if (max_slew_time_q5_27 > slew_time_log2_q5_27_) {
      const uint32_t slew_time_step_q5_27 =
        (max_slew_time_q5_27 - slew_time_log2_q5_27_) / stage_samples_left_;
      if (slew_time_step_q5_27 > chiff_slew_time_log2_step_q5_27_) {
        chiff_slew_time_log2_step_q5_27_ = slew_time_step_q5_27;
        chiff_slew_rate_decay_q32_ =
          DecayFromIncrement_q32(chiff_slew_time_log2_step_q5_27_);
      }
    }
  }
}

void Envelope::RenderSamples(int16_t* sample_buffer, int32_t bias_target_q31) {
  // Bias is unaffected by stage change, thus has distinct lifecycle from other locals
  const int32_t bias_slope_q31 = ((bias_target_q31 >> 1) - (bias_q31_ >> 1)) >> (kAudioBlockSizeBits - 1);
  RenderStage(sample_buffer, kAudioBlockSize, bias_q31_, bias_slope_q31);
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
  int32_t nominal_q30 = nominal_q30_;
  int32_t chiff_state_q30 = chiff_state_q30_;

  // One straight run, bounded by the block, the stage countdown, and (while
  // live) the chiff window. Whichever expires hands off or re-enters -- once,
  // not re-checked per sample.
  const bool timed = phase_increment_u32_ != 0;
  // NO CHIFF ON/OFF ANYWHERE IN HERE. There is no window to be inside of and no
  // mode to be in: with AMOUNT 0 the shrink is zero, so the chiff input is
  // zero, the slew stops slowing and the rate is the stage's -- and every line
  // below degenerates to the classic slew on its own. Branching on it would
  // only make the BEST case cheaper, which is worth nothing here; the worst
  // case is a live chiff and it pays this cost either way.
  uint32_t run_samples = block_samples_left;
  if (timed) run_samples = std::min<uint32_t>(run_samples, stage_samples_left_);
  int16_t* const segment_end = sample_buffer + run_samples;
  const int32_t stage_target_q30 = target_q30_;

  {
    // BIAS IS A TERMINAL ADD. The loop's state is the envelope alone; bias is
    // added to a scratch copy on the way to the buffer, so the output is
    // saturate(envelope + bias) and the envelope's own trajectory is the same
    // whatever the bias does. Folding bias into the integrator instead (the
    // state clamp then bounding the sum) let the clamp write the bias back into
    // the envelope: at rest under a negative bias the state pinned at 0 and the
    // envelope came back at +bias. MEASURED before the split, against a bias-0
    // run at the same settings: the value diverged by exactly the bias
    // amplitude (10, 8000, 20000 s16) every time the sum touched a rail.
    const int32_t bias_q30 = bias_q31 >> 1;
    // THE CLAMP RIDES THE NOTE'S FLOOR. USAT bounds [0, 2^30) and nothing else,
    // so to bound a value whose range goes BELOW zero the loop runs on the
    // value measured FROM that floor. Subtracted here, added back after the
    // loop, and absorbed into the bias register for the output -- all per run,
    // so the loop is untouched and this costs NOTHING per sample.
    //
    // min(floor, 0), not floor: where the note's floor is already >= 0 the
    // offset is zero and the bound is exactly what it was, so the common case
    // cannot move (every bias-0 scenario hash is unchanged). Only a range that
    // reaches below zero shifts it, and then floor..floor+2^30 covers the
    // note's whole range because a range is at most full scale wide.
    // Slew-rate floor (timed stages): never slower than the stage's own
    // rate, else the value hangs on a moving stage near the window's slow
    // end. Monotone (the rate only falls), so flooring holds it there.
    int32_t decay_q32 = chiff_slew_rate_decay_q32_;
    // The floor exists to TRACK the nominal value, not to energize the chiff:
    // floored, a full chiff input would ride the stage rate and AMOUNT 1
    // would sound like attack-speed noise (a 0 -> 1 discontinuity). Shrinking
    // the chiff input by the response ratio keeps the output continuous
    // across the floor boundary, and sends AMOUNT -> 0 to silence smoothly.
    //
    // Both the floor and the scale are PER-RUN scratch: neither may be
    // written back into the persistent state. The chiff's own rate keeps
    // ramping on its own schedule (recovered from the slew time below), and
    // the chiff input keeps shrinking linearly -- persisting either compounds
    // it every block and drives the chiff input to zero in a few blocks.
    //
    // The chiff's response is the expensive term in this function (a sqrt and
    // a divide), computed once here: the mean clamp needs it to know how much
    // room to leave.
    // ONE ENCODING OF THE SLEW IS STORED -- the slew TIME -- and the rate is
    // derived here, once, for the loop to run on. They are the same quantity
    // (rate = 2^-time), and keeping both as state meant keeping two
    // accumulators for it: the loop decayed the rate per sample while the
    // writeback raised the time per run, and they were reconciled only when
    // the floor bound. Derived, the rate cannot drift from the schedule.
    uint32_t slew_time_q5_27 = slew_time_log2_q5_27_;
    int32_t slew_rate_q31 = SlewRateFromSlewTime_q31(slew_time_q5_27);
    const uint32_t chiff_response_q15_5 =
      ChiffFilterResponseFromTime_q15_5(slew_time_q5_27);
    // NO SLEW-RATE FLOOR, and no chiff input rescale at it. Both existed
    // because ONE slew had to track the nominal level AND carry the chiff: if
    // the chiff's rate went below the stage's, the level stopped tracking. The
    // nominal has its own one-pole now, so the chiff's rate is free to fall as
    // far as it likes -- which is the low-pass gate the asymptotic design
    // wanted and could not have while the filter was shared.
    // WHAT NOMINAL CHASES: past the target by 1/(1 - e^-4), so it arrives ON
    // the target as the stage's countdown expires. Holds chase the target.
    int32_t stage_aim_q30 = stage_target_q30;
    if (timed) {
      stage_aim_q30 = stage_start_q30_ + static_cast<int32_t>(
        (static_cast<int64_t>(stage_target_q30 - stage_start_q30_) *
         kStageAimOvershoot_u16) >> 16);
    }
    // NOT SMMLA, which would make each one-pole two instructions instead of
    // four: SMMLA is the ARMv7E-M DSP extension (Cortex-M4). The assembler
    // rejects it for -mcpu=cortex-m3, which is what this builds for.
    const int32_t stage_rate_q31 =
      SlewRateFromSlewTime_q31(stage_slew_time_log2_q5_27_);
    const int32_t input_q30 = ChiffInput_q30();
    const int32_t chiff_input_q30 = input_q30;
    // The chiff's output RMS: its input times the filter's response. Dividing
    // by 2^15.5 would be a 64-bit division, so multiply by the same constant
    // and shift 31 (46341^2 is 2^31 to 3 parts per million).
    // APPROXIMATE, and low by up to 0.87 dB in a band of slew times the chiff
    // transits early (see ChiffFilterResponseFromTime). The margin below
    // inherits that error where the chiff is loudest.
    // Computed once per RUN from the run-start slew time, while the rate decays
    // within the run -- so it runs GENEROUS as the run proceeds, which is safe.
    const int32_t chiff_rms_q30 = static_cast<int32_t>(
      (static_cast<int64_t>(input_q30)
       * (chiff_response_q15_5 * kResponseOne_q15_5)) >> 31);
    // THE MEAN IS HELD ONE CHIFF RMS INSIDE EACH RAIL; the chiff is then added,
    // so it has room by construction instead of being clipped.
    //  - the clamp does NOT feed back, which is why bias may be part of it.
    //    Steering the slew input instead waits on the integrator: MEASURED, a
    //    bias LFO at 364 ms left 6641 LSB of output error.
    //  - only the OFFSET is ramped across the run. nominal is an exponential and
    //    a linear chord over 64 samples is percent-level wrong on a 409-sample
    //    stage -- envelope distortion, not rounding.
    //  - EXCEPT when the chiff is wider than the rails allow (lo >= hi): the
    //    clamp is abandoned and the mean is centred instead, so the chiff clips
    //    both sides. A larger factor reaches that regime sooner.
    //  - ONE rms is inherited, not chosen: it is what the response returns.
    //    MEASURED AT ONE SETTING ONLY (peak at full scale, steady bias 8000),
    //    so treat as indicative, not established: peaks ~4.3x rms, ~1.6% of the
    //    loud phase clips at the rail, and 2x rms would cost ~1.6 dB of attack.
    const int32_t bias_slope_q30 = bias_slope_q31 >> 1;
    const int32_t lo_q30 = chiff_rms_q30;
    const int32_t hi_q30 = kValueMax_q30 - chiff_rms_q30;
    // Where nominal reaches by the run's end, for the offset's far endpoint.
    // Approximate (linear in rate * run_samples) -- it only sizes an offset
    // that is itself an approximation, and it never touches nominal's own path.
    int32_t nominal_end_q30 = nominal_q30;
    {
      const int32_t gap_q30 = stage_aim_q30 - nominal_q30;
      int64_t step = ((static_cast<int64_t>(gap_q30) * stage_rate_q31) >> 31)
        * static_cast<int32_t>(run_samples);
      if ((gap_q30 >= 0 && step > gap_q30) || (gap_q30 < 0 && step < gap_q30)) {
        step = gap_q30;
      }
      nominal_end_q30 += static_cast<int32_t>(step);
    }
    const int32_t bias_end_q30 =
      bias_q30 + bias_slope_q30 * static_cast<int32_t>(run_samples);
    int32_t combined_q30, combined_end_q30;
    if (lo_q30 < hi_q30) {
      combined_q30 = ClampOffset(nominal_q30 + bias_q30, lo_q30, hi_q30)
        + bias_q30;
      combined_end_q30 =
        ClampOffset(nominal_end_q30 + bias_end_q30, lo_q30, hi_q30)
        + bias_end_q30;
    } else {
      // Chiff wider than the rails: centre it and let the output saturate.
      combined_q30 = (kValueMax_q30 >> 1) - nominal_q30;
      combined_end_q30 = (kValueMax_q30 >> 1) - nominal_end_q30;
    }
    const int32_t combined_slope_q30 = run_samples
      ? (combined_end_q30 - combined_q30) / static_cast<int32_t>(run_samples)
      : 0;
    // TRACK THE GAP TO THE AIM, NOT THE VALUE. A one-pole on the value is
    // sub/smull/add; the same motion on the gap is a pure geometric decay,
    // smull/sub -- the shape the rate decay above already uses. The aim folds
    // into the offset register, so the output is one subtract either way.
    int32_t nominal_gap_q30 = stage_aim_q30 - nominal_q30;
    combined_q30 += stage_aim_q30;

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
    // HAND-ALLOCATED loop. GCC 4.8 allocates this badly and spills; presenting
    // every live value as an operand pins them. The behaviour is the C loop in
    // #else (the host reference), and the QEMU differential proves the two
    // bit-identical.
    //
    // Per sample: two independent one-poles. The chiff's chases +/- the
    // chiff input at its own decaying rate; nominal chases the stage's aim at
    // the STAGE's rate. The offset carrying bias and the mean correction ramps.
    // One USAT saturates and shifts in one instruction.
    __asm__ volatile(
      "  cmp   %[buf], %[end]\n"
      "  beq   2f\n"
      "1:\n"
      "  smull ip, lr, %[rate], %[decay]\n"       // (rate*decay), lr = hi
      "  sub   %[rate], %[rate], lr\n"            // rate -= (rate*decay)>>32
      "  ldr   ip, [%[prng]], #4\n"               // draw = *prng++
      "  sbfx  ip, ip, #16, #1\n"                 // sign mask from bit 16
      "  eor   lr, %[input], ip\n"
      "  sub   lr, lr, ip\n"                      // +/- chiff input
      "  sub   lr, lr, %[chiff]\n"                // delta = input - chiff
      "  smull ip, lr, lr, %[rate]\n"
      "  add   %[chiff], %[chiff], lr, lsl #1\n"  // chiff += (product>>32)*2
      "  smull ip, lr, %[gap], %[srate]\n"        // the gap to the aim decays
      "  sub   %[gap], %[gap], lr, lsl #1\n"      //   at the STAGE's rate
      "  add   %[comb], %[comb], %[cslope]\n"     // bias + mean + the aim
      "  sub   ip, %[comb], %[gap]\n"             // the mean
      "  add   ip, ip, %[chiff]\n"                // + the chiff
      "  usat  ip, #15, ip, asr #15\n"            // saturate and shift, one op
      "  strh  ip, [%[buf]], #2\n"
      "  cmp   %[buf], %[end]\n"
      "  bne   1b\n"
      "2:\n"
      : [chiff] "+r"(chiff_state_q30), [gap] "+r"(nominal_gap_q30),
        [rate] "+r"(slew_rate_q31), [comb] "+r"(combined_q30),
        [prng] "+r"(prng), [buf] "+r"(sample_buffer)
      : [decay] "r"(decay_q32), [input] "r"(chiff_input_q30),
        [srate] "r"(stage_rate_q31),
        [cslope] "r"(combined_slope_q30), [end] "r"(segment_end)
      : "ip", "lr", "cc", "memory");
#else
    while (sample_buffer != segment_end) {
      // PRNG budget: bit 16 = chiff input sign.
      uint32_t chiff_draw_u32 = *prng++;
      int32_t slew_rate_step = static_cast<int32_t>(
        (static_cast<int64_t>(slew_rate_q31) * decay_q32) >> 32);
      slew_rate_q31 -= slew_rate_step;
      int32_t sign_mask = static_cast<int32_t>(chiff_draw_u32 << 15) >> 31;
      // +/- the chiff input, branchless: (p ^ mask) - mask.
      int32_t chiff_input_signed_q30 =
        (chiff_input_q30 ^ sign_mask) - sign_mask;
      // (delta * rate) >> 32, DOUBLED -- i.e. the high word only, no low-word
      // term. Two instructions saved per one-pole. The dropped bit is a half
      // LSB per sample and cannot accumulate: at a one-pole's fixed point the
      // step is zero, so the error is bounded by the last step, not summed.
      chiff_state_q30 += 2 * static_cast<int32_t>(
        (static_cast<int64_t>(chiff_input_signed_q30 - chiff_state_q30)
         * slew_rate_q31) >> 32);
      nominal_gap_q30 -= 2 * static_cast<int32_t>(
        (static_cast<int64_t>(nominal_gap_q30) * stage_rate_q31) >> 32);
      combined_q30 += combined_slope_q30;
      // Matches USAT #15 with ASR #15: arithmetic shift, then saturate.
      int32_t sample = (combined_q30 - nominal_gap_q30 + chiff_state_q30)
        >> kSampleBits;
      if (sample < 0) sample = 0;
      if (sample > INT16_MAX) sample = INT16_MAX;
      *sample_buffer++ = static_cast<int16_t>(sample);
    }
#endif
    {
      // THE CHIFF INPUT SHRINKS FOREVER AND NEVER REACHES ZERO -- until Q30
      // runs out of bits, which is the only ending there is. Nothing mutes it,
      // nothing resets the slew: the term simply becomes too small to
      // represent, and the loop it feeds is already the classic slew with a
      // zero offset. That is the low-pass gate, and it is why no window exists
      // here -- nothing has to close.
      chiff_input_fraction_q30_ = static_cast<int32_t>(
        (static_cast<int64_t>(chiff_input_fraction_q30_) * SlewRateFromTimeLog2_q31(
           chiff_input_fraction_step_q5_27_ * run_samples))
        >> 31);
      uint32_t slew_time_log2_end =
        slew_time_log2_q5_27_ + chiff_slew_time_log2_step_q5_27_ * run_samples;
      const uint32_t max_slew_time_q5_27 = ChiffMaxSlewTime_q5_27();
      if (slew_time_log2_end > max_slew_time_q5_27) {
        slew_time_log2_end = max_slew_time_q5_27;
        // As slow as it goes: stop decaying the rate, or the loop keeps taking it
        // below the end it was told to stop at.
        chiff_slew_rate_decay_q32_ = 0;
      }
      // Nothing to reconcile: the next run derives its rate from this time.
      slew_time_log2_q5_27_ = slew_time_log2_end;
    }

    // value_q30_ is the realized envelope -- nominal plus the chiff -- and it
    // carries NO bias, so the consumers that read it (value(), tremolo(), the
    // next stage's start) see the same trajectory whatever the bias does.
    nominal_q30 = stage_aim_q30 - nominal_gap_q30;
    nominal_q30_ = nominal_q30;
    chiff_state_q30_ = chiff_state_q30;
    // Bounded to the note's own DAC range before anyone reads it. Nothing in
    // the render needs this -- neither one-pole integrates it, so there is no
    // windup to prevent -- but value() returns int16_t and tremolo() forms
    // (value - release target) * strength_u16 in int32, and both wrap on an
    // out-of-range value. MEASURED unbounded: -10014..33092, and 33092 * 65535
    // is 2.17e9 against INT32_MAX 2.147e9. Bias-free, so the invariant holds.
    value_q30 = nominal_q30 + chiff_state_q30;
    if (value_q30 < clamp_base_q30_) value_q30 = clamp_base_q30_;
    const int32_t value_top_q30 = clamp_base_q30_ + kValueMax_q30;
    if (value_q30 > value_top_q30) value_q30 = value_top_q30;
    bias_q31 += bias_slope_q31 * static_cast<int32_t>(run_samples);
  }

  block_samples_left -= run_samples;
  value_q30_ = value_q30;
  bias_q31_ = bias_q31;

  if (timed) {
    stage_samples_left_ -= run_samples;
    if (stage_samples_left_ == 0) {
      // Countdown expired: hand off to the next stage. Tail call stays flat.
      return HandOffToNextStage(
        sample_buffer, block_samples_left, bias_q31, bias_slope_q31);
    }
  }
  if (block_samples_left) {
    // Samples left with no stage handoff: re-enter for the remainder.
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
  // The shrink and its step are DIMENSIONLESS -- a fraction of the slack, and
  // octaves per sample -- so they do not scale with the levels. The rails do,
  // and the chiff input follows them because it is derived from the slack.
  chiff_input_full_q30_ = ScaleRatio(chiff_input_full_q30_, num, den);
  chiff_floor_q30_ = ScaleRatio(chiff_floor_q30_, num, den);
  chiff_top_q30_ = ScaleRatio(chiff_top_q30_, num, den);
  clamp_base_q30_ = std::min<int32_t>(chiff_floor_q30_, 0);
  for (int i = 0; i < ENV_NUM_STAGES; ++i) {
    stage_target_q30_[i] = ScaleRatio(stage_target_q30_[i], num, den);
  }
}

}  // namespace yarns
