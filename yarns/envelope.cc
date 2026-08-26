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

// The chiff's draws, and the generator that makes them. Each envelope has its
// own sequence; instances must not share one.
namespace {
  // Sixteen levels, for two reasons:
  //   - A two-level input's output is a square once the rate reaches 1, so
  //     "unslewed" and "overdriven" collide and the drive has nothing to
  //     shape. Sixteen makes the unslewed end midpoint noise, whose
  //     rms is kChiffDrawRmsFractionOfMax of a square's, and that fraction is
  //     what the drive reclaims.
  //   - Four bits divide a 32-bit word evenly, so the chunking below stays a
  //     shift and a mask. Asserted, not assumed.
  const uint32_t kChiffDrawBits = 4;
  // The word the draws are packed into. xorshift32 pins the width at 32: its
  // shift constants are only valid there.
  typedef uint32_t ChiffDrawWord;
  const uint32_t kChiffDrawsPerWord = 32 / kChiffDrawBits;
  // A draw may not straddle a word: both loops extract one with a single ubfx
  // at a compile-time offset. Negative array size because this is pre-C++11.
  typedef char kChiffDrawBitsMustDivideTheWord[
      (32 % kChiffDrawBits == 0) ? 1 : -1];
  // Levels are the odd multiples 2*draw - max, i.e. +/-1, +/-3 ... +/-max, so
  // the set is symmetric about zero: the chiff is zero-mean and the clip is
  // symmetric about what it clips.
  const int32_t kChiffDrawValueMax = (1 << kChiffDrawBits) - 1;
  // The draw set's rms over its largest value: sqrt((4n^2 - 1)/3)/(2n - 1) for
  // n magnitudes, i.e. sqrt(85)/15 = 0.6146 at four bits, which is -4.23 dB.
  // DERIVED from the draw width, so widening a draw moves both figures.
  const uint32_t kChiffNumDrawMagnitudes = 1u << (kChiffDrawBits - 1);
  const double kChiffDrawRmsFractionOfMax =
    __builtin_sqrt(
      (4.0 * kChiffNumDrawMagnitudes * kChiffNumDrawMagnitudes - 1.0) / 3.0)
      / (2.0 * kChiffNumDrawMagnitudes - 1.0);
  typedef char kChiffDrawsMustFillWholeWords[
      (kAudioBlockSize % kChiffDrawsPerWord == 0) ? 1 : -1];

  // xorshift32. Zero is a fixed point, which is why the seeder below sets the
  // low bit.
  inline ChiffDrawWord NextChiffDraws(ChiffDrawWord state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  }

  // Distinct seeds for distinct sequences. xorshift32 has one orbit, so seeds
  // are phases of a single stream and near seeds start near each other -- hence
  // a large odd stride.
  const uint32_t kChiffSeedStride = 2654435761u;  // round(2^32 / golden ratio)
  uint32_t next_chiff_seed = 0xCAFEBABE;
}  // namespace


// The output sample is the s16 range, which USAT #15 states directly.
const int kSampleBits = 15;
// USAT's width: an unsigned saturate to 15 bits is the C reference's clamp to
// [0, INT16_MAX]. Named so the asm can take it as an immediate.
const int kOutputSaturateBits = 15;

// The DAC range in Q30: 32767 << 15, and (2^30 - 1) >> 15 is 32767 exactly.
const int32_t kValueMax_q30 = (1 << 30) - 1;

// How far the mean must move so the chiff's amplitude fits between it and the
// rails; 0 when it already does.
//
// Q29 because the mean is nominal + bias, which passes INT32_MAX at a
// full-range note with a railed bias. The half LSB that costs lands on a DC
// offset. The answer can pass INT32_MAX too, so it is formed unsigned: it is
// only ever added into the modular accumulator.
inline uint32_t OffsetForChiffAmplitude(
    int32_t mean_q29, int32_t min_q29, int32_t max_q29) {
  if (mean_q29 < min_q29) {
    return (static_cast<uint32_t>(min_q29) - static_cast<uint32_t>(mean_q29)) << 1;
  }
  if (mean_q29 > max_q29) {
    return (static_cast<uint32_t>(max_q29) - static_cast<uint32_t>(mean_q29)) << 1;
  }
  return 0;
}

// 1.0 in the _q31_sqrt format: scale 2^15.5 = sqrt(2^31), so two values
// multiply and >> 31 to a plain product. Not an int_frac Q format. Exact to
// 3 ppm: 46341^2 = 2^31 + 4633.
const uint32_t kOne_q31_sqrt = static_cast<uint32_t>(
  32768.0 * __builtin_sqrt(2.0) + 0.5);

// lut_env_expo's last entry. Naming it lets ChiffAmountAtPhase_q30 normalise
// by subtraction.
const uint32_t kEnvExpoFull_u16 = 65535;

// Number of slew time constants a timed stage spans, as log2 in Q5.27.
// log2(4) = 2: the stage hands off with e^-4 ~= 1.8% of its initial delta
// remaining (absorbed by the next stage's slew). Tunable by ear: larger
// front-loads the curve and lands closer to the target; smaller straightens
// the curve but leaves a bigger residual at handoff.
const uint32_t kSlewTimesPerStageLog2_q5_27 = 2u << 27;

// Caps how slow a slew may get, so the exp2 helper's `>> integer_part` stays
// well-defined. 2^27 samples is ~50 minutes at 45 kHz, already absurd.
const uint32_t kMaxRepresentableSlewTimeLog2_q5_27 = 27u << 27;

// The fractional part of a Q5.27 slew time, i.e. everything below one octave.
const uint32_t kSlewTimeFraction_q5_27 = (1u << 27) - 1;

// AMOUNT reaches the envelope as a fraction of full scale. The panel's step
// count stays in part.cc; the two figures below are the knob positions the
// chiff was calibrated at, as ratios of the 0..127 range they were heard on.
const uint32_t kChiffAmountFractionalBits = 30;
const uint32_t kChiffAmountFull_q30 = 1u << kChiffAmountFractionalBits;

// The chiff's slew state is carried in Q26, not Q30, so the driven input has
// four bits of headroom. Costs nothing: the output add takes a shifted operand
// either way.
const uint32_t kChiffLevelFractionalBits = 30;
const uint32_t kChiffSlewStateFractionalBits = 26;
// Capped.
static inline int32_t SlewRateFromSlewTime_q31(uint32_t slew_time_log2_q5_27);

void Envelope::Init(int16_t zero_value_s16) {
  stage_phase_increment_u32_ = 0;
  stage_samples_left_ = 0;
  stage_slew_rate_q31_ = SlewRateFromSlewTime_q31(0);
  chiff_slew_time_log2_q5_27_ = 0;
  chiff_slew_time_at_amount_zero_q5_27_ = 0;
  chiff_slew_input_fraction_q30_ = 0;
  chiff_slew_input_max_q30_ = 0;
  chiff_amount_initial_q30_ = 0;
  chiff_amount_q30_ = 0;
  chiff_phase_q32_ = 0;
  chiff_phase_step_q32_ = 0;
  // Bias survives NoteOn/NoteOff to stay smooth; only Init resets it.
  // Init does: a reused envelope starts its first block's ramp from zero.
  bias_q31_ = 0;
  int32_t zero_value_q30 = zero_value_s16 << (31 - 16);
  value_without_bias_q30_ = zero_value_q30;
  nominal_value_q30_ = zero_value_q30;
  chiff_slew_state_q26_ = 0;
  stage_start_q30_ = zero_value_q30;
  value_floor_q30_ = std::min<int32_t>(zero_value_q30, 0);
  std::fill(
    &note_target_q30_[0],
    &note_target_q30_[ENV_NUM_STAGES],
    zero_value_q30
  );
  next_chiff_seed += kChiffSeedStride;
  chiff_draws_ = next_chiff_seed | 1u;
  chiff_draws_left_ = kChiffDrawsPerWord;
  Trigger(ENV_STAGE_DEAD);
}

void Envelope::NoteOff() {
  Trigger(ENV_STAGE_RELEASE);
}

// The +/- the slew chases. Sized from the note's allowed range, not the value
// it reaches, so the chiff reaches the same amplitude at every velocity.
int32_t Envelope::ChiffSlewInput_q30() const {
  return static_cast<int32_t>(
    (static_cast<int64_t>(chiff_slew_input_max_q30_) * chiff_slew_input_fraction_q30_)
    >> 30);
}

