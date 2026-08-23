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

// Chiff input draws. One kChiffDrawBits-wide field per sample selects the level
// the chiff's filter chases, so a word carries kChiffDrawsPerWord samples.
// EACH ENVELOPE GENERATES ITS OWN, from its own xorshift state seeded distinctly
// in Init -- the one thing ever asked of this PRNG is that instances not share a
// sequence. The word IS the state: the unrolled loop reads its fields with ubfx
// and never writes it, so advancing it is three instructions with no load, no
// store, and no buffer to index.
namespace {
  // Bits per draw. Sixteen levels, for two reasons:
  //   - A two-level input's output IS a square once the rate reaches 1, so
  //     "unfiltered" and "overdriven" collide and the drive has nothing to
  //     shape. Sixteen makes the unfiltered midpoint noise instead, which sits
  //     kChiffDrawRmsFractionOfMax below a square of the same peak -- and that
  //     gap is what the drive reclaims.
  //   - Four bits divide a 32-bit word evenly, so the chunking below stays a
  //     shift and a mask. Asserted, not assumed.
  const uint32_t kChiffDrawBits = 4;
  // The word the draws are packed into. xorshift32 pins the width at 32: its
  // shift constants are only valid there.
  typedef uint32_t ChiffDrawWord;
  const uint32_t kChiffDrawsPerWord = 32 / kChiffDrawBits;
  // A DRAW MAY NOT STRADDLE A WORD. Both render loops extract one with a single
  // ubfx at a compile-time offset, so a draw width that did not divide the word
  // would silently read across the boundary. Pre-C++11 here, hence the negative
  // array size rather than static_assert.
  typedef char kChiffDrawBitsMustDivideTheWord[
      (32 % kChiffDrawBits == 0) ? 1 : -1];
  // THE LEVELS ARE THE ODD MULTIPLES of 1/kChiffDrawValueMax of the chiff input:
  // level = 2 * draw - kChiffDrawValueMax for a draw in [0, kChiffDrawValueMax], so they
  // run +/-1, +/-3 ... +/-kChiffDrawValueMax and the set is SYMMETRIC about zero.
  // Symmetry is not a nicety: the chiff must be zero-mean, and an asymmetric
  // set (what a plain sign-extended field gives, [-8, 7]) leaves a standing
  // offset of half a level -- ~1000 LSB at full scale -- and makes the
  // symmetric state clip asymmetric about the signal it is clipping.
  // Reading the draw as an odd multiple costs one add and one subtract.
  const int32_t kChiffDrawValueMax = (1 << kChiffDrawBits) - 1;
  // The draw set's rms over its largest value, Q16: sqrt((4n^2 - 1)/3)/(2n - 1)
  // for n magnitudes, i.e. sqrt(85)/15 = 0.6146 at four bits -- 4.23 dB below a
  // square of the same peak, which is the room the drive has. DERIVED from the
  // draw width, so widening a draw moves it.
  const uint32_t kChiffNumDrawMagnitudes = 1u << (kChiffDrawBits - 1);
  const uint32_t kChiffDrawRmsFractionOfMax_q16 = static_cast<uint32_t>(
    65536.0 * __builtin_sqrt(
      (4.0 * kChiffNumDrawMagnitudes * kChiffNumDrawMagnitudes - 1.0) / 3.0)
      / (2.0 * kChiffNumDrawMagnitudes - 1.0) + 0.5);
  typedef char kChiffDrawsMustFillWholeWords[
      (kAudioBlockSize % kChiffDrawsPerWord == 0) ? 1 : -1];

  // xorshift32. Zero is a fixed point, so a seed may never be zero; the seeder
  // below cannot produce one.
  inline ChiffDrawWord NextChiffDraws(ChiffDrawWord state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  }

  // Distinct seeds for distinct sequences. xorshift32 has ONE orbit, so seeds
  // are phases of a single stream and near seeds start near each other -- hence
  // an odd stride large relative to the orbit rather than a counter. Handed out
  // per Init rather than per object: taking a fresh one on every layout change
  // costs nothing and removes the claimed-once flag the slot allocator needed.
  const uint32_t kChiffSeedStride = 2654435761u;  // round(2^32 / golden ratio)
  uint32_t next_chiff_seed = 0xCAFEBABE;
}  // namespace

// The DAC range in Q30: the s16 output 32767 is 32767 << 15, and
// (2^30 - 1) >> 15 is 32767 exactly. It bounds the envelope's own integrator
// (anti-windup) and, separately, the biased output.
const int32_t kValueMax_q30 = (1 << 30) - 1;

// The output sample is the s16 range, which USAT #15 states directly.
const int kSampleBits = 15;
// USAT's width. INT16_MAX is 2^15 - 1, so an UNSIGNED saturate to this many
// bits is exactly the C reference's clamp to [0, INT16_MAX]. Named so the asm
// can take it as an immediate operand instead of writing 15 again.
const int kOutputSaturateBits = 15;

// How far the mean must move to sit inside [min, max]; 0 when it already does.
inline int32_t MeanCorrection(int32_t mean, int32_t min_q30, int32_t max_q30) {
  if (mean < min_q30) return min_q30 - mean;
  if (mean > max_q30) return max_q30 - mean;
  return 0;
}

// 1.0 in the _q31_sqrt format: scale 2^15.5 = sqrt(2^31), so two values
// multiply and >> 31 to a plain product. NOT an int_frac Q format. Exact to
// 3 ppm: 46341^2 = 2^31 + 4633.
const uint32_t kOne_q31_sqrt = static_cast<uint32_t>(
  32768.0 * __builtin_sqrt(2.0) + 0.5);

// lut_env_expo's last entry. Naming it lets ChiffAmountAtPhase_q7_25 normalise
// by subtraction instead of dividing by a value re-read from the table.
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

// THE CHIFF'S FASTEST SLEW TIME, nearly zero on purpose: at rate 1.0 a
// one-pole's output IS its input, so the fast end is genuinely unfiltered.
// 1/128 octave off zero is rate 0.9946 -- unfiltered to within half a percent,
// while staying off the exact zero the exp2 helper would special-case.
//   - NOT kMaxSlewRate. That cap exists for a slew that tracks a target; the
//     chiff tracks none, so it derives its rate uncapped.
//   - What makes rate 1.0 usable is the multi-level input. Two levels would
//     give a square there, and the drive would have nothing to shape.
const uint32_t kChiffMinSlewTimeLog2_q5_27 = (1u << 27) / 128;

// chiff_amount lives in [0, kChiffAmountMax].
// The fractional part of a Q5.27 slew time, i.e. everything below one octave.
const uint32_t kSlewTimeFraction_q5_27 = (1u << 27) - 1;

const uint32_t kChiffAmountBits = 7;
const uint32_t kChiffAmountMax = (1u << kChiffAmountBits) - 1;

// THE CHARACTER AXIS: where the drive starts pushing the chiff into its clip.
//   - Same filter, same clip threshold, more signal at it. The clip saturates
//     the one-pole (the clipped value feeds back), so the output squares off
//     and grows louder at once.
//   - Below this amount the drive is 1 and nothing is shaped.
//   - A two-level input would have no such axis: unfiltered it is ALREADY a
//     square. The crest factor the sixteen draw levels give away is what the
//     drive spends.
const uint32_t kChiffAmountForDriveBegin = (kChiffAmountMax + 1) / 2;
// Where the slew time reaches its fast end. Independent of where drive begins.
const uint32_t kChiffAmountForMinSlewTime = 110;
// The chiff's slew state is carried in Q26, not Q30. The drive multiplies what
// the filter chases by up to 2^kChiffDriveSpanOctaves, and Q30 would overflow;
// four bits of headroom covers it. Costs nothing -- the output add takes a
// shifted operand either way.
const uint32_t kChiffLevelFractionalBits = 30;
const uint32_t kChiffSlewStateFractionalBits = 26;
// The drive's ceiling IS that headroom, which is what this use always meant.
const uint32_t kChiffMaxDriveOctaves =
    kChiffLevelFractionalBits - kChiffSlewStateFractionalBits;
// Octaves of drive from kChiffAmountForDriveBegin to full amount, Q5.27.
//   - CALIBRATED, not derived. Reaching drive == kChiffDrawValueMax would hold
//     the state on the clip at every level and make the output a square; that
//     would want kChiffMaxDriveOctaves. This span is shorter, so the top of the
//     knob approaches the square asymptotically instead of arriving.
//   - MUST NOT EXCEED kChiffMaxDriveOctaves, or the driven input leaves Q30.
const uint32_t kChiffDriveSpanOctaves_q5_27 = (5u << 27) / 2;  // 2.5 octaves
// A CEILING ON THE SLOW END OF THE AMOUNT AXIS, in octaves of slew time.
//   - Without it the slow end is duration-derived, so turning DURATION moves
//     every cutoff below kChiffAmountForMinSlewTime; at long durations the slew
//     gets slow enough that the input rails and the law stops holding.
//   - A min, not an assignment: a chiff shorter than this time constant would
//     be truncated by its own filter before reaching amplitude -- darker AND
//     quieter.
//   - It costs the lowest corners the knob can reach, i.e. the sub-audio
//     wobble at the bottom of AMOUNT. That trade is why it is a named
//     constant.
const uint32_t kChiffMaxSlewTimeOctaves = 11;
const uint32_t kChiffMaxSlewTimeLog2_q5_27 = kChiffMaxSlewTimeOctaves << 27;
// How many chiff amplitudes the clip threshold sits at. Sized so the undriven
// end clips essentially nothing while holding the mean no further in than it
// must.
const uint32_t kChiffClipAmplitudesShift = 1;  // 2 amplitudes = 3*sqrt(2) sigma