// Samples -> the slew time that settles within them: log2(samples/4), Q5.27,
// clamped. The /4 is kSlewTimesPerStageLog2. log2 as integer bits plus a
// linear mantissa fraction, max error ~0.09.
static uint32_t ChiffSlewTimeFromSamples_q5_27(uint32_t samples) {
  if (samples < 4) return 0;  // the fastest slew, not a jump; see kMaxSlewRate
  uint32_t leading_zeros = __builtin_clz(samples);
  uint32_t integer_bits = 31 - leading_zeros;
  uint32_t mantissa_frac_q5_27 =
      ((samples << leading_zeros) & 0x7FFFFFFFu) >> 4;
  uint32_t log2_q5_27 = (integer_bits << 27) + mantissa_frac_q5_27;
  if (log2_q5_27 <= kSlewTimesPerStageLog2_q5_27) return 0;
  return std::min(
      log2_q5_27 - kSlewTimesPerStageLog2_q5_27, kMaxRepresentableSlewTimeLog2_q5_27);
}

// Uncapped, unlike SlewRateFromSlewTime_q31.
static inline uint32_t SlewRateFromTimeLog2_q31(uint32_t slew_time_log2_q5_27);

// A noise signal has no amplitude. This is the rule that gives it one.
const double kChiffAmplitudeSigmas = 3.0 / __builtin_sqrt(2.0);   // 2.121

// sigma_out = input_rms * sqrt(r / (2 - r)). Split it: root = 2^(-t/2) is
// applied per run, and this is the rest of the identity at small r. The
// series in ChiffAmplitudeGainAtSlewTime -- 1 / sqrt(1 - r/2) -- restores it
// exactly at all r.
const double kChiffSigmaPerRootAtSmallRate = 1.0 / __builtin_sqrt(2.0);

// amplitude_gain = this * root * series.
const uint32_t kChiffAmplitudeGainCoefficient_q31_sqrt = static_cast<uint32_t>(
  kChiffAmplitudeSigmas * kChiffSigmaPerRootAtSmallRate * kChiffDrawRmsFractionOfMax
      * kOne_q31_sqrt + 0.5);

// The rate is a parameter, so the forward and inverse series read one value
// and stay in step to the low bit.
static uint32_t ChiffAmplitudeGainAtSlewTime_q31_sqrt(
    uint32_t slew_time_log2_q5_27, int32_t rate_q31) {
  // 2^(-t/2) in Q31, then into _q31_sqrt at the coefficient.
  const uint32_t root_q31 = SlewRateFromTimeLog2_q31(slew_time_log2_q5_27 >> 1);
  // 1 + r/4 + 3r^2/32 in Q31.
  const uint64_t r_q31 = static_cast<uint64_t>(static_cast<uint32_t>(rate_q31));
  const uint64_t r_sq_q31 = (r_q31 * r_q31) >> 31;
  const uint64_t correction_q31 =
    (1ull << 31) + (r_q31 >> 2) + ((3ull * r_sq_q31) >> 5);
  const uint64_t uncorrected_q31_sqrt =
    (static_cast<uint64_t>(root_q31) * kChiffAmplitudeGainCoefficient_q31_sqrt) >> 31;
  const uint32_t amplitude_gain_q31_sqrt = static_cast<uint32_t>(
    (uncorrected_q31_sqrt * correction_q31) >> 31);
  return amplitude_gain_q31_sqrt > kOne_q31_sqrt ? kOne_q31_sqrt : amplitude_gain_q31_sqrt;
}

static uint32_t DivU64ByU32(uint32_t hi, uint32_t lo, uint32_t divisor);

// A fraction of full scale. The decay's speed is set so the amount reaches it exactly at the
// nominal duration, which is what makes DURATION read true.
//   - The quantity held to it is the chiff's amplitude, so its sigma is
//     kChiffAmplitudeSigmas lower again.
//   - Written as the dB figure and converted here, not transcribed as digits.
//     __builtin_pow folds at compile time, so it costs no code and no libm.

// One endpoint of the ramp the render adds to every sample: the mean's rail
// correction, plus the bias it was measured with. The run wants it at its start
// and at its end, and differences the two.
static uint32_t TargetWithAllBias(
    int32_t nominal_q30, int32_t bias_q30,
    int32_t mean_min_q30, int32_t mean_max_q30) {
  if (mean_min_q30 >= mean_max_q30) {
    // Chiff wider than the rails: centre it and let the output saturate.
    return static_cast<uint32_t>((kValueMax_q30 >> 1) - nominal_q30);
  }
  return OffsetForChiffAmplitude((nominal_q30 >> 1) + (bias_q30 >> 1),
                                 mean_min_q30 >> 1, mean_max_q30 >> 1)
    + static_cast<uint32_t>(bias_q30);
}

// The knob's own maps, taking a Q7.25 amount where the knob indexes an integer
// -- a decaying chiff wears the timbre each amount it passes through has as an
// onset, which holds only where both read the same curve.
// Returns at or under slew_time_at_amount_zero.
static uint32_t ChiffSlewTimeAtAmount_q5_27(
    uint32_t amount_q30, uint32_t slew_time_at_amount_zero_q5_27) {
  // The chiff's fastest slew time, nearly zero on purpose: at rate 1.0 a
  // slew's output is its input, so the fast end is genuinely unslewed.
  // 1/128 octave off zero is rate 0.9946, and rate 1.0 is a setting the chiff
  // uses, so its rate is derived uncapped.
  const uint32_t kChiffMinSlewTimeLog2_q5_27 = (1u << 27) / 128;
  // Where the slew time reaches its fast end. Independent of where the drive
  // begins.
  const uint32_t kChiffAmountForMinSlewTime_q30 = static_cast<uint32_t>(
    kChiffAmountFull_q30 * (110.0 / 127.0) + 0.5);
  if (slew_time_at_amount_zero_q5_27 <= kChiffMinSlewTimeLog2_q5_27) {
    return slew_time_at_amount_zero_q5_27;
  }
  // Scales the amount before it indexes the curve, so the curve is fully
  // traversed by kChiffAmountForMinSlewTime_q30. Q1.16 so a non-power-of-two
  // point is expressible.
  const uint32_t kChiffSlewCurveAmountScale_q1_16 = static_cast<uint32_t>(
    ((static_cast<uint64_t>(kChiffAmountFull_q30) << 16)
     + (kChiffAmountForMinSlewTime_q30 >> 1)) / kChiffAmountForMinSlewTime_q30);
  const uint64_t unclamped_amount_q30 =
      (static_cast<uint64_t>(amount_q30) * kChiffSlewCurveAmountScale_q1_16) >> 16;
  const uint32_t scaled_amount_q30 = unclamped_amount_q30 > kChiffAmountFull_q30
      ? kChiffAmountFull_q30 : static_cast<uint32_t>(unclamped_amount_q30);
  // Every entry of the curve, not every other one: the amount is finer than
  // the knob now, so the knots may as well be too.
  const uint32_t kCurveIndexShift = kChiffAmountFractionalBits - 8;
  const uint32_t index = scaled_amount_q30 >> kCurveIndexShift;
  const uint32_t frac = scaled_amount_q30 & ((1u << kCurveIndexShift) - 1);
  const uint32_t lo_u16 = lut_env_expo[index];
  const uint32_t hi_u16 = index < LUT_ENV_EXPO_SIZE - 1
      ? lut_env_expo[index + 1] : lo_u16;
  const uint32_t warped_u16 = lo_u16 + static_cast<uint32_t>(
      (static_cast<uint64_t>(hi_u16 - lo_u16) * frac) >> kCurveIndexShift);
  const uint32_t warp_expo_u16 = (warped_u16 << 16) / kEnvExpoFull_u16;
  // A quarter of the way toward a straight line. The exponential curve alone
  // holds the slew time near its fast end for 124 ms of a 687 ms chiff; a
  // straight line moves it at once, but adds octaves across the whole knob.
  const uint32_t warp_linear_u16 = scaled_amount_q30 >> (kChiffAmountFractionalBits - 16);
  // lut_env_expo is above the straight line everywhere, so this only subtracts.
  const uint32_t warp_u16 = warp_expo_u16 - ((warp_expo_u16 - warp_linear_u16) >> 2);
  return slew_time_at_amount_zero_q5_27 - static_cast<uint32_t>(
    (static_cast<uint64_t>(
       slew_time_at_amount_zero_q5_27 - kChiffMinSlewTimeLog2_q5_27) * warp_u16)
    >> 16);
}

// Closed form, because the chiff's amplitude IS slew_input_max * amount:
//
//   slew_input_max * amount >= kChiffInaudibleAmplitude
//
//   - One 64/32 divide, and it evaluates none of the maps whose calibration
//     the threshold depends on.
//   - PREDICTED: where the slew input caps, the amplitude is under
//     slew_input_max * amount, so the chiff goes inaudible earlier than this.
//     The symptom is DURATION reading short at the bottom of AMOUNT.
static uint32_t ChiffInaudibleAmount_q30(
    uint32_t start_q30, int32_t slew_input_max_q30) {
  if (slew_input_max_q30 <= 0) return start_q30;
  const double kChiffInaudibleDbFs = -48.2;
  const uint32_t kChiffInaudibleAmplitude_q30 = static_cast<uint32_t>(
    static_cast<double>(1u << 30)
      * __builtin_pow(10.0, kChiffInaudibleDbFs / 20.0) + 0.5);
  const uint64_t scaled =
    static_cast<uint64_t>(kChiffInaudibleAmplitude_q30) << kChiffAmountFractionalBits;
  const uint32_t amount_q30 = DivU64ByU32(
    static_cast<uint32_t>(scaled >> 32), static_cast<uint32_t>(scaled),
    static_cast<uint32_t>(slew_input_max_q30));
  return amount_q30 < start_q30 ? amount_q30 : start_q30;
}

// The phase at which the amount reaches that fraction of its initial value --
// a fraction of the decay. It is the decay speed: make
// this phase arrive at the duration and the chiff goes inaudible there.
//
// Inverts lut_env_expo by searching it: 257 monotone entries, so eight
// compares land on the bracket and one interpolation finishes.
//   - "Already inaudible at the onset" is an early return: the phase to reach
//     the target is zero there, and the decay runs at full speed.
static uint32_t ChiffInaudiblePhase_u16(
    uint32_t start_q30, int32_t slew_input_max_q30) {
  if (!start_q30) return 65536;
  const uint32_t target_q30 =
    ChiffInaudibleAmount_q30(start_q30, slew_input_max_q30);
  // Inaudible before the note starts: fall through AMOUNT at full speed.
  if (target_q30 >= start_q30) return 65536;
  // The remaining fraction the decay has to reach, u16. target < start is
  // guaranteed above, so the quotient fits u16.
  const uint32_t inaudible_amount_fraction_u16 = DivU64ByU32(
    target_q30 >> 16, target_q30 << 16, start_q30);
  // lut_env_expo rises, so the remaining fraction falls: find the last index
  // whose remaining is still >= needed.
  uint32_t lo = 0, hi = LUT_ENV_EXPO_SIZE - 1;
  while (hi - lo > 1) {
    const uint32_t mid = (lo + hi) >> 1;
    if (kEnvExpoFull_u16 - lut_env_expo[mid] >= inaudible_amount_fraction_u16) lo = mid;
    else hi = mid;
  }
  const uint32_t above = kEnvExpoFull_u16 - lut_env_expo[lo];
  const uint32_t below = kEnvExpoFull_u16 - lut_env_expo[hi];
  const uint32_t span = above - below;
  const uint32_t frac_u8 = span
    ? (((above - inaudible_amount_fraction_u16) << 8) / span) : 0;
  // ChiffAmountAtPhase reads the index from phase >> 24 and the fraction from
  // the next byte down, so a u16 phase is exactly index:fraction.
  const uint32_t phase_u16 = (lo << 8) | (frac_u8 > 255 ? 255 : frac_u8);
  return phase_u16 ? phase_u16 : 1;
}

// The initial amount times the curve, which is fixed for every chiff.
//   - The curve is lut_env_expo, the envelope's own stage curve, and it has to
//     be: amplitude goes as 20log10(amount), so constant dB per second wants
//     the amount itself to decay exponentially: AMOUNT's top half spans a few
//     dB and its bottom few units span tens.
//   - It lands on exactly zero, which is what makes the chiff converge: a
//     slew reaches zero only if what it chases does.
static uint32_t ChiffAmountAtPhase_q30(
    uint32_t initial_q30, uint32_t phase_q32) {
  const uint32_t index = phase_q32 >> 24;
  const uint32_t frac_u8 = (phase_q32 >> 16) & 0xFF;
  const uint32_t lo = lut_env_expo[index];
  const uint32_t hi = index < LUT_ENV_EXPO_SIZE - 1
    ? lut_env_expo[index + 1] : lut_env_expo[LUT_ENV_EXPO_SIZE - 1];
  const uint32_t done_u16 = lo + (((hi - lo) * frac_u8) >> 8);
  // The normalisation is a subtract. The table's last entry is
  // kEnvExpoFull, and x * 2^16 / kEnvExpoFull is exactly x for every x below
  // that entry -- the quotient's fractional part only reaches 1 at the entry
  // itself.
  const uint32_t amount_fraction_u16 = done_u16 < kEnvExpoFull_u16
    ? kEnvExpoFull_u16 - done_u16 + (done_u16 ? 0 : 1) : 0;
  return static_cast<uint32_t>(
    (static_cast<uint64_t>(initial_q30) * amount_fraction_u16) >> 16);
}

// THE GAIN CANCELS HERE, which is what leaves the chiff's amplitude equal to
// slew_input_max * amount. The slew reaches only amplitude_gain of what it
// chases, so the input is solved by dividing that back out:
//
//   fraction = min(1, amount / amplitude_gain)
//
//   - Below kChiffAmountForDriveBegin two effects cut the output: the input
//     shrinks, and a slower slew reaches less of it. This removes the second.
//   - At and above kChiffAmountForMinSlewTime it is exactly a no-op: the gain
//     clamps at 1.0 and this collapses to the amount itself.
//   - Where the min binds, the amplitude falls under slew_input_max * amount
//     and the bottom of AMOUNT goes with it.
static int32_t ChiffSlewInputFractionAtAmount_q30(
    uint32_t amount_q30, uint32_t slew_time_log2_q5_27, int32_t rate_q31) {
  // (1 - r/4 - r^2/32): the forward correction inverted to two terms.
  const uint32_t r_q31 = static_cast<uint32_t>(rate_q31);
  const uint32_t r_sq_q31 = static_cast<uint32_t>(
    (static_cast<uint64_t>(r_q31) * r_q31) >> 31);
  const uint32_t correction_q31 =
    (1u << 31) - (r_q31 >> 2) - (r_sq_q31 >> 5);
  const uint32_t corrected_q30 = static_cast<uint32_t>(
    (static_cast<uint64_t>(amount_q30) * correction_q31) >> 31);
  // The reciprocal comes out of the same exp2 table the
  // forward gain uses:
  //
  //   gain     = min(1, coefficient * 2^(-t/2) * (1 + r/4 + 3r^2/32))
  //   1 / gain = max(1, (1/coefficient) * 2^(+t/2) * (1 - r/4 - r^2/32))
  //
  //   - 2^(+t/2) exceeds Q30, so it is built from one table read: with n =
  //     floor(g) and f its fraction, 2^g is 2^(n+1) * 2^(f-1), read at
  //     (1 - f).
  // The coefficient in octaves, negated, so the reciprocal is built by
  // addition. The WHOLE coefficient is inverted: it carries the sigma multiple
  // and kChiffDrawRmsFractionOfMax alike.
  const uint32_t kChiffAmplitudeGainCoefficientLog2_q5_27 = static_cast<uint32_t>(
    -__builtin_log2(static_cast<double>(kChiffAmplitudeGainCoefficient_q31_sqrt)
                    / kOne_q31_sqrt) * 134217728.0 + 0.5);
  const uint32_t g_q5_27 =
    (slew_time_log2_q5_27 >> 1) + kChiffAmplitudeGainCoefficientLog2_q5_27;
  const uint32_t shift = (g_q5_27 >> 27) + 1;
  const uint32_t two_pow_f_q31 = SlewRateFromTimeLog2_q31(
    (1u << 27) - (g_q5_27 & kSlewTimeFraction_q5_27));
  const uint32_t scaled_q30 = static_cast<uint32_t>(
    (static_cast<uint64_t>(corrected_q30) * two_pow_f_q31) >> 31);
  // |chiff| <= input always, so an input past full scale asks for more output
  // than exists. Saturate in the shift's own terms, before it wraps.
  const uint32_t ceiling_q30 = shift >= 31 ? 0u : ((1u << 30) >> shift);
  if (scaled_q30 >= ceiling_q30) return 1 << 30;
  const uint32_t slew_input_fraction_q30 = scaled_q30 << shift;
  // The gain's own min(1, ...) read backwards: under its clamp the solve asks
  // for less input than the amount, and the amount is the answer.
  return static_cast<int32_t>(
    slew_input_fraction_q30 > amount_q30 ? slew_input_fraction_q30 : amount_q30);
}