// A timed stage runs kSlewTimesPerStageLog2 = 2, i.e. four time constants, and
// a one-pole covers only 1 - e^-4 = 98.17% of its span in that time. So the
// slew TARGETS PAST its target by the reciprocal and lands exactly on the target
// as the countdown expires, instead of handing off 1.8% short.
//   - The adjusted target overshoots by 1.9% of the span, which the note may not have.
//     Fine: the value never reaches it. It does mean the slew input can
//     sit outside the note's range; nothing clamps it, since the mean is held
//     clear of the rails at the point of use and no integrator carries it.
//   - It also makes lut_env_expo read straight: normalized to land at 1.0, the
//     table already describes the true slew once the adjusted target carries the
//     1/(1 - e^-4), so no landing-fraction scaling is needed.
const uint32_t kStageAimOvershoot_u16 = static_cast<uint32_t>(
  65536.0 / (1.0 - __builtin_exp(-4.0)) + 0.5);

// Slew rate 2^-slew_time, Q31, capped at kMaxSlewRate for a slew that has to
// track a target; defined below, declared here because Init caches it.
static inline int32_t SlewRateFromSlewTime_q31(uint32_t slew_time_log2_q5_27);

void Envelope::Init(int16_t zero_value_s16) {
  stage_phase_increment_u32_ = 0;
  stage_samples_left_ = 0;
  stage_slew_rate_q31_ = SlewRateFromSlewTime_q31(0);
  chiff_slew_time_log2_q5_27_ = 0;
  chiff_slew_time_at_amount_zero_q5_27_ = 0;
  chiff_slew_input_fraction_q30_ = 0;
  chiff_slew_input_max_q30_ = 0;
  chiff_drive_q4_26_ = 1 << kChiffSlewStateFractionalBits;  // drive 1.0
  chiff_amount_initial_q7_25_ = 0;
  chiff_amount_q7_25_ = 0;
  chiff_phase_q32_ = 0;
  chiff_phase_step_q32_ = 0;
  // Bias is a CONTINUOUS control, not note state -- nothing else resets it,
  // because it must survive NoteOn/NoteOff to stay smooth. Init means "from a
  // known state", so it is reset HERE and only here: a reused envelope (the
  // sim renders every note through one static) would otherwise ramp its first
  // block from the previous note's bias.
  bias_q31_ = 0;
  int32_t zero_value_q30 = zero_value_s16 << (31 - 16);
  value_without_bias_q1_30_ = zero_value_q30;
  nominal_value_q1_30_ = zero_value_q30;
  chiff_slew_state_q26_ = 0;
  stage_start_q1_30_ = zero_value_q30;
  value_floor_q1_30_ = std::min<int32_t>(zero_value_q30, 0);
  std::fill(
    &stage_target_q1_30_[0],
    &stage_target_q1_30_[ENV_NUM_STAGES],
    zero_value_q30
  );
  // A distinct sequence per instance, which is the whole requirement. The
  // stride keeps successive seeds far apart in the single xorshift orbit; the
  // OR guarantees nonzero, which is a fixed point.
  next_chiff_seed += kChiffSeedStride;
  chiff_draws_ = next_chiff_seed | 1u;
  chiff_draws_left_ = kChiffDrawsPerWord;
  Trigger(ENV_STAGE_DEAD);
}

void Envelope::NoteOff() {
  Trigger(ENV_STAGE_RELEASE);
}

// the +/- the chiff puts on the slew input. Sized from the note's
// ALLOWED range (set in NoteOn) and NOT from the level the note actually
// reaches, so the noise does not thin out at a low sustain or a low peak --
// the chiff hits the same way however hard the note is played.
//
// Making room is the MEAN's job (RenderStage holds it clear of the rails), not
// the chiff input's. Sizing the input from the room between the level and the
// rails instead would notch the excursion at the peak, where that room
// vanishes.
int32_t Envelope::ChiffSlewInput_q30() const {
  return static_cast<int32_t>(
    (static_cast<int64_t>(chiff_slew_input_max_q30_) * chiff_slew_input_fraction_q30_)
    >> 30);
}

// Duration -> the slew time that settles within it: log2(samples/4), Q5.27,
// clamped to [0, kMaxRepresentableSlewTimeLog2_q5_27]. The /4 is kSlewTimesPerStageLog2 -- four
// slew times per stage. log2 as integer bits plus a linear mantissa fraction
// (max error ~0.09, same approximation spirit as Trigger's).
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

// Slew rate 2^-slew_time, Q31; defined below. Uncapped, unlike
// SlewRateFromSlewTime_q31, which a slew that has to track a target needs.
static inline int32_t SlewRateFromTimeLog2_q31(uint32_t slew_time_log2_q5_27);

// A noise signal has no amplitude. This is the rule that gives it one.
const double kChiffAmplitudeSigmas = 3.0 / __builtin_sqrt(2.0);   // 2.121

// sigma_out = input_rms * sqrt(r / (2 - r)). Split it: root = 2^(-t/2) = sqrt(r)
// is applied per run, and this is the rest of the identity at small r. The
// series in ChiffAmplitudeGainAtSlewTime -- 1 / sqrt(1 - r/2) -- restores it
// exactly at all r.
const double kChiffSigmaPerRootAtSmallRate = 1.0 / __builtin_sqrt(2.0);

// The filter chases draw levels; their rms is this fraction of the largest.
const double kChiffDrawRmsFraction =
    static_cast<double>(kChiffDrawRmsFractionOfMax_q16) / 65536.0;

// amplitude_gain = this * root * series. Both folded factors have two consumers
// each, and applying one at a single site moves the inaudibility threshold by
// 0.7 octaves.
const uint32_t kChiffAmplitudeGainCoefficient_q31_sqrt = static_cast<uint32_t>(
  kChiffAmplitudeSigmas * kChiffSigmaPerRootAtSmallRate * kChiffDrawRmsFraction
      * kOne_q31_sqrt + 0.5);

// THE RATE IS PASSED IN, not squared out of the root. The caller already has
// 2^-t -- the loop runs on it -- and ChiffSlewInputFractionAtAmount builds the INVERSE
// of this same correction from it. Deriving r two different ways (a table read
// here, root^2 there) let the forward and inverse series disagree in their low
// bits for no reason.
static uint32_t ChiffAmplitudeGainAtSlewTime_q31_sqrt(
    uint32_t slew_time_log2_q5_27, int32_t rate_q31_in) {
  // 2^(-t/2) in Q31, then into Q15.5 at kChiffScaledRmsPerRoot.
  const uint32_t root_q31 = static_cast<uint32_t>(
    SlewRateFromTimeLog2_q31(slew_time_log2_q5_27 >> 1));
  // 1 + r/4 + 3r^2/32 in Q31.
  const uint64_t rate_q31 = static_cast<uint64_t>(
    static_cast<uint32_t>(rate_q31_in));
  const uint64_t rate_sq_q31 = (rate_q31 * rate_q31) >> 31;
  const uint64_t correction_q31 =
    (1ull << 31) + (rate_q31 >> 2) + ((3ull * rate_sq_q31) >> 5);
  const uint64_t uncorrected_q31_sqrt =
    (static_cast<uint64_t>(root_q31) * kChiffAmplitudeGainCoefficient_q31_sqrt) >> 31;
  const uint32_t amplitude_gain_q31_sqrt = static_cast<uint32_t>(
    (uncorrected_q31_sqrt * correction_q31) >> 31);
  return amplitude_gain_q31_sqrt > kOne_q31_sqrt ? kOne_q31_sqrt : amplitude_gain_q31_sqrt;
}


// Defined below (Hacker's Delight divlu); used by Rescale.
static uint32_t DivU64ByU32(uint32_t hi, uint32_t lo, uint32_t divisor);

// THE AMPLITUDE AT WHICH THE CHIFF STOPS BEING AUDIBLE, as a fraction of full
// scale. The decay's speed is set so the amount reaches it exactly at the
// nominal duration, which is what makes DURATION read true.
//   - The quantity held to it is the chiff's amplitude, so its sigma is
//     kChiffAmplitudeSigmas lower again.
//   - Written as the dB figure and converted here, not transcribed as digits.
//     __builtin_pow folds at compile time, so it costs no code and no libm.
const double kChiffInaudibleDbFs = -48.2;
const uint32_t kChiffInaudibleAmplitude_q30 = static_cast<uint32_t>(
  static_cast<double>(1u << 30)
    * __builtin_pow(10.0, kChiffInaudibleDbFs / 20.0) + 0.5);