static int32_t ChiffDriveAtAmount_q4_26(uint32_t amount_q30) {
  // Same slew, same clip threshold, more signal at it -- and the clip
  // saturates the slew, so the output squares off and its amplitude rises at
  // once. Below this the drive is 1. Half of full AMOUNT, so the span above it
  // is a shift.
  const uint32_t kChiffAmountForDriveBegin_q30 = kChiffAmountFull_q30 >> 1;
  const uint32_t kChiffMaxDriveOctaves =
      kChiffLevelFractionalBits - kChiffSlewStateFractionalBits;
  // Octaves of drive from the start above to full AMOUNT.
  //   - Calibrated by ear, short of the kChiffMaxDriveOctaves that pins the
  //     state on the clip at every level, so the top of the knob approaches a
  //     square asymptotically.
  //   - The suffix is kChiffMaxDriveOctaves: past that the driven input leaves
  //     Q30.
  //   - Carried at the amount's own fractional bits. The drive spans half of
  //     full AMOUNT, so the amount past the start times this IS the Q5.27
  //     exponent in the product's high word -- one umull, no shift.
  const uint32_t kChiffDriveSpanOctaves_q2_30 = static_cast<uint32_t>(
    2.5 * (1u << kChiffAmountFractionalBits));
  uint32_t drive_octaves_q5_27 = 0;
  if (amount_q30 > kChiffAmountForDriveBegin_q30) {
    drive_octaves_q5_27 = MulU32(
      amount_q30 - kChiffAmountForDriveBegin_q30, kChiffDriveSpanOctaves_q2_30);
  }
  // Exponent measured down from the ceiling, so Q26 carries the division.
  return static_cast<int32_t>(SlewRateFromTimeLog2_q31(
    (kChiffMaxDriveOctaves << 27) - drive_octaves_q5_27) >> 1);
}

void Envelope::NoteOn(
  ADSR& adsr,
  int32_t min_target_s16, int32_t max_target_s16,
  uint32_t chiff_amount_q30, uint32_t chiff_audible_samples
) {
  adsr_ = &adsr;
  int16_t scale_s16 = max_target_s16 - min_target_s16;
  int32_t min_target_q31 = min_target_s16 << 16;
  // NB: sustain level can be higher than peak
  note_target_q30_[ENV_STAGE_ATTACK] =
    (min_target_q31 + scale_s16 * adsr.peak_u16) >> 1;
  note_target_q30_[ENV_STAGE_DECAY] = note_target_q30_[ENV_STAGE_SUSTAIN] =
    (min_target_q31 + scale_s16 * adsr.sustain_u16) >> 1;
  note_target_q30_[ENV_STAGE_RELEASE] = note_target_q30_[ENV_STAGE_DEAD] =
    min_target_q31 >> 1;
  // The range may be numerically inverted -- CV DAC codes fall as volts rise,
  // and a warped timbre target may be negative -- so the two lines below take
  // a min over the note targets and a magnitude, not release/peak directly.
  int32_t release_q30 = note_target_q30_[ENV_STAGE_RELEASE];
  value_floor_q30_ = std::min<int32_t>(0, std::min(release_q30, std::min(
    note_target_q30_[ENV_STAGE_ATTACK], note_target_q30_[ENV_STAGE_SUSTAIN])));
  // Half the range, in the targets' Q30 domain: a target is s16 << 15.
  chiff_slew_input_max_q30_ =
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
      // A sizing reference: it sets how fast the amount falls.
      // The initial amount is the liveness flag -- zero exactly when this note
      // has no chiff, so liveness has one source.
      chiff_amount_initial_q30_ = chiff_audible_samples ? chiff_amount_q30 : 0;
      if (!chiff_amount_initial_q30_) {
        chiff_slew_input_fraction_q30_ = 0;
        break;
      }
      // The largest slew time the chiff may reach, which it does at amount
      // zero.
      //   - Without it that end is duration-derived, so DURATION moves every
      //     cutoff ChiffSlewTimeAtAmount places, and at long durations the
      //     input rails and amplitude stops tracking AMOUNT.
      //   - A min, not an assignment: the chiff must outlast its own slew's
      //     time constant to reach amplitude.
      //   - It costs the sub-audio slew rates at the bottom of AMOUNT.
      const uint32_t kChiffMaxSlewTimeLog2_q5_27 = 11u << 27;  // 11 octaves
      chiff_slew_time_at_amount_zero_q5_27_ = std::min(
        ChiffSlewTimeFromSamples_q5_27(chiff_audible_samples),
        kChiffMaxSlewTimeLog2_q5_27);
      chiff_amount_q30_ = chiff_amount_initial_q30_;
      chiff_phase_q32_ = 0;
      // The audible range of AMOUNT is crossed in exactly the duration; the
      // inaudible remainder is where the chiff finishes converging.
      chiff_phase_step_q32_ = static_cast<uint32_t>(
        (static_cast<uint64_t>(0xFFFFFFFFu / chiff_audible_samples)
         * ChiffInaudiblePhase_u16(chiff_amount_initial_q30_,
             chiff_slew_input_max_q30_)) >> 16);
      // Every chiff number is read off the starting amount here.
      // Leaving any to the first run enters the note on the previous note's
      // value, and a whole chiff can live inside one block.
      chiff_slew_time_log2_q5_27_ = ChiffSlewTimeAtAmount_q5_27(
        chiff_amount_initial_q30_, chiff_slew_time_at_amount_zero_q5_27_);
      chiff_slew_input_fraction_q30_ = ChiffSlewInputFractionAtAmount_q30(
        chiff_amount_initial_q30_, chiff_slew_time_log2_q5_27_,
        static_cast<int32_t>(
          SlewRateFromTimeLog2_q31(chiff_slew_time_log2_q5_27_)));
      break;
    }
  }
}

// Slew rate = 2^-slew_time_log2, Q31 (2^31 == 1.0). Result fits int32.
static inline uint32_t SlewRateFromTimeLog2_q31(uint32_t slew_time_log2_q5_27) {
  uint32_t integer_part = slew_time_log2_q5_27 >> 27;
  uint32_t two_pow_neg_fraction_u16 = Interpolate824(
    lut_expo2_neg, (slew_time_log2_q5_27 & kSlewTimeFraction_q5_27) << 5);
  return (two_pow_neg_fraction_u16 << 15) >> integer_part;
}

// 1 - e^-1, the true slew coefficient at a one-sample time constant.
//   - rate = 2^-t is the small-rate approximation of 1 - e^(-1/tau). It
//     reaches 1.0 at t = 0, where the truth is 0.632, and a rate of 1.0 is not a slew
//     -- the value arrives in one sample.
//   - Capping removes the "stage too short to slew" special case and lands a
//     short stage where every other stage lands: 1 - 0.632 is e^-1, so a
//     4-sample stage covers 1 - e^-4 like the rest.
//   - 4 samples is the shortest stage that exists: modulate_7_13 clamps to
//     [0, 8191], so the increment table bottoms out at UINT32_MAX/4.
//   - The cap lives on the rate, not on the exp2 it is read from.
static inline int32_t SlewRateFromSlewTime_q31(uint32_t slew_time_log2_q5_27) {
  const int32_t kMaxSlewRate_q31 = static_cast<int32_t>(
    (1.0 - __builtin_exp(-1.0)) * 2147483648.0 + 0.5);
  const int32_t rate_q31 = static_cast<int32_t>(
    SlewRateFromTimeLog2_q31(slew_time_log2_q5_27));
  return rate_q31 > kMaxSlewRate_q31 ? kMaxSlewRate_q31 : rate_q31;
}