// THE KNOB'S OWN MAPS, AT DECAY RESOLUTION. The amount passes BETWEEN knob
// positions, so these take a Q7.25 amount and interpolate where the integer
// versions index. They must stay the same maps: the whole point of the decay is
// that a decaying chiff wears the timbre the amount it is passing through would
// have as its onset, and that only holds if the decay reads the same curve the
// knob does.
static uint32_t ChiffSlewTimeAtAmount_q5_27(
    uint32_t amount_q7_25, uint32_t slew_time_at_amount_zero_q5_27) {
  if (slew_time_at_amount_zero_q5_27 <= kChiffMinSlewTimeLog2_q5_27) {
    return slew_time_at_amount_zero_q5_27;
  }
  // WHERE THE FILTER STOPS OPENING, which is NOT where the drive starts:
  // separating the two lets the corner reach its fast end before the drive
  // begins. Q16 so a non-power-of-two open point is expressible.
  const uint32_t kAmountMax_q7_25 = kChiffAmountMax << 25;
  const uint32_t kChiffSlewAmountStretch_q1_16 = static_cast<uint32_t>(
    65536.0 * (kChiffAmountMax + 1) / kChiffAmountForMinSlewTime + 0.5);
  const uint64_t doubled_q7_25 =
      (static_cast<uint64_t>(amount_q7_25) * kChiffSlewAmountStretch_q1_16) >> 16;
  const uint32_t stretched_amount_q7_25 = doubled_q7_25 > kAmountMax_q7_25
      ? kAmountMax_q7_25 : static_cast<uint32_t>(doubled_q7_25);
  const uint32_t kWarpStep = (LUT_ENV_EXPO_SIZE - 1) >> kChiffAmountBits;
  const uint32_t warp_max_u16 = lut_env_expo[kChiffAmountMax * kWarpStep];
  const uint32_t index = stretched_amount_q7_25 >> 25;
  const uint32_t frac_q25 = stretched_amount_q7_25 & ((1u << 25) - 1);
  const uint32_t lo_u16 = lut_env_expo[index * kWarpStep];
  const uint32_t hi_u16 = index < kChiffAmountMax
      ? lut_env_expo[(index + 1) * kWarpStep] : lo_u16;
  const uint32_t warped_u16 = lo_u16 + static_cast<uint32_t>(
      (static_cast<uint64_t>(hi_u16 - lo_u16) * frac_q25) >> 25);
  const uint32_t warp_expo_u16 = (warped_u16 << 16) / warp_max_u16;
  // A QUARTER OF THE WAY TOWARD A STRAIGHT LINE. The exponential
  // curve alone starts a note's darkening late (124 ms into a 687 ms chiff);
  // a straight line starts it at once but darkens the whole knob by octaves.
  // A quarter of the way there buys the first and almost none of the second.
  // >> 9 rather than a 64-bit divide: rate_amount is amount << 25, so >> 9 is
  // amount << 16, and dividing that by a 32-bit constant is a multiply. GCC 4.8
  // turns the 64-bit form into __aeabi_uldivmod, which nothing here may call.
  const uint32_t warp_linear_u16 = (stretched_amount_q7_25 >> 9) / kChiffAmountMax;
  // lut_env_expo is above the straight line everywhere, so this only subtracts.
  const uint32_t warp_u16 = warp_expo_u16 - ((warp_expo_u16 - warp_linear_u16) >> 2);
  return slew_time_at_amount_zero_q5_27 - static_cast<uint32_t>(
    (static_cast<uint64_t>(
       slew_time_at_amount_zero_q5_27 - kChiffMinSlewTimeLog2_q5_27) * warp_u16)
    >> 16);
}

// THE AMOUNT AT A POINT ON THE DECAY: initial x the curve, which is fixed for
// every chiff.
//   - The curve is lut_env_expo, the envelope's own stage curve, and it has to
//     be: amplitude goes as 20log10(amount), so constant dB per second wants
//     the amount itself to decay exponentially. Stepping the axis evenly would
//     plateau then dive, since its top half spans a few dB and its bottom few
//     units span tens.
//   - It lands on exactly zero, which is what makes the chiff converge rather
//     than merely go quiet -- a one-pole reaches zero only if what it chases
//     does.

// A RECIPROCAL, NOT A DIVIDE: GCC 4.8 does not strength-reduce a 64-bit divide
// by a constant, so the plain form calls __aeabi_uldivmod, which costs ~1.4 kB
// of library code that nothing in the check suite can see.
// A Q7.25 amount times this, high word kept, is the Q30 level the law asks
// for: 2^32 * 2^30 / 2^25 / kChiffAmountMax, i.e. 2^37 / kChiffAmountMax.
// CEIL, not round: the multiply that uses it TRUNCATES, so rounding the
// reciprocal down would bias every level low.
const uint32_t kChiffAmountRecipShift = 32 + 30 - 25;
const uint32_t kChiffAmountMaxRecip_q32 = static_cast<uint32_t>(
  ((1ull << kChiffAmountRecipShift) + kChiffAmountMax - 1) / kChiffAmountMax);

// THE AMOUNT AT WHICH THE CHIFF IS PREDICTED INAUDIBLE, Q7.25. Closed form,
// because the law makes it one:
//
//   slew_input_max * (amount / kChiffAmountMax) >= kChiffInaudibleAmplitude
//
//   - One 64/32 divide, and it evaluates none of the maps whose calibration
//     the threshold depends on.
//   - PREDICTED: where the slew input rails, the real output is below what the
//     law says, so the chiff goes inaudible EARLIER than this. The symptom is
//     DURATION reading short at the bottom of AMOUNT.
static uint32_t ChiffInaudibleAmount_q7_25(
    uint32_t start_q7_25, int32_t slew_input_max_q30) {
  if (slew_input_max_q30 <= 0) return start_q7_25;
  const uint64_t numerator = static_cast<uint64_t>(kChiffAmountMax) << 25;
  const uint64_t scaled = numerator * kChiffInaudibleAmplitude_q30;
  const uint32_t amount_q7_25 = DivU64ByU32(
    static_cast<uint32_t>(scaled >> 32), static_cast<uint32_t>(scaled),
    static_cast<uint32_t>(slew_input_max_q30));
  return amount_q7_25 < start_q7_25 ? amount_q7_25 : start_q7_25;
}

// The phase at which the curve has fallen to that amount, u16 of the duration.
// This IS the decay speed: make this phase arrive at the duration and the
// chiff goes inaudible exactly there.
//
// Inverts lut_env_expo by searching it: 257 monotone entries, so eight
// compares land on the bracket and one interpolation finishes.
//   - "Already inaudible at the onset" is an early return, not a clamped zero.
//     The phase to reach the target is zero there, and clamping to 1 would mean
//     a decay at 1/65536 speed that never crosses the axis at all.
static uint32_t ChiffInaudiblePhase_u16(
    uint32_t start_q7_25, int32_t slew_input_max_q30) {
  if (!start_q7_25) return 65536;
  const uint32_t target_q7_25 =
    ChiffInaudibleAmount_q7_25(start_q7_25, slew_input_max_q30);
  // Inaudible before the note starts: cross the axis at full speed.
  if (target_q7_25 >= start_q7_25) return 65536;
  // The remaining fraction the decay has to reach, u16. DivU64ByU32, NOT a
  // plain 64/32: GCC 4.8 turns that into __aeabi_uldivmod, which drags in
  // ~1.4 kB of library code. target < start is guaranteed above, so the
  // quotient fits u16.
  const uint32_t inaudible_amount_fraction_u16 = DivU64ByU32(
    target_q7_25 >> 16, target_q7_25 << 16, start_q7_25);
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

static uint32_t ChiffAmountAtPhase_q7_25(
    uint32_t initial_q7_25, uint32_t phase_q32) {
  const uint32_t index = phase_q32 >> 24;
  const uint32_t frac_u8 = (phase_q32 >> 16) & 0xFF;
  const uint32_t lo = lut_env_expo[index];
  const uint32_t hi = index < LUT_ENV_EXPO_SIZE - 1
    ? lut_env_expo[index + 1] : lut_env_expo[LUT_ENV_EXPO_SIZE - 1];
  const uint32_t done_u16 = lo + (((hi - lo) * frac_u8) >> 8);
  // THE NORMALISATION IS A SUBTRACT, NOT A DIVIDE. The table's last entry is
  // kEnvExpoFull, and x * 2^16 / kEnvExpoFull is EXACTLY x for every x below
  // that entry -- the quotient's fractional part only reaches 1 at the entry
  // itself.
  const uint32_t amount_fraction_u16 = done_u16 < kEnvExpoFull_u16
    ? kEnvExpoFull_u16 - done_u16 + (done_u16 ? 0 : 1) : 0;
  return static_cast<uint32_t>(
    (static_cast<uint64_t>(initial_q7_25) * amount_fraction_u16) >> 16);
}

// OCTAVES OF DRIVE PER UNIT OF AMOUNT ABOVE kChiffAmountForDriveBegin, Q32 -- one constant
// where there were two, because the two multiplies it replaces were a Q30
// round trip through the fraction of the span. Q7.25 amount times this,
// keeping the high word, is the Q5.27 exponent directly. Folded at compile
// time from the same named quantities the two-step form used.
const uint32_t kChiffDriveSlope_q32 = static_cast<uint32_t>(
  ((static_cast<uint64_t>(kChiffDriveSpanOctaves_q5_27) << 32)
   + (((kChiffAmountMax - kChiffAmountForDriveBegin) << 25) >> 1))
  / ((kChiffAmountMax - kChiffAmountForDriveBegin) << 25));
// What the reciprocal is worth at a slew time of zero, Q5.27. It is the WHOLE
// stored quantity that gets inverted, not the 1.5: kChiffScaledRmsPerRoot also
// carries kChiffDrawRmsFractionOfMax, and reading it as 1.5 alone costs 4.23 dB flat.
const uint32_t kChiffAmplitudeGainCoefficientLog2_q5_27 = static_cast<uint32_t>(
  -__builtin_log2(static_cast<double>(kChiffAmplitudeGainCoefficient_q31_sqrt)
                  / kOne_q31_sqrt) * 134217728.0 + 0.5);

// THE INPUT IS SOLVED FOR, NOT DIALLED. The law wants the OUTPUT proportional
// to the amount, and the filter reaches only a fraction of what it chases, so
// the input makes up the difference:
//
//   fraction = min(1, (amount / kChiffAmountMax) / amplitude_gain)
//
//   - Reading the input straight off the amount would make the output fall
//     twice below kChiffAmountForDriveBegin: once because the input shrank,
//     once because a slow filter reaches less of it.
//   - At and above kChiffAmountForMinSlewTime this is exactly a no-op: the
//     gain clamps at 1.0 and the expression collapses to amount / max.
//   - The min is a real bound. |chiff| <= input always, so a larger input asks
//     for an output that cannot occur; where it binds, the knob's bottom goes
//     quiet.
//
// BUILT, NOT DIVIDED -- dividing by the gain would be __aeabi_uldivmod. The
// reciprocal comes out of the same exp2 table the forward gain uses:
//
//   gain     = min(1, coefficient * 2^(-t/2) * (1 + r/4 + 3r^2/32))
//   1 / gain = max(1, (1/coefficient) * 2^(+t/2) * (1 - r/4 - r^2/32))
//
//   - 2^(+t/2) exceeds Q30 and is never materialized: with n = floor(g) and f
//     its fraction, 2^g is 2^(n+1) * 2^(f-1), one table read at (1 - f).
//   - The max() is the gain's own clamp read backwards, so no crossing has to
//     be derived.
//   - The rate is passed in; every caller already has it.
static int32_t ChiffSlewInputFractionAtAmount_q30(
    uint32_t amount_q7_25, uint32_t slew_time_log2_q5_27, int32_t rate_q31_in) {
  const uint32_t amount_fraction_q30 = static_cast<uint32_t>(
    (static_cast<uint64_t>(amount_q7_25) * kChiffAmountMaxRecip_q32) >> 32);
  // (1 - r/4 - r^2/32): ChiffAmplitudeGainAtSlewTime's own correction, inverted to
  // the same two terms so the two agree where it matters most.
  const uint32_t rate_q31 = static_cast<uint32_t>(rate_q31_in);
  const uint32_t rate_sq_q31 = static_cast<uint32_t>(
    (static_cast<uint64_t>(rate_q31) * rate_q31) >> 31);
  const uint32_t correction_q31 =
    (1u << 31) - (rate_q31 >> 2) - (rate_sq_q31 >> 5);
  const uint32_t corrected_q30 = static_cast<uint32_t>(
    (static_cast<uint64_t>(amount_fraction_q30) * correction_q31) >> 31);
  const uint32_t g_q5_27 =
    (slew_time_log2_q5_27 >> 1) + kChiffAmplitudeGainCoefficientLog2_q5_27;
  const uint32_t shift = (g_q5_27 >> 27) + 1;
  const uint32_t two_pow_f_q31 = static_cast<uint32_t>(SlewRateFromTimeLog2_q31(
    (1u << 27) - (g_q5_27 & kSlewTimeFraction_q5_27)));
  const uint32_t scaled_q30 = static_cast<uint32_t>(
    (static_cast<uint64_t>(corrected_q30) * two_pow_f_q31) >> 31);
  // |chiff| <= input always, so an input past full scale asks for an output
  // that cannot occur. Saturate in the shift's own terms, before it wraps.
  const uint32_t ceiling_q30 = shift >= 31 ? 0u : ((1u << 30) >> shift);
  if (scaled_q30 >= ceiling_q30) return 1 << 30;
  const uint32_t slew_input_fraction_q30 = scaled_q30 << shift;
  return static_cast<int32_t>(slew_input_fraction_q30 > amount_fraction_q30 ? slew_input_fraction_q30 : amount_fraction_q30);
}

static int32_t ChiffDriveAtAmount_q30(uint32_t amount_q7_25) {
  const uint32_t drive_begin_q7_25 = kChiffAmountForDriveBegin << 25;
  uint32_t drive_octaves_q5_27 = 0;
  if (amount_q7_25 > drive_begin_q7_25) {
    drive_octaves_q5_27 = static_cast<uint32_t>(
      (static_cast<uint64_t>(amount_q7_25 - drive_begin_q7_25)
       * kChiffDriveSlope_q32) >> 32);
  }
  // Measured from the ceiling, not the span: Q26 carries the division.
  return static_cast<int32_t>(
    static_cast<uint32_t>(SlewRateFromTimeLog2_q31(
      (kChiffMaxDriveOctaves << 27) - drive_octaves_q5_27)) >> 1);
}



void Envelope::NoteOn(
  ADSR& adsr,
  // Bounds stored as s32 but semantically s16
  int32_t min_target_s16, int32_t max_target_s16,
  uint8_t chiff_amount, uint32_t chiff_audible_samples
) {
  adsr_ = &adsr;
  int16_t scale_s16 = max_target_s16 - min_target_s16;
  int32_t min_target_q31 = min_target_s16 << 16;
  // NB: sustain level can be higher than peak
  stage_target_q1_30_[ENV_STAGE_ATTACK] =
    (min_target_q31 + scale_s16 * adsr.peak_u16) >> 1;
  stage_target_q1_30_[ENV_STAGE_DECAY] = stage_target_q1_30_[ENV_STAGE_SUSTAIN] =
    (min_target_q31 + scale_s16 * adsr.sustain_u16) >> 1;
  stage_target_q1_30_[ENV_STAGE_RELEASE] = stage_target_q1_30_[ENV_STAGE_DEAD] =
    min_target_q31 >> 1;
  // The note's range, as ORDERED bounds: the range may be numerically
  // inverted (CV DAC codes fall as volts rise; a warped timbre target may be
  // negative), so min/max over the stage targets, not release/peak.
  int32_t release_q30 = stage_target_q1_30_[ENV_STAGE_RELEASE];
  // The note's floor, kept only as the offset the render needs: min over the
  // stage targets, because the range may be numerically inverted.
  value_floor_q1_30_ = std::min<int32_t>(0, std::min(release_q30, std::min(
    stage_target_q1_30_[ENV_STAGE_ATTACK], stage_target_q1_30_[ENV_STAGE_SUSTAIN])));
  // half the note's ALLOWED range, in the stage targets' Q30
  // domain (a target is s16 << 15, so half the range is |scale| << 14). The
  // range may be numerically inverted, hence the magnitude.
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
      // The chiff's audible duration is a time of its own, set by CHIFF
      // DURATION, with no reference to the attack.
      uint32_t window_samples = chiff_audible_samples;
      // The nominal duration is a SIZING REFERENCE, not a countdown: it sets
      // how fast the amount falls, and the input and slew time follow it, and
      // nothing observes it elapsing. AMOUNT 0 arms nothing, which is the one
      // case that still degenerates to the classic slew by construction.
      // THE INITIAL AMOUNT IS THE LIVENESS FLAG. It is zero exactly when
      // this note has no chiff, so no separate armed/disarmed state exists to
      // fall out of step with it.
      chiff_amount_initial_q7_25_ =
        window_samples ? static_cast<uint32_t>(chiff_amount) << 25 : 0;
      if (!chiff_amount_initial_q7_25_) {
        chiff_slew_input_fraction_q30_ = 0;
        break;
      }
      // The slow end of the amount axis, which the amount descends toward. It is
      // computed first because the axis interpolates every amount FROM it.
      // Capped, so the axis stops depending on DURATION once the duration is long
      // enough for the cap to bind; see kChiffMaxSlewTimeOctaves.
      chiff_slew_time_at_amount_zero_q5_27_ = std::min(
        ChiffSlewTimeFromSamples_q5_27(window_samples),
        kChiffMaxSlewTimeLog2_q5_27);
      // The slew input is HALF THE NOTE'S ALLOWED RANGE times the fraction --
      // the largest symmetric +/- that fits, and sized from the ALLOWED range
      // rather than the realized one, so a quiet note gets the same chiff as a
      // loud one.
      //   - No drive is derived here: it is read off the amount, which starts
      //     at this note's, so the first run computes it.
      //   - DURATION needs no correction term. The drive is read off the same
      //     amount, so it relaxes as the amount descends and is gone by the
      //     time the decay lands.
      chiff_amount_q7_25_ = chiff_amount_initial_q7_25_;
      chiff_phase_q32_ = 0;
      // The amount crosses the audible part of the axis in exactly the duration;
      // the inaudible remainder is where it finishes converging to nominal.
      chiff_phase_step_q32_ = static_cast<uint32_t>(
        (static_cast<uint64_t>(0xFFFFFFFFu / window_samples)
         * ChiffInaudiblePhase_u16(chiff_amount_initial_q7_25_,
             chiff_slew_input_max_q30_)) >> 16);
      // THE DECAY'S FIRST STATE IS THE NOTE'S FIRST STATE: all three of the
      // chiff's numbers are read off the starting amount HERE. Leaving any of
      // them to the first run means entering the note on the previous note's
      // value, and the onset is the loudest, most character-defining part of
      // the chiff -- a window can be shorter than one block, so a whole chiff
      // can live inside the block that would get it wrong.
      chiff_slew_time_log2_q5_27_ = ChiffSlewTimeAtAmount_q5_27(
        chiff_amount_initial_q7_25_, chiff_slew_time_at_amount_zero_q5_27_);
      // AFTER the slew time, which the input is now solved against.
      chiff_slew_input_fraction_q30_ = ChiffSlewInputFractionAtAmount_q30(
        chiff_amount_initial_q7_25_, chiff_slew_time_log2_q5_27_,
        SlewRateFromTimeLog2_q31(chiff_slew_time_log2_q5_27_));
      break;
    }
  }
}