// The same reciprocal the envelope stages use to turn an increment into a span.
uint32_t ChiffAudibleSamples(uint32_t chiff_duration_increment_u32) {
  return chiff_duration_increment_u32
      ? (UINT32_MAX / chiff_duration_increment_u32) : UINT32_MAX;
}

// decay = 1 - 2^-increment in Q32, via 2-term Taylor of 1 - 2^-x about x = 0
// (u = x*ln2): decay ~ u - u^2/2. Exact enough since `increment` is a tiny
// per-sample shift step. Q32 (small positive) so the ramp step is a single
// SMMUL: rate -= (rate * decay) >> 32.
static inline int32_t ChiffSlewRateDecayFromTimeStep_q32(uint32_t increment_q5_27) {
  const uint32_t kLn2_q28 = static_cast<uint32_t>(
    __builtin_log(2.0) * 268435456.0 + 0.5);
  int64_t u_q32 = (static_cast<int64_t>(increment_q5_27) * kLn2_q28) >> 23;
  return static_cast<int32_t>(u_q32 - ((u_q32 * u_q32) >> 33));
}

void Envelope::Trigger(EnvelopeStage stage) {
  // Anchor the new stage on where the leaving stage's nominal value reached:
  // with no chiff the value is the classic slew; a timed stage's is closed-form
  // from its phase; a hold's has converged.
  if (!chiff_slew_input_fraction_q30_) {
    stage_start_q30_ = nominal_value_q30_;
  } else if (stage_phase_increment_u32_) {
    // A stage that ran to completion leaves stage_samples_left_ == 0, which
    // wraps the product back to phase 0 -- aliasing "fully elapsed" onto "not
    // started" and anchoring the new stage where the old one began. Saturate.
    uint32_t stage_phase_u32 = stage_samples_left_
      ? 0u - stage_samples_left_ * stage_phase_increment_u32_
      : UINT32_MAX;
    // No landing fraction: the slew aims past the target, so lut_env_expo's
    // own normalization already describes where the value is.
    uint32_t expo_u16 = Interpolate824(lut_env_expo, stage_phase_u32);
    stage_start_q30_ += static_cast<int32_t>(
      (static_cast<int64_t>(stage_target_q30_ - stage_start_q30_) * expo_u16) >> 16);
  } else {
    stage_start_q30_ = stage_target_q30_;
  }
  stage_ = stage;
  stage_target_q30_ = note_target_q30_[stage]; // Cache against new NoteOn
  switch (stage) {
    case ENV_STAGE_ATTACK : stage_phase_increment_u32_ = adsr_->attack_u32  ; break;
    case ENV_STAGE_DECAY  : stage_phase_increment_u32_ = adsr_->decay_u32   ; break;
    case ENV_STAGE_RELEASE: stage_phase_increment_u32_ = adsr_->release_u32 ; break;
    default:
      // Hold stage: no countdown; keep slewing toward the target at the rate
      // inherited from the previous stage. The chiff runs on its own schedule.
      stage_phase_increment_u32_ = 0;
      return;
  }

  if (stage_start_q30_ == stage_target_q30_) {
    return Trigger(static_cast<EnvelopeStage>(stage + 1));
  }

  if (!stage_phase_increment_u32_) {
    // Zero increment: a hold.
    return;
  }

  stage_samples_left_ = UINT32_MAX / stage_phase_increment_u32_;

  // Slew time from the stage duration: with N = 2^32/increment samples and k
  // time constants per stage, 2^shift = N/k. Same log2 approximation as
  // ChiffSlewTimeFromSamples_q5_27.
  uint8_t leading_zeros = __builtin_clz(stage_phase_increment_u32_);
  uint32_t stage_slew_time_log2_q5_27;
  if (leading_zeros >= 30) {
    // Increment <= 3: N >= ~2^30.5, whose shift saturates the cap anyway.
    // Computed separately: (leading_zeros + 1) << 27 overflows at this end.
    stage_slew_time_log2_q5_27 = kMaxRepresentableSlewTimeLog2_q5_27;
  } else {
    uint32_t mantissa_frac_q5_27 =
        ((stage_phase_increment_u32_ << leading_zeros) & 0x7FFFFFFFu) >> 4;
    uint32_t log2_stage_samples_q5_27 =
        (static_cast<uint32_t>(leading_zeros + 1) << 27) - mantissa_frac_q5_27;
    // Floored at 0, the fastest the slew runs -- and the subtraction is
    // unsigned, so it needs the branch anyway.
    stage_slew_time_log2_q5_27 =
      log2_stage_samples_q5_27 <= kSlewTimesPerStageLog2_q5_27
      ? 0
      : std::min(
          log2_stage_samples_q5_27 - kSlewTimesPerStageLog2_q5_27,
          kMaxRepresentableSlewTimeLog2_q5_27
        );
  }
  // The stage's rate changes only here, so this is where it is derived: it
  // costs an exp2 table interpolation, for a quantity that moves once a stage.
  // The slew time it comes from is a local, this being its only reader.
  stage_slew_rate_q31_ = SlewRateFromSlewTime_q31(stage_slew_time_log2_q5_27);
  // A release only speeds the decay up. CHIFF DURATION owns the schedule; the
  // release adds one deadline, so the chiff dies with the note that makes it.
  if (stage == ENV_STAGE_RELEASE && stage_samples_left_ && chiff_amount_initial_q30_) {
    // One deadline and one mechanism: what is left of the decay, over the
    // samples available, taken only if faster than the step already running.
    // The slew time and the input follow, because both read the amount: the
    // slew reaches the handoff at release speed, not at chiff speed.
    const uint32_t chiff_phase_step_q32 =
      (0xFFFFFFFFu - chiff_phase_q32_) / stage_samples_left_;
    if (chiff_phase_step_q32 > chiff_phase_step_q32_) {
      chiff_phase_step_q32_ = chiff_phase_step_q32;
    }
  }
}

void Envelope::RenderSamples(int16_t* sample_buffer, int32_t bias_target_q31) {
  // Bias is unaffected by a stage change, so it is computed once a block.
  const int32_t bias_slope_q31 = ((bias_target_q31 >> 1) - (bias_q31_ >> 1)) >> (kAudioBlockSizeBits - 1);
  RenderStage(sample_buffer, kAudioBlockSize, bias_q31_, bias_slope_q31);
}

// Advance to the next stage and render the block's remaining samples there.
// The caller saves the value first, so the re-entrant Trigger sees the real
// start value.
void Envelope::HandOffToNextStage(
  int16_t* sample_buffer, size_t block_samples_left,
  int32_t bias_q31, int32_t bias_slope_q31
) {
  Trigger(static_cast<EnvelopeStage>(stage_ + 1));
  RenderStage(sample_buffer, block_samples_left, bias_q31, bias_slope_q31);
}

// One rendered sample, written once and expanded by all four loops below:
// whole-word and head/tail, in both the asm and the C reference. `draw` is the
// raw 0..kChiffDrawValueMax field. `bit_offset` is a string because `ubfx`
// needs an immediate.
#define YARNS_CHIFF_ASM_SAMPLE(bit_offset) \
  "  smull ip, lr, %[rate], %[decay]\n"       /* (rate*decay), lr = hi     */ \
  "  sub   %[rate], %[rate], lr\n"            /* rate -= (rate*decay)>>32  */ \
  "  ubfx  ip, %[draws], #" bit_offset ", %[drawbits]\n" /* one draw, low end */ \
  "  add   ip, ip, ip\n"                      /* level = 2*draw - 15, i.e. */ \
  "  sub   ip, ip, %[drawmax]\n"              /*   an odd multiple, signed */ \
  "  mul   lr, ip, %[qinput]\n"               /* what the slew chases      */ \
  "  sub   lr, lr, %[chiff]\n"                /* delta                     */ \
  "  smull ip, lr, lr, %[rate]\n"                                             \
  "  add   %[chiff], %[chiff], lr, lsl #1\n"  /* chiff += (product>>32)*2  */ \
  "  cmp   %[chiff], %[clip]\n"               /* saturating slew: the      */ \
  "  it    gt\n"                              /*   clipped value feeds     */ \
  "  movgt %[chiff], %[clip]\n"               /*   back, so the state      */ \
  "  cmn   %[chiff], %[clip]\n"               /*   holds only what it      */ \
  "  it    lt\n"                              /*   is allowed to show      */ \
  "  rsblt %[chiff], %[clip], #0\n"                                           \
  "  smull ip, lr, %[delta], %[srate]\n"        /* delta to the adj. target  */ \
  "  sub   %[delta], %[delta], lr, lsl #1\n"      /*   at the STAGE's rate     */ \
  "  add   %[comb], %[comb], %[cslope]\n"     /* bias + mean + adj. target */ \
  "  sub   ip, %[comb], %[delta]\n"             /* the mean                  */ \
  "  add   ip, ip, %[chiff], lsl %[stshift]\n" /* + the chiff, unscaled    */ \
  "  usat  ip, %[satbits], ip, asr %[sbits]\n" /* saturate and shift, 1 op */ \
  "  strh  ip, [%[buf]], #2\n"
// Every constant above is an "i" operand, not a digit in a string, so a
// rename reaches the asm. "i" substitutes the literal and costs no register,
// which matters: twelve "r" operands is the ceiling the body allocates.

// The operands both asm blocks share, written once and expanded by each: the
// QEMU differential proves asm == C, not asm == asm.
#define YARNS_CHIFF_ASM_STATE                                                 \
  [chiff] "+r"(chiff_slew_state_q26), [delta] "+r"(nominal_delta_q1_30),                 \
  [rate] "+r"(chiff_slew_rate_q31), [comb] "+r"(target_with_all_bias),                      \
  [buf] "+r"(sample_buffer), [draws] "+r"(draws)
#define YARNS_CHIFF_ASM_INPUTS                                                \
  [decay] "r"(chiff_slew_rate_decay_q32), [qinput] "r"(chiff_driven_slew_input_scaled_q1_26),            \
  [clip] "r"(chiff_clip_threshold_q26), [srate] "r"(stage_slew_rate_q31),             \
  [cslope] "r"(target_with_all_bias_slope),                                           \
  [drawbits] "i"(kChiffDrawBits), [drawmax] "i"(kChiffDrawValueMax),               \
  [stshift] "i"((kChiffLevelFractionalBits - kChiffSlewStateFractionalBits)), [sbits] "i"(kSampleBits),                  \
  [satbits] "i"(kOutputSaturateBits)

#define YARNS_CHIFF_RENDER_SAMPLE(draw)                                       \
  do {                                                                        \
    chiff_slew_rate_q31 -= static_cast<int32_t>(                                    \
      (static_cast<int64_t>(chiff_slew_rate_q31) * chiff_slew_rate_decay_q32) >> 32);               \
    /* (delta * rate) >> 32, DOUBLED -- i.e. the high word only, no low-word  \
     * term. Two instructions saved per slew step. The dropped bit is a half  \
     * LSB per sample, and the error is bounded by the last step: at a       \
     * slew's fixed point the step is zero. */                                \
    int32_t delta_q26 = (2 * (draw) - kChiffDrawValueMax)                          \
      * chiff_driven_slew_input_scaled_q1_26 - chiff_slew_state_q26;                          \
    chiff_slew_state_q26 += 2 * static_cast<int32_t>(                              \
      (static_cast<int64_t>(delta_q26) * chiff_slew_rate_q31) >> 32);               \
    /* The clipped value feeds back: a saturating slew, not a waveshaped      \
     * output. Measured to reach an exact square wave at 16x drive where      \
     * clipping the output only approaches one. */                            \
    if (chiff_slew_state_q26 > chiff_clip_threshold_q26) {                            \
      chiff_slew_state_q26 = chiff_clip_threshold_q26;                                \
    } else if (chiff_slew_state_q26 < -chiff_clip_threshold_q26) {                    \
      chiff_slew_state_q26 = -chiff_clip_threshold_q26;                               \
    }                                                                         \
    nominal_delta_q1_30 -= 2 * static_cast<int32_t>(                              \
      (static_cast<int64_t>(nominal_delta_q1_30) * stage_slew_rate_q31) >> 32);        \
    target_with_all_bias += target_with_all_bias_slope;                                       \
    /* The asm's USAT: arithmetic shift by kSampleBits, then saturate         \
     * unsigned to kOutputSaturateBits. The upper bound is spelled from THAT   \
     * constant and not as INT16_MAX -- the two are equal today, and a twin    \
     * that agrees only by coincidence is how the pair drifts. */              \
    /* Reinterpreted as signed BEFORE the shift: the accumulator is modular,   \
     * the shift must be arithmetic to match the asm's asr, and the true value  \
     * of this sum is in int32 range. */                                        \
    int32_t sample = static_cast<int32_t>(target_with_all_bias                          \
      - static_cast<uint32_t>(nominal_delta_q1_30)                                  \
      + (static_cast<uint32_t>(chiff_slew_state_q26) << (kChiffLevelFractionalBits - kChiffSlewStateFractionalBits)))          \
      >> kSampleBits;                                                           \
    const int32_t kSampleMax = (1 << kOutputSaturateBits) - 1;                \
    if (sample < 0) sample = 0;                                               \
    if (sample > kSampleMax) sample = kSampleMax;                             \
    *sample_buffer++ = static_cast<int16_t>(sample);                          \
  } while (0)

// Advances the chiff's phase, amount and slew input by one run, and hands back
// what the loop runs on. All three move together: pinning the input costs the
// pass-through invariant (up to 18 dB of error) and the chiff settles above
// zero.
// How far the chiff may swing before the clip bites, and equally how far the
// mean is held from each rail. min() because the chiff is bounded by the
// smaller of its slew input (the state is a convex combination of +/- it) and
// its own tail: the input binds when fast, the tail when slow.
//   - The amplitude is the gain times the input. Multiplying by kOne_q31_sqrt
//     and shifting 31 is the divide by 2^15.5, in 32-bit ops, within 0.031 dB
//     of the exact form -- and the threshold inherits that.
//   - Computed from the run-start slew time while the rate decays within the
//     run, so it runs generous, which is safe.
static int32_t ChiffClipThreshold_q30(
    int32_t chiff_slew_input_q30, uint32_t chiff_amplitude_gain_q31_sqrt) {
  // How many chiff amplitudes the threshold sits at. Sized so the undriven end
  // passes essentially unclipped, at the smallest hold on the mean that allows
  // it.
  const uint32_t kChiffClipAmplitudesShift = 1;  // 2 amplitudes, 3*sqrt(2) sigma
  const int32_t chiff_amplitude_q30 = static_cast<int32_t>(
    (static_cast<int64_t>(chiff_slew_input_q30)
     * (chiff_amplitude_gain_q31_sqrt * kOne_q31_sqrt)) >> 31);
  return std::min<int32_t>(
    chiff_slew_input_q30, chiff_amplitude_q30 << kChiffClipAmplitudesShift);
}

Envelope::ChiffRunDecay Envelope::AdvanceChiffDecay(uint32_t run_samples) {
  ChiffRunDecay decay;
  decay.slew_time_step_q5_27 = 0;
  decay.drive_q4_26 = 1 << kChiffSlewStateFractionalBits;  // 1.0
  if (!chiff_amount_initial_q30_) return decay;
  // Saturate on the high word and on the phase left, not on a 64-bit compare:
  // one umull answers whether the product fits.
  const uint32_t chiff_phase_remaining_q32 = 0xFFFFFFFFu - chiff_phase_q32_;
  const uint64_t advanced =
    static_cast<uint64_t>(chiff_phase_step_q32_) * run_samples;
  const uint32_t advanced_q32 = static_cast<uint32_t>(advanced);
  const uint32_t chiff_phase_end_q32 =
    (advanced >> 32) == 0 && advanced_q32 < chiff_phase_remaining_q32
      ? chiff_phase_q32_ + advanced_q32 : 0xFFFFFFFFu;
  // This run's start is last run's end for both the amount and the slew time,
  // so only the END is derived here and carried forward.
  const uint32_t chiff_amount_q30 = chiff_amount_q30_;
  chiff_amount_q30_ =
    ChiffAmountAtPhase_q30(chiff_amount_initial_q30_, chiff_phase_end_q32);
  const uint32_t chiff_slew_time_end_q5_27 = ChiffSlewTimeAtAmount_q5_27(
    chiff_amount_q30_, chiff_slew_time_at_amount_zero_q5_27_);
  decay.slew_time_step_q5_27 = run_samples
    ? (chiff_slew_time_end_q5_27 - chiff_slew_time_log2_q5_27_) / run_samples : 0;
  decay.drive_q4_26 = ChiffDriveAtAmount_q4_26(chiff_amount_q30);
  // Against this run's START slew time: the writeback to the end is at the
  // loop's tail, so the slew time and amount here are a consistent pair.
  chiff_slew_input_fraction_q30_ = ChiffSlewInputFractionAtAmount_q30(
    chiff_amount_q30, chiff_slew_time_log2_q5_27_,
    static_cast<int32_t>(
      SlewRateFromTimeLog2_q31(chiff_slew_time_log2_q5_27_)));
  chiff_phase_q32_ = chiff_phase_end_q32;
  return decay;
}