// Slew rate = 2^-slew_time_log2, Q31 (2^31 == 1.0). Result fits int32.
static inline int32_t SlewRateFromTimeLog2_q31(uint32_t slew_time_log2_q5_27) {
  uint32_t integer_part = slew_time_log2_q5_27 >> 27;
  uint32_t two_pow_neg_fraction_u16 = Interpolate824(
    lut_expo2_neg, (slew_time_log2_q5_27 & kSlewTimeFraction_q5_27) << 5);
  return (two_pow_neg_fraction_u16 << 15) >> integer_part;
}

// THE FASTEST A TARGET-TRACKING SLEW MAY RUN: 1 - e^-1, the true one-pole
// coefficient at a one-sample time constant.
//   - rate = 2^-t is the small-rate approximation of 1 - e^(-1/tau). It hits
//     1.0 at t = 0, where the truth is 0.632, and a rate of 1.0 is not a slew
//     -- the value arrives in one sample.
//   - Capping removes the "stage too short to slew" special case and lands a
//     short stage where every other stage lands: 1 - 0.632 IS e^-1, so a
//     4-sample stage covers 1 - e^-4 like the rest.
//   - 4 samples is the shortest stage that exists: modulate_7_13 clamps to
//     [0, 8191], so the increment table bottoms out at UINT32_MAX/4.
//   - NOT applied inside SlewRateFromTimeLog2_q31: that is a general 2^-x and
//     its other callers must reach 1.0.
const int32_t kMaxSlewRate_q31 = static_cast<int32_t>(
  (1.0 - __builtin_exp(-1.0)) * 2147483648.0 + 0.5);

static inline int32_t SlewRateFromSlewTime_q31(uint32_t slew_time_log2_q5_27) {
  const int32_t rate_q31 = SlewRateFromTimeLog2_q31(slew_time_log2_q5_27);
  return rate_q31 > kMaxSlewRate_q31 ? kMaxSlewRate_q31 : rate_q31;
}

// The chiff's audible duration, in samples. Same reciprocal the envelope
// stages use to turn a phase increment into a span.
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
  if (!chiff_slew_input_fraction_q30_) {
    stage_start_q1_30_ = nominal_value_q1_30_;
  } else if (stage_phase_increment_u32_) {
    // Phase runs 0 -> ~UINT32_MAX across the stage, but a stage that ran to
    // completion leaves stage_samples_left_ == 0, which WRAPS the product back
    // to phase 0 -- aliasing "fully elapsed" onto "not started" and anchoring
    // the new stage at the old stage's START instead of where it landed. That
    // collapses the next stage's nominal value (and yanks the value with it)
    // whenever the chiff is still live at a handoff. Saturate instead.
    uint32_t stage_phase_u32 = stage_samples_left_
      ? 0u - stage_samples_left_ * stage_phase_increment_u32_
      : UINT32_MAX;
    // No landing fraction: the slew targets past its own target, so lut_env_expo's own
    // normalization already describes where the value is.
    uint32_t expo_u16 = Interpolate824(lut_env_expo, stage_phase_u32);
    stage_start_q1_30_ += static_cast<int32_t>(
      (static_cast<int64_t>(target_q1_30_ - stage_start_q1_30_) * expo_u16) >> 16);
  } else {
    stage_start_q1_30_ = target_q1_30_;
  }
  stage_ = stage;
  target_q1_30_ = stage_target_q1_30_[stage]; // Cache against new NoteOn
  switch (stage) {
    case ENV_STAGE_ATTACK : stage_phase_increment_u32_ = adsr_->attack_u32  ; break;
    case ENV_STAGE_DECAY  : stage_phase_increment_u32_ = adsr_->decay_u32   ; break;
    case ENV_STAGE_RELEASE: stage_phase_increment_u32_ = adsr_->release_u32 ; break;
    default:
      // Hold stage: no countdown; keep slewing toward the target with the
      // rate inherited from the previous stage, converging asymptotically.
      // The chiff window, if live, keeps running on its own timetable.
      stage_phase_increment_u32_ = 0;
      return;
  }

  if (stage_start_q1_30_ == target_q1_30_) {
    // Nothing to do this stage; skip ahead
    return Trigger(static_cast<EnvelopeStage>(stage + 1));
  }

  if (!stage_phase_increment_u32_) {
    // Degenerate zero increment: treat as a hold
    return;
  }

  // Nominal stage duration in samples
  stage_samples_left_ = UINT32_MAX / stage_phase_increment_u32_;

  // Slew shift from stage duration: with N = 2^32 / increment samples and
  // k = 2^kSlewTimesPerStageLog2 time constants per stage, the time
  // constant 2^shift = N / k, i.e. shift = log2(N) - log2(k).
  // log2(N) = 32 - log2(increment); log2(increment) is approximated as
  // (31 - clz) plus a linear mantissa fraction (max error ~0.09, i.e. ~6%
  // of the time constant -- inaudible, and monotone in the increment).
  uint8_t leading_zeros = __builtin_clz(stage_phase_increment_u32_);
  // Local, not state: stage_slew_rate_q31_ below is its only reader.
  uint32_t stage_slew_time_log2_q5_27;
  if (leading_zeros >= 30) {
    // Increment <= 3: N >= ~2^30.5, whose shift saturates the cap anyway.
    // Computed separately because (leading_zeros + 1) << 27 would overflow.
    stage_slew_time_log2_q5_27 = kMaxRepresentableSlewTimeLog2_q5_27;
  } else {
    uint32_t mantissa_frac_q5_27 =
        ((stage_phase_increment_u32_ << leading_zeros) & 0x7FFFFFFFu) >> 4;
    uint32_t log2_stage_samples_q5_27 =
        (static_cast<uint32_t>(leading_zeros + 1) << 27) - mantissa_frac_q5_27;
    stage_slew_time_log2_q5_27 = log2_stage_samples_q5_27 <= kSlewTimesPerStageLog2_q5_27
      ? 0 // Floor, not a special case: slew time 0 is one sample per time
          // constant, the fastest the slew runs (see kMaxSlewRate_q31). The
          // subtraction below is unsigned, so it needs this anyway.
      : std::min(
          log2_stage_samples_q5_27 - kSlewTimesPerStageLog2_q5_27,
          kMaxRepresentableSlewTimeLog2_q5_27
        );
  }
  // THE STAGE'S RATE CHANGES ONLY HERE, so this is where it is derived: it
  // costs an exp2 table interpolation, and deriving it per run instead would
  // pay that for a quantity that moves once per stage.
  stage_slew_rate_q31_ = SlewRateFromSlewTime_q31(stage_slew_time_log2_q5_27);
  // A RELEASE CAN ONLY MAKE THE DECAY FASTER, NEVER SLOWER. CHIFF DURATION
  // owns the schedule, but a note that ends while the chiff is still audible
  // would leave noise with nothing producing it, so the release imposes a
  // deadline: be inaudible by the end of this stage.
  //   - Expressed as a phase step, not a countdown: ask what is left, divide
  //     by the samples available, and take it only if it is faster than the
  //     step already running.
  if (stage == ENV_STAGE_RELEASE && stage_samples_left_ && chiff_amount_initial_q7_25_) {
    // BOTH mechanisms get the same deadline. Speeding up only the chiff input
    // leaves the note ending with the slew still running at chiff speed, and
    // the ~2% of the stage's span that a slew has left at handoff is then
    // consumed in a fraction of a millisecond instead of gliding away over the
    // stage's own time constant -- a click at the end of every note.
    // ONE DEADLINE, ONE MECHANISM: finish the decay
    // by the release's end. The slew time and the input follow on their own,
    // because both are read off the amount reached, and the next
    // run recomputes the slew step from scratch.
    const uint32_t phase_step_q32 =
      (0xFFFFFFFFu - chiff_phase_q32_) / stage_samples_left_;
    if (phase_step_q32 > chiff_phase_step_q32_) {
      chiff_phase_step_q32_ = phase_step_q32;
    }
  }
}

void Envelope::RenderSamples(int16_t* sample_buffer, int32_t bias_target_q31) {
  // Bias is unaffected by stage change, thus has distinct lifecycle from other locals
  const int32_t bias_slope_q31 = ((bias_target_q31 >> 1) - (bias_q31_ >> 1)) >> (kAudioBlockSizeBits - 1);
  RenderStage(sample_buffer, kAudioBlockSize, bias_q31_, bias_slope_q31);
}

// Advance to the next stage and resume rendering this block's remaining
// samples there. The caller saves value_without_bias_q1_30_ first (so the re-entrant Trigger
// sees the real start value). Even with no samples left, the re-entry saves
// bias state for us.
void Envelope::HandOffToNextStage(
  int16_t* sample_buffer, size_t block_samples_left,
  int32_t bias_q31, int32_t bias_slope_q31
) {
  Trigger(static_cast<EnvelopeStage>(stage_ + 1));
  RenderStage(sample_buffer, block_samples_left, bias_q31, bias_slope_q31);
}

// ONE RENDERED SAMPLE, written once and used by every loop below so they
// cannot drift: the whole-word loop and the head/tail loop, in both the ARM
// asm and the C reference. `draw` is the raw 0..kChiffDrawValueMax field.
//
// Two independent one-poles. The chiff's chases +/- the chiff input at its own
// decaying rate; nominal chases the stage's aim at the STAGE's rate. The
// offset carrying bias and the mean correction ramps.
//
// The asm form is HAND-ALLOCATED: GCC 4.8 allocates this badly and spills, and
// presenting every live value as an operand pins them. Its behaviour is the C
// form, and the QEMU differential proves the two bit-identical. `bit_offset`
// is a string because `ubfx` needs an immediate -- one instruction where the C
// reference's shift-and-mask would be two.
#define YARNS_CHIFF_ASM_SAMPLE(bit_offset) \
  "  smull ip, lr, %[rate], %[decay]\n"       /* (rate*decay), lr = hi     */ \
  "  sub   %[rate], %[rate], lr\n"            /* rate -= (rate*decay)>>32  */ \
  "  ubfx  ip, %[draws], #" bit_offset ", %[drawbits]\n" /* one draw, low end */ \
  "  add   ip, ip, ip\n"                      /* level = 2*draw - 15, i.e. */ \
  "  sub   ip, ip, %[drawmax]\n"              /*   an odd multiple, signed */ \
  "  mul   lr, ip, %[qinput]\n"               /* what the filter chases    */ \
  "  sub   lr, lr, %[chiff]\n"                /* delta                     */ \
  "  smull ip, lr, lr, %[rate]\n"                                             \
  "  add   %[chiff], %[chiff], lr, lsl #1\n"  /* chiff += (product>>32)*2  */ \
  "  cmp   %[chiff], %[clip]\n"               /* saturating one-pole: the  */ \
  "  it    gt\n"                              /*   clipped value FEEDS     */ \
  "  movgt %[chiff], %[clip]\n"               /*   BACK, so the state can  */ \
  "  cmn   %[chiff], %[clip]\n"               /*   never carry more than   */ \
  "  it    lt\n"                              /*   it is allowed to show   */ \
  "  rsblt %[chiff], %[clip], #0\n"                                           \
  "  smull ip, lr, %[gap], %[srate]\n"        /* the gap to the aim decays */ \
  "  sub   %[gap], %[gap], lr, lsl #1\n"      /*   at the STAGE's rate     */ \
  "  add   %[comb], %[comb], %[cslope]\n"     /* bias + mean + the aim     */ \
  "  sub   ip, %[comb], %[gap]\n"             /* the mean                  */ \
  "  add   ip, ip, %[chiff], lsl %[stshift]\n" /* + the chiff, unscaled    */ \
  "  usat  ip, %[satbits], ip, asr %[sbits]\n" /* saturate and shift, 1 op */ \
  "  strh  ip, [%[buf]], #2\n"
// EVERY CONSTANT ABOVE IS AN "i" OPERAND, not a digit in a string. They were
// written out as #4, #15, #4, #15, #15 -- five duplicates of named constants in
// the hottest code in the system, with only the QEMU differential (slow, double
// emulated, not in the fast loop) standing between a renamed constant and a
// silently wrong render. "i" is an IMMEDIATE constraint, so it substitutes the
// literal and costs no register -- which matters, because the body is at the
// register ceiling exactly (a 13th "r" operand does not allocate).

// THE OPERANDS BOTH ASM BLOCKS SHARE, written once so the two lists cannot
// disagree: the QEMU differential proves asm == C, not asm == asm.
#define YARNS_CHIFF_ASM_STATE                                                 \
  [chiff] "+r"(chiff_slew_state_q26), [gap] "+r"(nominal_delta_q30),                 \
  [rate] "+r"(chiff_slew_rate_q31), [comb] "+r"(target_with_all_bias_q30),                      \
  [buf] "+r"(sample_buffer), [draws] "+r"(draws)
#define YARNS_CHIFF_ASM_INPUTS                                                \
  [decay] "r"(chiff_slew_rate_decay_q32), [qinput] "r"(chiff_driven_slew_input_scaled_q26),            \
  [clip] "r"(chiff_clip_threshold_q26), [srate] "r"(stage_slew_rate_q31),             \
  [cslope] "r"(target_with_all_bias_slope_q30),                                           \
  [drawbits] "i"(kChiffDrawBits), [drawmax] "i"(kChiffDrawValueMax),               \
  [stshift] "i"((kChiffLevelFractionalBits - kChiffSlewStateFractionalBits)), [sbits] "i"(kSampleBits),                  \
  [satbits] "i"(kOutputSaturateBits)

#define YARNS_CHIFF_RENDER_SAMPLE(draw)                                       \
  do {                                                                        \
    chiff_slew_rate_q31 -= static_cast<int32_t>(                                    \
      (static_cast<int64_t>(chiff_slew_rate_q31) * chiff_slew_rate_decay_q32) >> 32);               \
    /* (delta * rate) >> 32, DOUBLED -- i.e. the high word only, no low-word  \
     * term. Two instructions saved per one-pole. The dropped bit is a half   \
     * LSB per sample and cannot accumulate: at a one-pole's fixed point the  \
     * step is zero, so the error is bounded by the last step, not summed. */ \
    int32_t delta_q30 = (2 * (draw) - kChiffDrawValueMax)                          \
      * chiff_driven_slew_input_scaled_q26 - chiff_slew_state_q26;                          \
    chiff_slew_state_q26 += 2 * static_cast<int32_t>(                              \
      (static_cast<int64_t>(delta_q30) * chiff_slew_rate_q31) >> 32);               \
    /* The clipped value feeds back: a saturating one-pole, not a waveshaped  \
     * output. MEASURED to reach an exact square wave at 16x drive where      \
     * clipping the output only approaches one. */                            \
    if (chiff_slew_state_q26 > chiff_clip_threshold_q26) {                            \
      chiff_slew_state_q26 = chiff_clip_threshold_q26;                                \
    } else if (chiff_slew_state_q26 < -chiff_clip_threshold_q26) {                    \
      chiff_slew_state_q26 = -chiff_clip_threshold_q26;                               \
    }                                                                         \
    nominal_delta_q30 -= 2 * static_cast<int32_t>(                              \
      (static_cast<int64_t>(nominal_delta_q30) * stage_slew_rate_q31) >> 32);        \
    target_with_all_bias_q30 += target_with_all_bias_slope_q30;                                       \
    /* The asm's USAT: arithmetic shift by kSampleBits, then saturate         \
     * unsigned to kOutputSaturateBits. The upper bound is spelled from THAT   \
     * constant and not as INT16_MAX -- the two are equal today, and a twin    \
     * that agrees only by coincidence is how the pair drifts. */              \
    /* Reinterpreted as signed BEFORE the shift: the accumulator is modular,   \
     * the shift must be arithmetic to match the asm's asr, and the true value  \
     * of this sum is in int32 range. */                                        \
    int32_t sample = static_cast<int32_t>(target_with_all_bias_q30                          \
      - static_cast<uint32_t>(nominal_delta_q30)                                  \
      + static_cast<uint32_t>(chiff_slew_state_q26 << (kChiffLevelFractionalBits - kChiffSlewStateFractionalBits)))             \
      >> kSampleBits;                                                           \
    const int32_t kSampleMax = (1 << kOutputSaturateBits) - 1;                \
    if (sample < 0) sample = 0;                                               \
    if (sample > kSampleMax) sample = kSampleMax;                             \
    *sample_buffer++ = static_cast<int16_t>(sample);                          \
  } while (0)