void Envelope::RenderStage(
  int16_t* sample_buffer, size_t block_samples_left,
  int32_t bias_q31, int32_t bias_slope_q31
) {
  int32_t value_without_bias_q30 = value_without_bias_q30_;
  int32_t nominal_value_q30 = nominal_value_q30_;
  int32_t chiff_slew_state_q26 = chiff_slew_state_q26_;

  // One straight run, bounded by the block and the stage countdown. Whichever
  // expires hands off or re-enters -- once, not re-checked per sample.
  const bool timed = stage_phase_increment_u32_ != 0;
  // No chiff on/off in here: at AMOUNT 0 the input is zero and every line
  // below degenerates on its own. The worst case is a live chiff.
  uint32_t run_samples = block_samples_left;
  if (timed) run_samples = std::min<uint32_t>(run_samples, stage_samples_left_);
  int16_t* const run_end = sample_buffer + run_samples;
  const int32_t stage_target_q30 = stage_target_q30_;

  {
    // Bias is a terminal add: neither slew carries it, so the envelope's
    // trajectory is the same whatever the bias does. The battery pins the
    // independence.
    const int32_t bias_q30 = bias_q31 >> 1;
    // Per-run copies: the loop decays the rate every sample, and the slew time
    // is what persists across runs.
    //   - The per-sample decay costs ~5 cycles/sample (~4% of the CPU) and
    //     earns it: per-run resolution holds the slew time fixed on a chiff
    //     that lives one block, and the duration inherits the attack's
    //     velocity modulation, so that is not a corner case.
    const ChiffRunDecay chiff_decay = AdvanceChiffDecay(run_samples);
    const int32_t chiff_slew_rate_decay_q32 =
      ChiffSlewRateDecayFromTimeStep_q32(chiff_decay.slew_time_step_q5_27);

    // Derived, not stored: the rate and the slew time are one quantity, held
    // in one accumulator.
    uint32_t chiff_slew_time_q5_27 = chiff_slew_time_log2_q5_27_;
    int32_t chiff_slew_rate_q31 = static_cast<int32_t>(
      SlewRateFromTimeLog2_q31(chiff_slew_time_q5_27));
    const uint32_t chiff_amplitude_gain_q31_sqrt =
      ChiffAmplitudeGainAtSlewTime_q31_sqrt(
        chiff_slew_time_q5_27, chiff_slew_rate_q31);
    // What nominal chases: past the target by 1/(1 - e^-4), so it arrives ON
    // the target as the countdown expires. Holds chase the target itself.
    // A timed stage runs four time constants, and a slew covers 1 - e^-4 =
    // 98.17% of its span in that time, so the slew aims past its target by the
    // reciprocal and lands ON it as the countdown expires.
    //   - The adjusted target passes the stage target by 1.9% of the span, so
    //     the slew input sits outside the note's range, unclamped.
    //   - lut_env_expo then reads straight: normalized to 1.0, it already
    //     describes the true slew once the target carries the 1/(1 - e^-4).
    const uint32_t kStageTargetOvershoot_u16 = static_cast<uint32_t>(
      65536.0 / (1.0 - __builtin_exp(-4.0)) + 0.5);
    int32_t stage_adjusted_target_q1_30 = stage_target_q30;
    if (timed) {
      stage_adjusted_target_q1_30 = stage_start_q30_ + static_cast<int32_t>(
        (static_cast<int64_t>(stage_target_q30 - stage_start_q30_) *
         kStageTargetOvershoot_u16) >> 16);
    }
    // Each slew step is four instructions: SMMLA, which halves that, is the
    // ARMv7E-M DSP extension (Cortex-M4), and this builds for cortex-m3.
    const int32_t stage_slew_rate_q31 = stage_slew_rate_q31_;
    const int32_t chiff_slew_input_q30 = ChiffSlewInput_q30();
    // What the slew chases. Q26 carries the drive's division, so this stays
    // inside int32 at full drive.
    const int32_t chiff_driven_slew_input_q4_26 = static_cast<int32_t>(
      (static_cast<int64_t>(chiff_slew_input_q30) * chiff_decay.drive_q4_26) >> 30);
    // One level's worth, so a draw read as an odd multiple multiplies straight
    // into what the slew chases. Dividing once a run keeps the extreme level
    // EQUAL to the input, so |chiff| <= input holds exactly
    // and the clip binds where it says. A constant divisor: one multiply.
    const int32_t chiff_driven_slew_input_scaled_q1_26 =
      chiff_driven_slew_input_q4_26 / kChiffDrawValueMax;
    // Wider than the rails allow (min >= max): the clamp is abandoned and the
    // mean centred, so the chiff clips both sides.
    const int32_t chiff_clip_threshold_q30 = ChiffClipThreshold_q30(
      chiff_slew_input_q30, chiff_amplitude_gain_q31_sqrt);
    const int32_t chiff_clip_threshold_q26 = chiff_clip_threshold_q30
      >> (kChiffLevelFractionalBits - kChiffSlewStateFractionalBits);
    const int32_t bias_slope_q30 = bias_slope_q31 >> 1;
    const int32_t mean_min_q30 = chiff_clip_threshold_q30;
    const int32_t mean_max_q30 = kValueMax_q30 - chiff_clip_threshold_q30;
    // Where nominal reaches by the run's end, for the offset's far endpoint.
    // Approximate (linear in rate * run_samples) -- it only sizes an offset
    // that is itself an approximation; nominal's own path stays exact.
    int32_t nominal_value_end_q30 = nominal_value_q30;
    {
      const int32_t nominal_delta_q1_30 = stage_adjusted_target_q1_30 - nominal_value_q30;
      int64_t step = ((static_cast<int64_t>(nominal_delta_q1_30) * stage_slew_rate_q31) >> 31)
        * static_cast<int32_t>(run_samples);
      if ((nominal_delta_q1_30 >= 0 && step > nominal_delta_q1_30) || (nominal_delta_q1_30 < 0 && step < nominal_delta_q1_30)) {
        step = nominal_delta_q1_30;
      }
      nominal_value_end_q30 += static_cast<int32_t>(step);
    }
    const int32_t bias_end_q30 = static_cast<int32_t>(
      static_cast<uint32_t>(bias_q30)
      + static_cast<uint32_t>(bias_slope_q30) * run_samples);
    // 30 fractional bits, and no Q suffix: it wraps by design, so there is no
    // maximum for a suffix to name. The adjusted target plus a full-scale
    // bias passes INT32_MAX. Every use subtracts the nominal delta first, and
    // that is in range, so the wrap cancels.
    // Unsigned makes the wrap defined.
    uint32_t target_with_all_bias = TargetWithAllBias(
      nominal_value_q30, bias_q30, mean_min_q30, mean_max_q30);
    const uint32_t target_with_all_bias_end = TargetWithAllBias(
      nominal_value_end_q30, bias_end_q30, mean_min_q30, mean_max_q30);
    // The difference is small and signed; the wrap in the subtraction is what
    // makes reinterpreting it as int32 give the true delta.
    const int32_t target_with_all_bias_slope = run_samples
      ? static_cast<int32_t>(target_with_all_bias_end - target_with_all_bias)
          / static_cast<int32_t>(run_samples)
      : 0;
    // Track the delta to the adjusted target, not the value: a slew step on
    // the value is sub/smull/add, on the delta it is a pure geometric decay,
    // smull/sub. The target folds into the offset register either way.
    int32_t nominal_delta_q1_30 = stage_adjusted_target_q1_30 - nominal_value_q30;
    target_with_all_bias += static_cast<uint32_t>(stage_adjusted_target_q1_30);

    // A run can straddle a word boundary, so the loop chunks there. Both the
    // word and how much of it is unspent carry across runs.
    ChiffDrawWord draw_state = chiff_draws_;
    uint32_t draws_left = chiff_draws_left_;
    uint32_t draws =
      draw_state >> ((kChiffDrawsPerWord - draws_left) * kChiffDrawBits);
    while (sample_buffer != run_end) {
      const uint32_t samples_left =
        static_cast<uint32_t>(run_end - sample_buffer);
      // Whole words stay inside the loop. The body sits at the register
      // ceiling -- 11 values + the draws word + ip/lr = 14 -- so the loop's
      // end test is read from memory, 2 cycles per 8 samples. A chunk loop
      // around the body spills and reloads all 14 at each word boundary.
      if (draws_left == kChiffDrawsPerWord &&
          samples_left >= kChiffDrawsPerWord) {
        uint32_t words_left = samples_left / kChiffDrawsPerWord;
#if defined(__arm__) && __ARM_ARCH >= 7
        __asm__ volatile(
          "1:\n"
          YARNS_CHIFF_ASM_SAMPLE("0")
          YARNS_CHIFF_ASM_SAMPLE("4")
          YARNS_CHIFF_ASM_SAMPLE("8")
          YARNS_CHIFF_ASM_SAMPLE("12")
          YARNS_CHIFF_ASM_SAMPLE("16")
          YARNS_CHIFF_ASM_SAMPLE("20")
          YARNS_CHIFF_ASM_SAMPLE("24")
          YARNS_CHIFF_ASM_SAMPLE("28")
          // Consume then advance, so the register leaves holding the next
          // unspent word. ubfx reads without writing, so xorshift32 in place
          // is three instructions and no memory traffic.
          "  eor   %[draws], %[draws], %[draws], lsl #13\n"
          "  eor   %[draws], %[draws], %[draws], lsr #17\n"
          "  eor   %[draws], %[draws], %[draws], lsl #5\n"
          "  subs  %[words], %[words], #1\n"        // in the freed pointer's
          "  bne   1b\n"                            //   register
          : YARNS_CHIFF_ASM_STATE, [words] "+r"(words_left)
          : YARNS_CHIFF_ASM_INPUTS
          : "ip", "lr", "cc", "memory");
#else
        while (words_left--) {
          for (uint32_t i = 0; i < kChiffDrawsPerWord; ++i) {
            YARNS_CHIFF_RENDER_SAMPLE(
              static_cast<int32_t>((draws >> (i * kChiffDrawBits))
                                   & kChiffDrawValueMax));
          }
          draws = NextChiffDraws(draws);
        }
#endif
        // The loop leaves the next unspent word in hand; draws_left is
        // already a whole word.
        draw_state = draws;
        continue;
      }
      uint32_t chunk = samples_left;
      if (chunk > draws_left) chunk = draws_left;
      int16_t* const chunk_end = sample_buffer + chunk;
    // Thumb-2 asm. Gate on __arm__, not __ARM_ARCH: arm64 hosts define
    // __ARM_ARCH == 8 without __arm__, and must take the C reference below.
#if defined(__arm__) && __ARM_ARCH >= 7
    // Hand-allocated: GCC 4.8 spills here, so every live value is an operand.
    // The QEMU differential proves this identical to the C reference.
    // Head and tail only -- the samples outside the whole words.
    __asm__ volatile(
      "  cmp   %[buf], %[end]\n"
      "  beq   2f\n"
      "1:\n"
      YARNS_CHIFF_ASM_SAMPLE("0")
      "  lsr   %[draws], %[draws], %[drawbits]\n"  // consumed low end first
      "  cmp   %[buf], %[end]\n"
      "  bne   1b\n"
      "2:\n"
      : YARNS_CHIFF_ASM_STATE
      : YARNS_CHIFF_ASM_INPUTS, [end] "r"(chunk_end)
      : "ip", "lr", "cc", "memory");
#else
    while (sample_buffer != chunk_end) {
      // One draw, consumed low end first, matching the asm's UBFX then LSR.
      YARNS_CHIFF_RENDER_SAMPLE(static_cast<int32_t>(draws & kChiffDrawValueMax));
      draws >>= kChiffDrawBits;
    }
#endif
      draws_left -= chunk;
      if (!draws_left) {
        draw_state = NextChiffDraws(draw_state);
        draws = draw_state;
        draws_left = kChiffDrawsPerWord;
      }
    }
    chiff_draws_ = draw_state;
    chiff_draws_left_ = static_cast<uint8_t>(draws_left);
    {
      // The end-of-run slew time becomes the next run's start. Needs no bound:
      // the step is a truncating divide, so this lands at or under the end it
      // was derived from. The battery watches the invariant.
      chiff_slew_time_log2_q5_27_ += chiff_decay.slew_time_step_q5_27 * run_samples;
    }

    nominal_value_q30 = stage_adjusted_target_q1_30 - nominal_delta_q1_30;
    nominal_value_q30_ = nominal_value_q30;
    chiff_slew_state_q26_ = chiff_slew_state_q26;
    // Nominal plus chiff, carrying NO bias, and bounded before anyone reads
    // it: value_without_bias() returns int16_t and tremolo() multiplies in
    // int32, so both wrap out of range.
    value_without_bias_q30 = nominal_value_q30 + static_cast<int32_t>(
      static_cast<uint32_t>(chiff_slew_state_q26)
      << (kChiffLevelFractionalBits - kChiffSlewStateFractionalBits));
    if (value_without_bias_q30 < value_floor_q30_) {
      value_without_bias_q30 = value_floor_q30_;
    }
    const int32_t value_top_q30 = value_floor_q30_ + kValueMax_q30;
    if (value_without_bias_q30 > value_top_q30) {
      value_without_bias_q30 = value_top_q30;
    }
    bias_q31 = static_cast<int32_t>(static_cast<uint32_t>(bias_q31)
      + static_cast<uint32_t>(bias_slope_q31) * run_samples);
  }

  block_samples_left -= run_samples;
  value_without_bias_q30_ = value_without_bias_q30;
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

#undef YARNS_CHIFF_ASM_SAMPLE
#undef YARNS_CHIFF_RENDER_SAMPLE
#undef YARNS_CHIFF_ASM_STATE
#undef YARNS_CHIFF_ASM_INPUTS

// Exact unsigned 64/32 division, valid when the quotient fits 32 bits
// (hi < divisor). Hacker's Delight "divlu".
//
// Every 64-bit divide in this file comes here. GCC 4.8 emits __aeabi_uldivmod
// for the plain form -- ~1.4 kB of library code that no check in the suite
// can see.
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

// Scale by numerator/denominator in 32-bit hardware ops only: a umull forms
// the 64-bit product, DivU64ByU32 divides, the result saturates into int32.
// Keeps full precision at extreme ratios, where a Q15 factor collapses.
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

// Rescale every level by numerator/denominator, both non-negative. Cold
// path, so exact per-field division is fine. Slew times are
// rates, so they are scale-invariant.
void Envelope::Rescale(int32_t numerator, int32_t denominator) {
  if (denominator <= 0) return; // Degenerate scale; leave levels unchanged
  uint32_t num = static_cast<uint32_t>(numerator);
  uint32_t den = static_cast<uint32_t>(denominator);
  bias_q31_ = ScaleRatio(bias_q31_, num, den);
  value_without_bias_q30_ = ScaleRatio(value_without_bias_q30_, num, den);
  stage_target_q30_ = ScaleRatio(stage_target_q30_, num, den);
  stage_start_q30_ = ScaleRatio(stage_start_q30_, num, den);
  chiff_slew_input_max_q30_ = ScaleRatio(chiff_slew_input_max_q30_, num, den);
  // min(floor, 0) * s == min(floor * s, 0) for a non-negative s, so the offset
  // scales directly and the floor it came from need not be kept.
  value_floor_q30_ = ScaleRatio(value_floor_q30_, num, den);
  for (int i = 0; i < ENV_NUM_STAGES; ++i) {
    note_target_q30_[i] = ScaleRatio(note_target_q30_[i], num, den);
  }
}

}  // namespace yarns