void Envelope::RenderStage(
  int16_t* sample_buffer, size_t block_samples_left,
  int32_t bias_q31, int32_t bias_slope_q31
) {
  int32_t value_q30 = value_without_bias_q1_30_;
  int32_t nominal_value_q1_30 = nominal_value_q1_30_;
  int32_t chiff_slew_state_q26 = chiff_slew_state_q26_;

  // One straight run, bounded by the block, the stage countdown, and (while
  // live) the chiff window. Whichever expires hands off or re-enters -- once,
  // not re-checked per sample.
  const bool timed = stage_phase_increment_u32_ != 0;
  // NO CHIFF ON/OFF ANYWHERE IN HERE. There is no window to be inside of and no
  // mode to be in: with AMOUNT 0 the amount fraction is zero, so the chiff
  // input is zero, the slew stops slowing and the rate is the stage's -- every line
  // below degenerates to the classic slew on its own. Branching on it would
  // only make the BEST case cheaper, which is worth nothing here; the worst
  // case is a live chiff and it pays this cost either way.
  uint32_t run_samples = block_samples_left;
  if (timed) run_samples = std::min<uint32_t>(run_samples, stage_samples_left_);
  int16_t* const segment_end = sample_buffer + run_samples;
  const int32_t stage_target_q30 = target_q1_30_;

  {
    // BIAS IS A TERMINAL ADD. Neither one-pole's state carries it: bias enters
    // only through the offset register the loop ramps, so the envelope's own
    // trajectory is the same whatever the bias does.
    // THAT IS THE WHOLE OF THE CLAIM. The output is NOT saturate(envelope +
    // bias), because the mean clamp below holds nominal + bias clear of the
    // rails to leave the chiff room.
    // Folding bias into the integrator instead, with one clamp bounding the
    // sum, would let the clamp write bias back into the envelope: the value
    // then diverges by the bias amplitude every time the sum touches a rail.
    // The battery pins bias independence as an invariant.
    const int32_t bias_q30 = bias_q31 >> 1;
    // PER-RUN COPIES of the rate and its decay. The loop decays the rate every
    // sample; writing that back would compound the schedule once per block and
    // collapse the chiff in a few. The persistent encoding is the slew TIME.
    //   - The per-sample decay costs ~5 cycles/sample (~4% of the CPU) and
    //     earns it: dropping it leaves the SCHEDULE unchanged but coarsens the
    //     resolution to per run, and a chiff living a single block then loses
    //     its darkening entirely. Not a corner case -- the duration inherits
    //     the attack's velocity modulation.
    uint32_t slew_time_step_q5_27 = 0;
    int32_t chiff_slew_rate_decay_q32 = 0;
    // THE DECAY, ADVANCED ONCE PER RUN. All three of the chiff's axes -- the
    // slew time, the drive and the input -- are read off the amount this run
    // sits at, by the maps the knob itself uses. The loop's per-sample rate
    // decay then carries the chirp between this run's start and end amounts.
    // ALL THREE, NOT TWO: leaving the input pinned while the other two move
    // costs the pass-through invariant (up to 18 dB of input error) and stops
    // the chiff converging at all -- it parks instead of landing.
    if (chiff_amount_initial_q7_25_) {
      // SATURATE ON THE HIGH WORD AND ON THE ROOM LEFT, not on a 64-bit
      // compare: the product is the only wide quantity here, and asking
      // whether it fits is asking whether its high word is empty and its low
      // word is inside what the phase has left. The 64-bit form made GCC 4.8
      // build the sum in a register pair and compare it against a pair of
      // immediates -- a umlal, a umull and a two-word compare for a question
      // that one umull answers.
      const uint32_t phase_remaining_q32 = 0xFFFFFFFFu - chiff_phase_q32_;
      const uint64_t advanced =
        static_cast<uint64_t>(chiff_phase_step_q32_) * run_samples;
      const uint32_t advanced_q32 = static_cast<uint32_t>(advanced);
      const uint32_t phase_end_q32 =
        (advanced >> 32) == 0 && advanced_q32 < phase_remaining_q32
          ? chiff_phase_q32_ + advanced_q32 : 0xFFFFFFFFu;
      // THIS RUN'S START IS LAST RUN'S END, for both the amount and the slew
      // time: the amount is a pure function of the phase and the phase is
      // continuous, so they are the same numbers. Only the END is derived, and
      // it is carried forward. One curve evaluation and one map evaluation per
      // run instead of two and two.
      const uint32_t amount_q7_25 = chiff_amount_q7_25_;
      chiff_amount_q7_25_ =
        ChiffAmountAtPhase_q7_25(chiff_amount_initial_q7_25_, phase_end_q32);
      const uint32_t slew_time_end_q5_27 = ChiffSlewTimeAtAmount_q5_27(
        chiff_amount_q7_25_, chiff_slew_time_at_amount_zero_q5_27_);
      slew_time_step_q5_27 = run_samples
        ? (slew_time_end_q5_27 - chiff_slew_time_log2_q5_27_) / run_samples : 0;
      chiff_slew_rate_decay_q32 = ChiffSlewRateDecayFromTimeStep_q32(slew_time_step_q5_27);
      chiff_drive_q4_26_ = ChiffDriveAtAmount_q30(amount_q7_25);
      // Against THIS run's start slew time: chiff_slew_time_log2_q5_27_ still holds
      // the run's start (the writeback to the end is at the loop's tail), and
      // amount_q7_25 is the start amount, so the pair is consistent.
      chiff_slew_input_fraction_q30_ = ChiffSlewInputFractionAtAmount_q30(
        amount_q7_25, chiff_slew_time_log2_q5_27_,
        SlewRateFromTimeLog2_q31(chiff_slew_time_log2_q5_27_));
      chiff_phase_q32_ = phase_end_q32;
    }

    // The rate the loop runs on, derived here from the slew time -- the one
    // stored encoding. They are the same quantity (rate = 2^-time), and
    // keeping both as state meant two accumulators that could drift apart.
    uint32_t slew_time_q5_27 = chiff_slew_time_log2_q5_27_;
    // UNCAPPED, unlike the stage rate below: see kChiffMinSlewTimeLog2_q5_27.
    int32_t chiff_slew_rate_q31 = SlewRateFromTimeLog2_q31(slew_time_q5_27);
    const uint32_t chiff_amplitude_gain_q31_sqrt =
      ChiffAmplitudeGainAtSlewTime_q31_sqrt(slew_time_q5_27, chiff_slew_rate_q31);
    // NO SLEW-RATE FLOOR, and no chiff input rescale at it. Both existed
    // because ONE slew had to track the nominal level AND carry the chiff: if
    // the chiff's rate went below the stage's, the level stopped tracking. The
    // nominal has its own one-pole now, so the chiff's rate is free to fall as
    // far as it likes -- which is the low-pass gate the asymptotic design
    // wanted and could not have while the filter was shared.
    // WHAT NOMINAL CHASES: past the target by 1/(1 - e^-4), so it arrives ON
    // the target as the stage's countdown expires. Holds chase the target.
    int32_t stage_adjusted_target_q30 = stage_target_q30;
    if (timed) {
      stage_adjusted_target_q30 = stage_start_q1_30_ + static_cast<int32_t>(
        (static_cast<int64_t>(stage_target_q30 - stage_start_q1_30_) *
         kStageAimOvershoot_u16) >> 16);
    }
    // NOT SMMLA, which would make each one-pole two instructions instead of
    // four: SMMLA is the ARMv7E-M DSP extension (Cortex-M4). The assembler
    // rejects it for -mcpu=cortex-m3, which is what this builds for.
    const int32_t stage_slew_rate_q31 = stage_slew_rate_q31_;
    const int32_t chiff_slew_input_q30 = ChiffSlewInput_q30();
    // What the filter chases: the input driven by the character axis, in the
    // state's scaled-down domain. The drive already carries the 1/2^shift, so
    // this cannot overflow however hard it is driven.
    const int32_t chiff_driven_slew_input_q26 = static_cast<int32_t>(
      (static_cast<int64_t>(chiff_slew_input_q30) * chiff_drive_q4_26_) >> 30);
    // ONE LEVEL'S WORTH is what the loop holds, so a draw read as an odd
    // multiple (2 * draw - kChiffDrawValueMax) multiplies straight into the input it
    // chases. Dividing here rather than in the loop is what keeps the extreme
    // level EQUAL to the input, so |chiff| <= input still holds exactly and the
    // clip point below still binds where it says it does.
    // A constant divisor: GCC turns it into a multiply and a shift, once a run.
    const int32_t chiff_driven_slew_input_scaled_q26 = chiff_driven_slew_input_q26 / kChiffDrawValueMax;
    // The chiff's amplitude: the gain times the slew input. Multiplying by
    // kOne_q31_sqrt and shifting 31 avoids a 64-bit divide by 2^15.5.
    //   - Approximate, within 0.031 dB of the exact form; the clip threshold
    //     inherits that error.
    //   - Computed once per run from the run-start slew time while the rate
    //     decays within the run, so it runs generous, which is safe.
    //   - It is the amplitude the knob asked for wherever the input is not
    //     capped; where it is, this follows the bare gain instead.
    const int32_t chiff_amplitude_q30 = static_cast<int32_t>(
      (static_cast<int64_t>(chiff_slew_input_q30)
       * (chiff_amplitude_gain_q31_sqrt * kOne_q31_sqrt)) >> 31);
    // THE CLIP THRESHOLD IS ALSO HOW FAR THE MEAN IS HELD FROM EACH RAIL, so
    // a clipped chiff always fits and no peak or bias can reach a rail.
    //   - It is 2 amplitudes, i.e. 4.24 sigma, not 2.121.
    //   - min() because the chiff is bounded by the SMALLER of its slew input
    //     (the state is a convex combination of +/- it) and its own tail. The
    //     input binds when fast, the tail when slow.
    //   - The clamp does NOT feed back, which is why bias may be part of it.
    //   - Only the offset is ramped across the run: nominal is an exponential,
    //     and a linear chord over 64 samples is percent-level wrong on a
    //     409-sample stage.
    //   - When the chiff is wider than the rails allow (lo >= hi) the clamp is
    //     abandoned and the mean centred, so it clips both sides.
    //   - HOW FAR IN THE MEAN SHOULD BE HELD IS OPEN: larger trades attack level for
    //     rail headroom, and the trade is uncharacterised.
    const int32_t chiff_clip_threshold_q30 = std::min<int32_t>(
      chiff_slew_input_q30, chiff_amplitude_q30 << kChiffClipAmplitudesShift);
    // The loop runs the state scaled down, so its clip point is too.
    const int32_t chiff_clip_threshold_q26 = chiff_clip_threshold_q30 >> (kChiffLevelFractionalBits - kChiffSlewStateFractionalBits);
    const int32_t bias_slope_q30 = bias_slope_q31 >> 1;
    const int32_t mean_min_q30 = chiff_clip_threshold_q30;
    const int32_t mean_max_q30 = kValueMax_q30 - chiff_clip_threshold_q30;
    // Where nominal reaches by the run's end, for the offset's far endpoint.
    // Approximate (linear in rate * run_samples) -- it only sizes an offset
    // that is itself an approximation, and it never touches nominal's own path.
    int32_t nominal_value_end_q1_30 = nominal_value_q1_30;
    {
      const int32_t gap_q30 = stage_adjusted_target_q30 - nominal_value_q1_30;
      int64_t step = ((static_cast<int64_t>(gap_q30) * stage_slew_rate_q31) >> 31)
        * static_cast<int32_t>(run_samples);
      if ((gap_q30 >= 0 && step > gap_q30) || (gap_q30 < 0 && step < gap_q30)) {
        step = gap_q30;
      }
      nominal_value_end_q1_30 += static_cast<int32_t>(step);
    }
    const int32_t bias_end_q30 =
      bias_q30 + bias_slope_q30 * static_cast<int32_t>(run_samples);
    // UNSIGNED, and it is a modular accumulator rather than a number: the adjusted target
    // plus a full-scale bias can exceed INT32_MAX (MEASURED 2.167e9 at a slow
    // attack over a full range with the bias railed). Nothing reads it alone --
    // every use subtracts the nominal delta first, and THAT is in range -- so the
    // wrap cancels exactly. Unsigned makes the wrap defined instead of UB; the
    // two places that reinterpret the result as signed cast back below.
    uint32_t target_with_all_bias_q30, target_with_all_bias_end_q30;
    if (mean_min_q30 < mean_max_q30) {
      target_with_all_bias_q30 = static_cast<uint32_t>(
        MeanCorrection(nominal_value_q1_30 + bias_q30, mean_min_q30, mean_max_q30) + bias_q30);
      target_with_all_bias_end_q30 = static_cast<uint32_t>(
        MeanCorrection(nominal_value_end_q1_30 + bias_end_q30, mean_min_q30, mean_max_q30)
        + bias_end_q30);
    } else {
      // Chiff wider than the rails: centre it and let the output saturate.
      target_with_all_bias_q30 = static_cast<uint32_t>((kValueMax_q30 >> 1) - nominal_value_q1_30);
      target_with_all_bias_end_q30 =
        static_cast<uint32_t>((kValueMax_q30 >> 1) - nominal_value_end_q1_30);
    }
    // The difference is small and signed; the wrap in the subtraction is what
    // makes reinterpreting it as int32 give the true delta.
    const int32_t target_with_all_bias_slope_q30 = run_samples
      ? static_cast<int32_t>(target_with_all_bias_end_q30 - target_with_all_bias_q30)
          / static_cast<int32_t>(run_samples)
      : 0;
    // TRACK THE DELTA TO THE ADJUSTED TARGET, NOT THE VALUE. A one-pole on the value is
    // sub/smull/add; the same motion on the delta is a pure geometric decay,
    // smull/sub -- the shape the rate decay above already uses. The adjusted target folds
    // into the offset register, so the output is one subtract either way.
    int32_t nominal_delta_q30 = stage_adjusted_target_q30 - nominal_value_q1_30;
    target_with_all_bias_q30 += static_cast<uint32_t>(stage_adjusted_target_q30);

    // ONE WORD IS kChiffDrawsPerWord SAMPLES of draws, so a run can straddle a
    // word boundary. Chunk the loop there rather than regenerating per sample.
    // The generator state IS the current word, and how much of it is still
    // unspent carries ACROSS runs -- a block can be rendered in several.
    ChiffDrawWord draw_state = chiff_draws_;
    uint32_t draws_left = chiff_draws_left_;
    uint32_t draws =
      draw_state >> ((kChiffDrawsPerWord - draws_left) * kChiffDrawBits);
    while (sample_buffer != segment_end) {
      const uint32_t samples_left =
        static_cast<uint32_t>(segment_end - sample_buffer);
      // WHOLE WORDS DO NOT LEAVE THE LOOP: the render body pins twelve
      // registers, so a chunk loop around it SPILLS AND RELOADS every one of
      // them at each word boundary -- more than the bookkeeping it saves.
      // THE REGISTER BUDGET IS EXACTLY FOURTEEN and this is at it: eleven
      // values, the draws word, and ip/lr as scratch. The loop's end test is
      // the one operand that does not get a register -- it is read from memory
      // once per word, which costs two cycles per eight samples instead of a
      // register the body cannot spare.
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
          // CONSUME THEN ADVANCE, so the word held at entry is the one
          // rendered and the register leaves holding the next unspent word.
          // xorshift32 in place: the word IS the state, and the ubfx above
          // never writes it, so this is three instructions with no memory
          // traffic at all -- against a load per word plus the loop-end load
          // that had no register of its own.
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
    // HEAD AND TAIL ONLY: the samples that do not fill a whole word, i.e. a
    // run that starts mid-word and the last few samples of any run. Whole
    // words go through the unrolled block above, which is where the work is.
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
      // The end-of-run slew time becomes the next run's start. No bound
      // needed: the step is (end - start) / run_samples with the end taken from
      // ChiffSlewTimeAtAmount, which returns at or under the axis end, and the
      // integer divide truncates -- so this lands at or under that end. The
      // battery watches the invariant.
      chiff_slew_time_log2_q5_27_ += slew_time_step_q5_27 * run_samples;
    }

    // value_without_bias_q1_30_ is the realized envelope -- nominal plus the chiff -- and it
    // carries NO bias, so the consumers that read it (value(), tremolo(), the
    // next stage's start) see the same trajectory whatever the bias does.
    nominal_value_q1_30 = stage_adjusted_target_q30 - nominal_delta_q30;
    nominal_value_q1_30_ = nominal_value_q1_30;
    chiff_slew_state_q26_ = chiff_slew_state_q26;
    // Bounded to the note's own DAC range before anyone reads it. Nothing in
    // the render needs this -- neither one-pole integrates it, so there is no
    // windup to prevent -- but value() returns int16_t and tremolo() forms
    // (value - release target) * strength_u16 in int32, and both wrap on an
    // out-of-range value: unbounded, the product overflows int32. Bias-free,
    // so the envelope-is-bias-independent invariant still holds.
    value_q30 = nominal_value_q1_30 + (chiff_slew_state_q26 << (kChiffLevelFractionalBits - kChiffSlewStateFractionalBits));
    if (value_q30 < value_floor_q1_30_) value_q30 = value_floor_q1_30_;
    const int32_t value_top_q30 = value_floor_q1_30_ + kValueMax_q30;
    if (value_q30 > value_top_q30) value_q30 = value_top_q30;
    bias_q31 += bias_slope_q31 * static_cast<int32_t>(run_samples);
  }

  block_samples_left -= run_samples;
  value_without_bias_q1_30_ = value_q30;
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
  value_without_bias_q1_30_ = ScaleRatio(value_without_bias_q1_30_, num, den);
  target_q1_30_ = ScaleRatio(target_q1_30_, num, den);
  stage_start_q1_30_ = ScaleRatio(stage_start_q1_30_, num, den);
  // chiff_slew_input_fraction_q30_ is DIMENSIONLESS -- a fraction of the full input
  // -- so it does not scale with the levels. The full input does, being half
  // the note's allowed range.
  chiff_slew_input_max_q30_ = ScaleRatio(chiff_slew_input_max_q30_, num, den);
  // min(floor, 0) * s == min(floor * s, 0) for a non-negative s, so the offset
  // scales directly and the floor it came from need not be kept.
  value_floor_q1_30_ = ScaleRatio(value_floor_q1_30_, num, den);
  for (int i = 0; i < ENV_NUM_STAGES; ++i) {
    stage_target_q1_30_[i] = ScaleRatio(stage_target_q1_30_[i], num, den);
  }
}

}  // namespace yarns
