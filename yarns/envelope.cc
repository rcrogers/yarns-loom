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
  // Bits per sample. FOUR LEVELS OF DETAIL ARE NOT THE POINT -- SIXTEEN LEVELS
  // ARE: a filter chasing a two-level input has a two-level output once its
  // rate reaches 1, i.e. a square, so "unfiltered" and "overdriven" collide and
  // the drive has nothing left to shape (the drive above the hinge then has nothing to shape). A multi-level input makes the unfiltered
  // midpoint NOISE, which costs 4.8 dB of level against a square of the same
  // peak -- and that 4.8 dB is exactly what the drive above the hinge reclaims.
  // FOUR divides a word evenly, so a word holds a whole number of samples and
  // the chunking below stays a shift and a mask. That is ASSERTED below, not
  // just asserted here.
  const uint32_t kChiffDrawBits = 4;
  // THE WORD THE DRAWS ARE PACKED INTO. Everything below derives from this type
  // rather than from a literal 32, which is what the width used to be written
  // as -- a number that is only correct as long as nobody changes the buffer's
  // element type, with nothing to notice if they do.
  typedef uint32_t ChiffDrawWord;
  const uint32_t kBitsPerByte = 8;
  const uint32_t kChiffDrawBitsPerWord = sizeof(ChiffDrawWord) * kBitsPerByte;
  const uint32_t kChiffDrawsPerWord = kChiffDrawBitsPerWord / kChiffDrawBits;
  // A DRAW MAY NOT STRADDLE A WORD. Both render loops extract one with a single
  // ubfx at a compile-time offset, so a draw width that did not divide the word
  // would silently read across the boundary. Pre-C++11 here, hence the negative
  // array size rather than static_assert.
  typedef char kChiffDrawBitsMustDivideTheWord[
      (kChiffDrawBitsPerWord % kChiffDrawBits == 0) ? 1 : -1];
  // THE LEVELS ARE THE ODD MULTIPLES of 1/kChiffDrawMax of the chiff input:
  // level = 2 * draw - kChiffDrawMax for a draw in [0, kChiffDrawMax], so they
  // run +/-1, +/-3 ... +/-kChiffDrawMax and the set is SYMMETRIC about zero.
  // Symmetry is not a nicety: the chiff must be zero-mean, and an asymmetric
  // set (what a plain sign-extended field gives, [-8, 7]) leaves a standing
  // offset of half a level -- ~1000 LSB at full scale -- and makes the
  // symmetric state clip asymmetric about the signal it is clipping.
  // Reading the draw as an odd multiple costs one add and one subtract.
  const int32_t kChiffDrawMax = (1 << kChiffDrawBits) - 1;
  // rms/peak of that set, Q16: sqrt((4n^2 - 1)/3) / (2n - 1) for
  // n = 2^(kChiffDrawBits - 1) levels per side, i.e. sqrt(85)/15 at four bits.
  // A square is 1.0 here; this is 4.2 dB below it, and that gap IS the drive's
  // room above the hinge. Kurtosis is 1.79 against a square's 1.00.
  // sqrt((4n^2 - 1)/3) / (2n - 1) for n levels per side, DERIVED from the
  // draw width rather than fitted to it, so widening a draw moves it.
  const uint32_t kChiffDrawLevelsPerSide = 1u << (kChiffDrawBits - 1);
  const uint32_t kChiffDrawRmsPerPeak_q16 = static_cast<uint32_t>(
    65536.0 * __builtin_sqrt(
      (4.0 * kChiffDrawLevelsPerSide * kChiffDrawLevelsPerSide - 1.0) / 3.0)
      / (2.0 * kChiffDrawLevelsPerSide - 1.0) + 0.5);
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

// How far a mean must move to sit inside [lo, hi]; 0 when it already does.
inline int32_t ClampOffset(int32_t mean, int32_t lo, int32_t hi) {
  if (mean < lo) return lo - mean;
  if (mean > hi) return hi - mean;
  return 0;
}

// 1.0 in Q15.5, the unit the chiff's scaled rms is carried in.
// 1.0 in Q15.5: 2^15 * sqrt(2). __builtin_sqrt folds at compile time.
const uint32_t kOne_q15_5 = static_cast<uint32_t>(
  32768.0 * __builtin_sqrt(2.0) + 0.5);

// What lut_env_expo lands on: yarns/resources/lookup_tables.py normalises the
// table by its own maximum and scales to 65535, so its last entry IS this and
// the curve reads as a fraction of it. Naming it is what lets the walk's
// normalisation fold away (see ChiffWalkRemaining_u16) instead of dividing by
// a value re-read from the table on every call.
const uint32_t kEnvExpoFull_u16 = 65535;

// Number of slew time constants a timed stage spans, as log2 in Q5.27.
// log2(4) = 2: the stage hands off with e^-4 ~= 1.8% of its initial delta
// remaining (absorbed by the next stage's slew). Tunable by ear: larger
// front-loads the curve and lands closer to the target; smaller straightens
// the curve but leaves a bigger residual at handoff.
const uint32_t kSlewTimesPerStageLog2_q5_27 = 2u << 27;

// Caps how slow a slew may get, so the exp2 helper's `>> integer_part` stays
// well-defined. 2^27 samples is ~50 minutes at 45 kHz, already absurd.
const uint32_t kMaxSlewTimeLog2_q5_27 = 27u << 27;

// THE CHIFF'S FASTEST SLEW TIME, and it is nearly ZERO on purpose: at rate 1.0
// the one-pole's output IS its input, so the hinge is genuinely unfiltered.
// A 1/128 octave step off zero, i.e. rate 0.9946 -- unfiltered to within half a
// percent, while staying off the exact zero the exp2 helper would have to
// special-case.
// IT IS NOT kMaxSlewRate. That cap is 1 - e^-1, the true one-pole coefficient
// at a ONE-SAMPLE time constant, and it is load-bearing for the STAGE rate: it
// is what lets a 4-sample stage cover 1 - e^-4 like any other. The chiff tracks
// no target and has no such constraint, so it derives its rate UNCAPPED.
// WHAT MAKES THIS USABLE is the multi-level input: driven to rate 1 with a
// two-level input the output is a square rather than noise, so unfiltered and
// overdriven collapse into one signal and the drive has nothing left to shape.
// 1/128 octave in Q5.27.
const uint32_t kChiffFastestSlewTimeLog2_q5_27 = (1u << 27) / 128;

// chiff_amount lives in [0, kChiffAmountMax].
const uint32_t kChiffAmountBits = 7;
const uint32_t kChiffAmountMax = (1u << kChiffAmountBits) - 1;

// THE CHARACTER AXIS. Above kChiffCleanAmount the chiff is DRIVEN into its
// clip: same filter, same clip point, more signal pushed at it. The clip is a
// saturating one-pole (the clipped value feeds back), so the output squares off
// and grows louder at once -- unfiltered white noise at the hinge, a full-scale
// random square at the top. Below the hinge the drive is 1 and the clip sits at
// the signal's own natural peak, so nothing is shaped.
// A TWO-LEVEL INPUT HAS NO SUCH AXIS once the rate is uncapped: its unfiltered
// output is ALREADY the square, so there is nothing left for drive to do. The
// crest factor the sixteen levels give away is what the drive spends.
const uint32_t kChiffCleanAmount = (kChiffAmountMax + 1) / 2;
// Where the FILTER finishes opening. Independent of kChiffCleanAmount, which
// is where the DRIVE starts; equal to it only by history.
const uint32_t kChiffFilterOpenAmount = kChiffCleanAmount;
// The state is held scaled DOWN by this many bits so the driven input cannot
// leave Q30: undriven it reaches 2^29, and 16x that is 2^33. Shifting the
// state instead costs nothing, because the output add takes a shifted operand.
// Must be >= kChiffDriveSpan: the drive is stored pre-divided by it.
const uint32_t kChiffStateShift = 4;
// Octaves of drive from the hinge to full amount, Q5.27.
// CALIBRATED, NOT DERIVED, and the difference matters: the terminal the drive
// approaches is drive == kChiffDrawMax, where even the smallest level holds the
// state on the clip and the output is a random square. Containing that terminal
// exactly would want kChiffStateShift octaves. This span is SHORTER, chosen
// against the whole criteria set rather than that one -- deriving it from the
// terminal alone won on that criterion and lost on two others -- so the top of
// the knob approaches the square asymptotically instead of arriving at it.
// MUST NOT EXCEED kChiffStateShift: the drive is stored pre-divided by
// 2^kChiffStateShift, so a wider span would leave the driven input outside Q30.
const uint32_t kChiffDriveSpan_q5_27 = (5u << 27) / 2;  // 2.5 octaves
// The clip point, and the mean's reserve, is this many of the chiff's own
// sigma: 2 x the stored scaled rms, i.e. 2 * 3/sqrt(2). Sized so the clean end
// clips essentially nothing while reserving no more than it must.
const uint32_t kChiffClipRmsShift = 1;  // scaled rms << 1 == 3*sqrt(2) sigma

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
const uint32_t kStageAimOvershoot_u16 = static_cast<uint32_t>(
  65536.0 / (1.0 - __builtin_exp(-4.0)) + 0.5);

// Slew rate 2^-slew_time, Q31, capped at kMaxSlewRate for a slew that has to
// track a target; defined below, declared here because Init caches it.
static inline int32_t SlewRateFromSlewTime_q31(uint32_t slew_time_log2_q5_27);

void Envelope::Init(int16_t zero_value_s16) {
  phase_increment_u32_ = 0;
  stage_samples_left_ = 0;
  stage_slew_time_log2_q5_27_ = 0;
  stage_rate_q31_ = SlewRateFromSlewTime_q31(0);
  slew_time_log2_q5_27_ = 0;
  chiff_slew_time_log2_end_q5_27_ = 0;
  chiff_input_fraction_q30_ = 0;
  chiff_input_full_q30_ = 0;
  chiff_drive_over_16_q30_ = 1 << (30 - kChiffStateShift);
  chiff_walk_start_q7_25_ = 0;
  chiff_walk_amount_q7_25_ = 0;
  chiff_walk_phase_q32_ = 0;
  chiff_walk_phase_step_q32_ = 0;
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
  clamp_base_q30_ = std::min<int32_t>(zero_value_q30, 0);
  std::fill(
    &stage_target_q30_[0],
    &stage_target_q30_[ENV_NUM_STAGES],
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
// The same, capped at kMaxSlewRate for a slew that has to track a target.
static inline int32_t SlewRateFromSlewTime_q31(uint32_t slew_time_log2_q5_27);

// What one unit of 2^(-t/2) is worth, Q15.5. The 1.5 is 3/2 -- the 3 of the
// exact form halved by its sqrt(1/4) at small rate -- and it CARRIES TWO
// FACTORS so no call site has to.
//   the 2.121 (3/sqrt(2)), so callers get scaled rms rather than sigma;
//   kChiffDrawRmsPerPeak, because sigma = input * sqrt(r/(2-r)) reads `input`
//   as the rms of what the filter chases, which is the PEAK only for a
//   two-level input.
// The second has TWO consumers -- the mean's reserve and the walk's threshold
// -- and applying it at one alone moves the deadline by 0.7 octaves, which is
// why it is folded in here rather than at either call site.
const uint32_t kChiffScaledRmsPerRoot_q15_5 = static_cast<uint32_t>(
  1.5 * (static_cast<double>(kChiffDrawRmsPerPeak_q16) / 65536.0)
      * kOne_q15_5 + 0.5);

// THE RATE IS PASSED IN, not squared out of the root. The caller already has
// 2^-t -- the loop runs on it -- and ChiffWalkInputFraction builds the INVERSE
// of this same correction from it. Deriving r two different ways (a table read
// here, root^2 there) let the forward and inverse series disagree in their low
// bits for no reason.
static uint32_t ChiffScaledRmsPerInput_q15_5(
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
  const uint64_t uncorrected_q15_5 =
    (static_cast<uint64_t>(root_q31) * kChiffScaledRmsPerRoot_q15_5) >> 31;
  const uint32_t scaled_rms_q15_5 = static_cast<uint32_t>(
    (uncorrected_q15_5 * correction_q31) >> 31);
  return scaled_rms_q15_5 > kOne_q15_5 ? kOne_q15_5 : scaled_rms_q15_5;
}


// Defined below (Hacker's Delight divlu); used by Rescale.
static uint32_t DivU64ByU32(uint32_t hi, uint32_t lo, uint32_t divisor);

// Defined below; chiff window in samples, scaled off the attack duration.

// THE LEVEL THE WALK'S SPEED IS CALIBRATED AGAINST, as a fraction of full
// scale. It is a CALIBRATED SPEC, not a threshold of audibility despite the
// name: the walk aims to reach this level exactly at the nominal duration, so
// it is what makes DURATION read true. Fitted to the user's own die-out
// criterion (~-65 dBFS at AMOUNT 22 / DURATION 117 / ATTACK 72) landing at the
// nominal duration.
//
// The quantity held to it is the SCALED rms, so the chiff's own sigma here is
// 2.121x lower again.
//
// COMPUTED FROM THE SPEC, not transcribed: -48.2 dB is the number that was
// calibrated, so it is the number written, and the Q30 digits are derived from
// it. __builtin_pow folds at compile time with constant arguments, so this
// costs no code and pulls in no libm -- verified by the image being byte
// identical to the build that had the literal.
const double kChiffInaudibleDbFs = -48.2;
const uint32_t kChiffInaudibleLevel_q30 = static_cast<uint32_t>(
  static_cast<double>(1u << 30)
    * __builtin_pow(10.0, kChiffInaudibleDbFs / 20.0) + 0.5);

// THE KNOB'S OWN MAPS, AT WALK RESOLUTION. The walk passes BETWEEN knob
// positions, so these take a Q7.25 amount and interpolate where the integer
// versions index. They must stay the same maps: the whole point of the walk is
// that a decaying chiff wears the timbre the amount it is passing through would
// have as its onset, and that only holds if the decay reads the same curve the
// knob does.
static uint32_t ChiffWalkSlewTimeLog2_q5_27(
    uint32_t amount_q7_25, uint32_t end_slew_time_log2_q5_27) {
  if (end_slew_time_log2_q5_27 <= kChiffFastestSlewTimeLog2_q5_27) {
    return end_slew_time_log2_q5_27;
  }
  // WHERE THE FILTER STOPS OPENING, which is NOT where the drive starts. They
  // were the same constant (kChiffCleanAmount) because both happened to be 64;
  // splitting them lets the corner reach its fast end before the drive begins.
  // Q16 so a non-power-of-two open point is expressible; at 64 it is exactly 2
  // and reproduces the old doubling bit for bit.
  const uint32_t kAmountMax_q7_25 = kChiffAmountMax << 25;
  const uint32_t kOpenScale_q16 = static_cast<uint32_t>(
    65536.0 * (kChiffAmountMax + 1) / kChiffFilterOpenAmount + 0.5);
  const uint64_t doubled_q7_25 =
      (static_cast<uint64_t>(amount_q7_25) * kOpenScale_q16) >> 16;
  const uint32_t rate_amount_q7_25 = doubled_q7_25 > kAmountMax_q7_25
      ? kAmountMax_q7_25 : static_cast<uint32_t>(doubled_q7_25);
  const uint32_t kWarpStep = (LUT_ENV_EXPO_SIZE - 1) >> kChiffAmountBits;
  const uint32_t warp_max_u16 = lut_env_expo[kChiffAmountMax * kWarpStep];
  const uint32_t index = rate_amount_q7_25 >> 25;
  const uint32_t frac_q25 = rate_amount_q7_25 & ((1u << 25) - 1);
  const uint32_t lo_u16 = lut_env_expo[index * kWarpStep];
  const uint32_t hi_u16 = index < kChiffAmountMax
      ? lut_env_expo[(index + 1) * kWarpStep] : lo_u16;
  const uint32_t warped_u16 = lo_u16 + static_cast<uint32_t>(
      (static_cast<uint64_t>(hi_u16 - lo_u16) * frac_q25) >> 25);
  const uint32_t warp_u16 = (warped_u16 << 16) / warp_max_u16;
  return end_slew_time_log2_q5_27 - static_cast<uint32_t>(
    (static_cast<uint64_t>(
       end_slew_time_log2_q5_27 - kChiffFastestSlewTimeLog2_q5_27) * warp_u16)
    >> 16);
}

// HOW MUCH OF THE WALK IS LEFT at this point in the duration, u16.
//
// THE CURVE IS lut_env_expo, the envelope's own stage curve, and it has to be.
// Level goes as 20log10(amount) at the bottom of the axis, so a constant dB per
// second wants the amount itself to decay exponentially; walking the axis
// EVENLY instead plateaus and then dives, because the axis' top half spans a
// few dB while its bottom few units span tens.
//
// AND IT LANDS ON ZERO: env_expo is 1 - e^-4x normalised, so the remaining
// fraction reaches exactly 0 at the end of the duration. That is what makes the
// chiff converge rather than merely go quiet -- a one-pole reaches zero only if
// what it chases reaches zero -- and it is the same construction the ADSR
// stages use to land ON their target.
static uint32_t ChiffWalkRemaining_u16(uint32_t phase_q32) {
  const uint32_t index = phase_q32 >> 24;
  const uint32_t frac_u8 = (phase_q32 >> 16) & 0xFF;
  const uint32_t lo = lut_env_expo[index];
  const uint32_t hi = index < LUT_ENV_EXPO_SIZE - 1
    ? lut_env_expo[index + 1] : lut_env_expo[LUT_ENV_EXPO_SIZE - 1];
  const uint32_t done_u16 = lo + (((hi - lo) * frac_u8) >> 8);
  // THE NORMALISATION IS A SUBTRACT, NOT A DIVIDE. The table's last entry is
  // kEnvExpoFull, and x * 2^16 / kEnvExpoFull is EXACTLY x for every x below
  // that entry -- the quotient's fractional part only reaches 1 at the entry
  // itself. So the general form, which read the table again and then divided
  // by what it read, computes the complement and nothing else.
  return done_u16 < kEnvExpoFull_u16
    ? kEnvExpoFull_u16 - done_u16 + (done_u16 ? 0 : 1) : 0;
}

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

// THE AMOUNT WHOSE OUTPUT SITS AT THE INAUDIBILITY THRESHOLD, Q7.25.
// CLOSED FORM. It was a twelve-iteration binary search, each iteration paying
// a ChiffWalkSlewTimeLog2 and a ChiffScaledRmsPerInput, because the level used
// to be a tangle of the input and the filter's response. Under the level law
// it is not: level == amount / kChiffAmountMax exactly, so
//
//   input_full * (amount / kChiffAmountMax)  >=  kChiffInaudibleLevel
//
// solves directly for the amount. One 64/32 divide replaces the search.
//
// THE SEARCH WAS ALSO A LIABILITY, not just a cost: it made the threshold
// reachable only through twelve rounds of the very maps whose calibration was
// in question, which is how a wrong constant stayed invisible.
//
// WHERE THE CAP BINDS this is not exact -- there the level follows the bare
// response instead of the law, so the true amount is higher. The threshold sits
// well above the capped band at every setting measured; if that stops being
// true the symptom is DURATION reading long at the bottom of AMOUNT.
static uint32_t ChiffWalkAudibleAmount_q7_25(
    uint32_t start_q7_25, int32_t input_full_q30) {
  if (input_full_q30 <= 0) return start_q7_25;
  const uint64_t numerator = static_cast<uint64_t>(kChiffAmountMax) << 25;
  const uint64_t scaled = numerator * kChiffInaudibleLevel_q30;
  const uint32_t amount_q7_25 = DivU64ByU32(
    static_cast<uint32_t>(scaled >> 32), static_cast<uint32_t>(scaled),
    static_cast<uint32_t>(input_full_q30));
  return amount_q7_25 < start_q7_25 ? amount_q7_25 : start_q7_25;
}

// The phase at which the curve has fallen to that amount, u16 of the duration.
// This IS the walk's speed: make this phase arrive at the duration and the
// chiff goes inaudible exactly there.
//
// IT INVERTS lut_env_expo BY SEARCHING THE TABLE, not by bisecting the curve.
// The old form ran sixteen iterations of ChiffWalkAmount -- a table read, an
// interpolation and a 64-bit multiply each -- to inverse-interpolate a 257-entry
// MONOTONE table. Eight compares against the table itself land on the bracket,
// and one linear interpolation inside it finishes the job.
//
// "ALREADY INAUDIBLE AT THE ONSET" IS AN EARLY RETURN, NOT A CLAMPED ZERO. The
// old code ended `return phase_u16 ? phase_u16 : 1`, which turned that case --
// target == start, so the phase to reach it is zero -- into a walk running at
// 1/65536 speed -- the walk then never crosses the axis, so the chiff is never
// scheduled to decay at all. The right answer is the FASTEST walk, not the
// slowest.
static uint32_t ChiffWalkAudiblePhase_u16(
    uint32_t start_q7_25, int32_t input_full_q30) {
  if (!start_q7_25) return 65536;
  const uint32_t target_q7_25 =
    ChiffWalkAudibleAmount_q7_25(start_q7_25, input_full_q30);
  // Inaudible before the note starts: cross the axis at full speed.
  if (target_q7_25 >= start_q7_25) return 65536;
  // The remaining fraction the walk has to reach, u16. DivU64ByU32, NOT a
  // plain 64/32: GCC 4.8 turns that into __aeabi_uldivmod, and 43ac801c got
  // that helper to zero call sites in the firmware for a reason -- it drags in
  // ~1.4 kB of library code. target < start is guaranteed above, so the
  // quotient fits u16.
  const uint32_t needed_u16 = DivU64ByU32(
    target_q7_25 >> 16, target_q7_25 << 16, start_q7_25);
  // lut_env_expo rises, so the remaining fraction falls: find the last index
  // whose remaining is still >= needed.
  uint32_t lo = 0, hi = LUT_ENV_EXPO_SIZE - 1;
  while (hi - lo > 1) {
    const uint32_t mid = (lo + hi) >> 1;
    if (kEnvExpoFull_u16 - lut_env_expo[mid] >= needed_u16) lo = mid;
    else hi = mid;
  }
  const uint32_t above = kEnvExpoFull_u16 - lut_env_expo[lo];
  const uint32_t below = kEnvExpoFull_u16 - lut_env_expo[hi];
  const uint32_t span = above - below;
  const uint32_t frac_u8 = span
    ? (((above - needed_u16) << 8) / span) : 0;
  // ChiffWalkRemaining reads the index from phase >> 24 and the fraction from
  // the next byte down, so a u16 phase is exactly index:fraction.
  const uint32_t phase_u16 = (lo << 8) | (frac_u8 > 255 ? 255 : frac_u8);
  return phase_u16 ? phase_u16 : 1;
}

static uint32_t ChiffWalkAmount_q7_25(uint32_t start_q7_25, uint32_t phase_q32) {
  return static_cast<uint32_t>(
    (static_cast<uint64_t>(start_q7_25) * ChiffWalkRemaining_u16(phase_q32)) >> 16);
}

// OCTAVES OF DRIVE PER UNIT OF AMOUNT ABOVE THE HINGE, Q32 -- one constant
// where there were two, because the two multiplies it replaces were a Q30
// round trip through the fraction of the span. Q7.25 amount times this,
// keeping the high word, is the Q5.27 exponent directly. Folded at compile
// time from the same named quantities the two-step form used.
const uint32_t kChiffDriveOctavesPerAmount_q32 = static_cast<uint32_t>(
  ((static_cast<uint64_t>(kChiffDriveSpan_q5_27) << 32)
   + (((kChiffAmountMax - kChiffCleanAmount) << 25) >> 1))
  / ((kChiffAmountMax - kChiffCleanAmount) << 25));
// What the reciprocal is worth at a slew time of zero, Q5.27. It is the WHOLE
// stored quantity that gets inverted, not the 1.5: kChiffScaledRmsPerRoot also
// carries kChiffDrawRmsPerPeak, and reading it as 1.5 alone costs 4.23 dB flat.
const uint32_t kChiffLog2PerScaledRms_q5_27 = static_cast<uint32_t>(
  -__builtin_log2(static_cast<double>(kChiffScaledRmsPerRoot_q15_5)
                  / kOne_q15_5) * 134217728.0 + 0.5);

// THE INPUT IS SOLVED FOR, NOT DIALLED. The knob promises a LEVEL, and the
// level is the input times the filter's own response:
//
//   level(amount) = amount / kChiffAmountMax        <- one law, whole knob
//   input(amount) = min(1, level(amount) / response(slew_time(amount)))
//
// An input read straight off the amount makes the level fall TWICE below the
// hinge -- once because the input shrank and once because a slow filter
// realizes less of it. AT AND ABOVE THE HINGE THIS IS A NO-OP, EXACTLY: the
// slew time is kChiffFastestSlewTimeLog2 there, the response clamps at 1.0,
// and the expression collapses to amount / kChiffAmountMax.
//
// WHY LINEAR IN AMOUNT: it is the law the walk ALREADY ASSUMES, since
// ChiffWalkRemaining decays the amount exponentially on the strength of level
// being proportional to amount. Making that hold everywhere is what makes the
// decay exponential in dB across the whole knob, which is L7.
//
// THE MIN IS A REAL BOUND, NOT DEFENSIVE: |chiff| <= input always, so an input
// past full scale asks for an output that cannot occur. Where it binds, the
// level falls short of the law and the knob's bottom goes quiet again.
//
// NOT A DIVIDE BY THE RESPONSE, which GCC 4.8 would turn into
// __aeabi_uldivmod. The reciprocal is BUILT the way ChiffScaledRmsPerInput
// builds the response, out of the same exp2 table:
//
//   response      = min(1, 1.5 * 2^(-t/2) * (1 + r/4 + 3r^2/32))
//   1 / response  = max(1, (2/3) * 2^(+t/2) * (1 - r/4 - r^2/32))
//
// the bracket being that correction inverted to two terms.
//
// 2^(+t/2) EXCEEDS Q30 AND MUST NOT BE MATERIALIZED. Split the exponent: with
// n = floor(g) and f its fraction, 2^g is 2^(n+1) * 2^(f-1), and 2^(f-1) is in
// [0.5, 1) -- one read of the same table at (1 - f). The level is multiplied
// by that FIRST and shifted after, so no wide intermediate exists.
//
// THE max() IS THE RESPONSE'S OWN CLAMP READ BACKWARDS. The response saturates
// at 1.0 near the hinge, so rather than deriving that crossing and testing for
// it, let the reciprocal come out below 1.0 and take the larger. Where it does,
// the input IS the level -- the no-op at and above the hinge.
// THE RATE IS PASSED IN, not derived: every caller already has it (the run
// derives it for the loop, NoteOn for RederiveSlewState), and deriving it here
// as well would be a second read of the same table at the same argument.
static int32_t ChiffWalkInputFraction_q30(
    uint32_t amount_q7_25, uint32_t slew_time_log2_q5_27, int32_t rate_q31_in) {
  const uint32_t level_q30 = static_cast<uint32_t>(
    (static_cast<uint64_t>(amount_q7_25) * kChiffAmountMaxRecip_q32) >> 32);
  // (1 - r/4 - r^2/32): ChiffScaledRmsPerInput's own correction, inverted to
  // the same two terms so the two agree where it matters most.
  const uint32_t rate_q31 = static_cast<uint32_t>(rate_q31_in);
  const uint32_t rate_sq_q31 = static_cast<uint32_t>(
    (static_cast<uint64_t>(rate_q31) * rate_q31) >> 31);
  const uint32_t correction_q31 =
    (1u << 31) - (rate_q31 >> 2) - (rate_sq_q31 >> 5);
  const uint32_t corrected_q30 = static_cast<uint32_t>(
    (static_cast<uint64_t>(level_q30) * correction_q31) >> 31);
  const uint32_t g_q5_27 =
    (slew_time_log2_q5_27 >> 1) + kChiffLog2PerScaledRms_q5_27;
  const uint32_t shift = (g_q5_27 >> 27) + 1;
  const uint32_t two_pow_f_q31 = static_cast<uint32_t>(SlewRateFromTimeLog2_q31(
    (1u << 27) - (g_q5_27 & 0x07FFFFFFu)));
  const uint32_t scaled_q30 = static_cast<uint32_t>(
    (static_cast<uint64_t>(corrected_q30) * two_pow_f_q31) >> 31);
  // |chiff| <= input always, so an input past full scale asks for an output
  // that cannot occur. Saturate in the shift's own terms, before it wraps.
  const uint32_t ceiling_q30 = shift >= 31 ? 0u : ((1u << 30) >> shift);
  if (scaled_q30 >= ceiling_q30) return 1 << 30;
  const uint32_t input_q30 = scaled_q30 << shift;
  return static_cast<int32_t>(input_q30 > level_q30 ? input_q30 : level_q30);
}

static int32_t ChiffWalkDriveOver16_q30(uint32_t amount_q7_25) {
  const uint32_t hinge_q7_25 = kChiffCleanAmount << 25;
  uint32_t drive_octaves_q5_27 = 0;
  if (amount_q7_25 > hinge_q7_25) {
    drive_octaves_q5_27 = static_cast<uint32_t>(
      (static_cast<uint64_t>(amount_q7_25 - hinge_q7_25)
       * kChiffDriveOctavesPerAmount_q32) >> 32);
  }
  // Measured from kChiffStateShift, not from the span: the stored value is the
  // drive divided by 2^kChiffStateShift.
  return static_cast<int32_t>(
    static_cast<uint32_t>(SlewRateFromTimeLog2_q31(
      (kChiffStateShift << 27) - drive_octaves_q5_27)) >> 1);
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
  // The note's floor, kept only as the offset the render needs: min over the
  // stage targets, because the range may be numerically inverted.
  clamp_base_q30_ = std::min<int32_t>(0, std::min(release_q30, std::min(
    stage_target_q30_[ENV_STAGE_ATTACK], stage_target_q30_[ENV_STAGE_SUSTAIN])));
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
      // THE WALK'S START AMOUNT IS THE LIVENESS FLAG. It is zero exactly when
      // this note has no chiff, so no separate armed/disarmed state exists to
      // fall out of step with it.
      chiff_walk_start_q7_25_ =
        window_samples ? static_cast<uint32_t>(chiff_amount) << 25 : 0;
      if (!chiff_walk_start_q7_25_) {
        chiff_input_fraction_q30_ = 0;
        RederiveSlewState();
        break;
      }
      // The slow end of the amount axis, which the walk descends toward. It is
      // computed first because the axis interpolates every amount FROM it.
      // TRIED AND REVERTED TWICE (the second time with a working detector):
      // pinning this to a constant, so the axis is duration-independent. It
      // leaves the die-out ratio unchanged and costs level at the knob's
      // bottom, so the duration-derived end stays.
      chiff_slew_time_log2_end_q5_27_ =
        SlewTimeLog2FromDuration_q5_27(window_samples);
      // The chiff input is HALF THE NOTE'S ALLOWED RANGE times the walk's
      // fraction. Half the range is the largest symmetric +/- that can ever fit
      // inside it -- a bound, not a tuned fraction. Sized from the ALLOWED
      // range rather than the realized one, so a quiet note gets the same
      // exciter as a loud one.
      // NO DRIVE IS DERIVED HERE. It is read off the walk's amount, which
      // starts at exactly this note's amount, so the first run computes it --
      // through ChiffWalkDriveOver16, by a reciprocal multiply rather than the
      // 64-bit divide by 63 that this used to do. Nothing reads the member in
      // between, so the old form was a __aeabi_uldivmod call whose result was
      // overwritten before use; it was the last call site of that helper.
      // DURATION STAYS DURATION WITHOUT A CORRECTION TERM. The drive makes the
      // chiff louder, and the old shrink had to be given extra octaves to stop
      // it singing past its duration. The walk needs none: the drive is read
      // off the amount, so it relaxes as the amount descends and is gone by the
      // time the walk lands.
      // THE WALK IS THE WHOLE SCHEDULE. DURATION is a time-based modulation of
      // AMOUNT: the amount descends its own axis and the drive, the slew time
      // and the input are all read off it by the maps the knob itself uses, so
      // a note started at any amount decays THROUGH the states every smaller
      // amount has as its onset. Nothing else decays; there is nothing to keep
      // in step with anything.
      chiff_walk_amount_q7_25_ = chiff_walk_start_q7_25_;
      chiff_walk_phase_q32_ = 0;
      // The walk crosses the AUDIBLE part of the axis in exactly the duration;
      // the inaudible remainder is where it finishes converging to nominal.
      chiff_walk_phase_step_q32_ = static_cast<uint32_t>(
        (static_cast<uint64_t>(0xFFFFFFFFu / window_samples)
         * ChiffWalkAudiblePhase_u16(chiff_walk_start_q7_25_,
             chiff_input_full_q30_)) >> 16);
      // THE WALK'S FIRST STATE IS THE NOTE'S FIRST STATE. All three of the
      // chiff's numbers are read off the starting amount here; the slew time
      // was the one left out, so it entered the note carrying whatever the
      // PREVIOUS note had ramped it to (or, on a first note, the STAGE's slew
      // time, which is not the chiff's at all). The first run then derived its
      // step from that stale value, and only the run's END landed on the walk.
      // The onset is the loudest, most character-defining part of the chiff,
      // and a window can be shorter than one block, so a whole chiff can live
      // inside the block that would get this wrong.
      slew_time_log2_q5_27_ = ChiffWalkSlewTimeLog2_q5_27(
        chiff_walk_start_q7_25_, chiff_slew_time_log2_end_q5_27_);
      // AFTER the slew time, which the input is now solved against.
      chiff_input_fraction_q30_ = ChiffWalkInputFraction_q30(
        chiff_walk_start_q7_25_, slew_time_log2_q5_27_,
        SlewRateFromTimeLog2_q31(slew_time_log2_q5_27_));
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
// 1 - e^(-1/tau). It is exact enough everywhere the module runs, and hits its
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
// and 2^(-t/2) in the scaled rms) all need to reach 1.0.
const int32_t kMaxSlewRate_q31 = static_cast<int32_t>(
  (1.0 - __builtin_exp(-1.0)) * 2147483648.0 + 0.5);

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
uint32_t ChiffWindowSamples(
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
  const uint32_t kLn2_q28 = static_cast<uint32_t>(
    __builtin_log(2.0) * 268435456.0 + 0.5);
  int64_t u_q32 = (static_cast<int64_t>(increment_q5_27) * kLn2_q28) >> 23;
  return static_cast<int32_t>(u_q32 - ((u_q32 * u_q32) >> 33));
}

void Envelope::RederiveSlewState() {
  if (chiff_walk_start_q7_25_) {
    // NOTHING ABOUT THE CHIFF'S SCHEDULE IS DERIVED HERE ANY MORE. The step
    // and the rate decay are read off the walk at the top of every run, so
    // whatever this wrote was overwritten before the loop ran; deriving them
    // here as well was a Taylor expansion per stage change with no reader.
    // The one thing left is a bound: the slew may not be slower than the end
    // of the walk's own axis, which the run's writeback also holds it to.
    if (slew_time_log2_q5_27_ > chiff_slew_time_log2_end_q5_27_) {
      slew_time_log2_q5_27_ = chiff_slew_time_log2_end_q5_27_;
    }
  } else {
    slew_time_log2_q5_27_ = stage_slew_time_log2_q5_27_;
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
  // THE STAGE'S RATE CHANGES ONLY HERE, so this is where it is derived. The
  // run used to re-derive it from the slew time every time, which is an exp2
  // table interpolation per run for a quantity that moves once per stage.
  stage_rate_q31_ = SlewRateFromSlewTime_q31(stage_slew_time_log2_q5_27_);
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
  if (stage == ENV_STAGE_RELEASE && stage_samples_left_ && chiff_walk_start_q7_25_) {
    // BOTH mechanisms get the same deadline. Speeding up only the chiff input
    // leaves the note ending with the slew still running at chiff speed, and
    // the ~2% of the stage's span that a slew has left at handoff is then
    // consumed in a fraction of a millisecond instead of gliding away over the
    // stage's own time constant -- a click at the end of every note.
    // UNDER THE WALK THERE IS ONE DEADLINE AND ONE MECHANISM: finish the walk
    // by the release's end. The slew time and the input follow on their own,
    // because both are read off the amount the walk has reached. A second
    // deadline on the slew time used to be imposed here as well; the walk
    // recomputes the slew step from scratch on the next run, so that one had
    // no reader -- it was a divide and a Taylor expansion writing state that
    // was overwritten a few hundred cycles later.
    const uint32_t walk_step_q32 =
      (0xFFFFFFFFu - chiff_walk_phase_q32_) / stage_samples_left_;
    if (walk_step_q32 > chiff_walk_phase_step_q32_) {
      chiff_walk_phase_step_q32_ = walk_step_q32;
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

// ONE RENDERED SAMPLE, written once and used by every loop below so they
// cannot drift: the whole-word loop and the head/tail loop, in both the ARM
// asm and the C reference. `draw` is the raw 0..kChiffDrawMax field.
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

// THE OPERANDS BOTH ASM BLOCKS SHARE, written once. They were copied out by
// hand into each, eleven of them, with nothing forcing the two lists to agree
// -- and the two blocks must agree, since the QEMU differential only proves
// asm == C, not asm == asm.
#define YARNS_CHIFF_ASM_STATE                                                 \
  [chiff] "+r"(chiff_state_q30), [gap] "+r"(nominal_gap_q30),                 \
  [rate] "+r"(slew_rate_q31), [comb] "+r"(combined_q30),                      \
  [buf] "+r"(sample_buffer), [draws] "+r"(draws)
#define YARNS_CHIFF_ASM_INPUTS                                                \
  [decay] "r"(decay_q32), [qinput] "r"(chiff_input_per_level_q30),            \
  [clip] "r"(chiff_clip_scaled_q30), [srate] "r"(stage_rate_q31),             \
  [cslope] "r"(combined_slope_q30),                                           \
  [drawbits] "i"(kChiffDrawBits), [drawmax] "i"(kChiffDrawMax),               \
  [stshift] "i"(kChiffStateShift), [sbits] "i"(kSampleBits),                  \
  [satbits] "i"(kOutputSaturateBits)

#define YARNS_CHIFF_RENDER_SAMPLE(draw)                                       \
  do {                                                                        \
    slew_rate_q31 -= static_cast<int32_t>(                                    \
      (static_cast<int64_t>(slew_rate_q31) * decay_q32) >> 32);               \
    /* (delta * rate) >> 32, DOUBLED -- i.e. the high word only, no low-word  \
     * term. Two instructions saved per one-pole. The dropped bit is a half   \
     * LSB per sample and cannot accumulate: at a one-pole's fixed point the  \
     * step is zero, so the error is bounded by the last step, not summed. */ \
    int32_t delta_q30 = (2 * (draw) - kChiffDrawMax)                          \
      * chiff_input_per_level_q30 - chiff_state_q30;                          \
    chiff_state_q30 += 2 * static_cast<int32_t>(                              \
      (static_cast<int64_t>(delta_q30) * slew_rate_q31) >> 32);               \
    /* The clipped value feeds back: a saturating one-pole, not a waveshaped  \
     * output. MEASURED to reach an exact square wave at 16x drive where      \
     * clipping the output only approaches one. */                            \
    if (chiff_state_q30 > chiff_clip_scaled_q30) {                            \
      chiff_state_q30 = chiff_clip_scaled_q30;                                \
    } else if (chiff_state_q30 < -chiff_clip_scaled_q30) {                    \
      chiff_state_q30 = -chiff_clip_scaled_q30;                               \
    }                                                                         \
    nominal_gap_q30 -= 2 * static_cast<int32_t>(                              \
      (static_cast<int64_t>(nominal_gap_q30) * stage_rate_q31) >> 32);        \
    combined_q30 += combined_slope_q30;                                       \
    /* The asm's USAT: arithmetic shift by kSampleBits, then saturate         \
     * unsigned to kOutputSaturateBits. The upper bound is spelled from THAT   \
     * constant and not as INT16_MAX -- the two are equal today, and a twin    \
     * that agrees only by coincidence is how the pair drifts. */              \
    int32_t sample = (combined_q30 - nominal_gap_q30                          \
      + (chiff_state_q30 << kChiffStateShift)) >> kSampleBits;                \
    const int32_t kSampleMax = (1 << kOutputSaturateBits) - 1;                \
    if (sample < 0) sample = 0;                                               \
    if (sample > kSampleMax) sample = kSampleMax;                             \
    *sample_buffer++ = static_cast<int16_t>(sample);                          \
  } while (0)

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
  // mode to be in: with AMOUNT 0 the walk's fraction is zero, so the chiff
  // input is zero, the slew stops slowing and the rate is the stage's -- every line
  // below degenerates to the classic slew on its own. Branching on it would
  // only make the BEST case cheaper, which is worth nothing here; the worst
  // case is a live chiff and it pays this cost either way.
  uint32_t run_samples = block_samples_left;
  if (timed) run_samples = std::min<uint32_t>(run_samples, stage_samples_left_);
  int16_t* const segment_end = sample_buffer + run_samples;
  const int32_t stage_target_q30 = target_q30_;

  {
    // BIAS IS A TERMINAL ADD. Neither one-pole's state carries it: bias enters
    // only through the offset register the loop ramps, so the envelope's own
    // trajectory is the same whatever the bias does.
    // THAT IS THE WHOLE OF THE CLAIM. The output is NOT saturate(envelope +
    // bias), because the mean clamp below holds nominal + bias clear of the
    // rails to leave the chiff room.
    // Folding bias into the integrator instead, with one clamp bounding the
    // sum, let the clamp write bias back into the envelope: at rest under a
    // negative bias the state pinned at 0 and the envelope came back at +bias.
    // The value then diverged by exactly the bias amplitude every time the sum
    // touched a rail. The battery pins this as an invariant.
    const int32_t bias_q30 = bias_q31 >> 1;
    // A PER-RUN COPY. The loop decays the rate every sample; writing that back
    // would compound the schedule once per block and collapse the chiff in a
    // few of them. The persistent encoding is the slew TIME, set once below.
    //
    // THESE TWO IN-LOOP INSTRUCTIONS ARE WORTH 5 CYCLES PER SAMPLE, 24.0% ->
    // 20.2% OF THE CPU. Deleting them costs nothing to build: the writeback
    // already advances the slew time by the whole run, so the next run derives
    // an advanced rate and the decay SCHEDULE is unchanged -- only its
    // resolution, per run instead of per sample.
    // Long chiffs barely notice, but a chiff that lives a single block loses
    // its darkening ENTIRELY, and no per-run constant restores the chirp.
    // SHORT CHIFFS ARE NOT A CORNER CASE: the window inherits the attack's
    // velocity modulation, so it reaches a fraction of a millisecond at high
    // velocity on the shipped default. Rejected by ear twice.
    // Both are per-RUN: derived here, consumed by this run's loop and its
    // writeback, and nothing carries them to the next run -- it re-derives
    // them from the walk.
    uint32_t slew_time_step_q5_27 = 0;
    int32_t decay_q32 = 0;
    // THE WALK, ADVANCED ONCE PER RUN. All three of the chiff's axes -- the
    // slew time, the drive and the input -- are read off the amount this run
    // sits at, by the maps the knob itself uses. The loop's per-sample rate
    // decay then carries the chirp between this run's start and end amounts.
    // ALL THREE, NOT TWO: leaving the input pinned while the other two walk
    // costs the pass-through invariant (up to 18 dB of input error) and stops
    // the chiff converging at all -- it parks instead of landing.
    if (chiff_walk_start_q7_25_) {
      // SATURATE ON THE HIGH WORD AND ON THE ROOM LEFT, not on a 64-bit
      // compare: the product is the only wide quantity here, and asking
      // whether it fits is asking whether its high word is empty and its low
      // word is inside what the phase has left. The 64-bit form made GCC 4.8
      // build the sum in a register pair and compare it against a pair of
      // immediates -- a umlal, a umull and a two-word compare for a question
      // that one umull answers.
      const uint32_t room_q32 = 0xFFFFFFFFu - chiff_walk_phase_q32_;
      const uint64_t advanced =
        static_cast<uint64_t>(chiff_walk_phase_step_q32_) * run_samples;
      const uint32_t advanced_q32 = static_cast<uint32_t>(advanced);
      const uint32_t phase_end_q32 =
        (advanced >> 32) == 0 && advanced_q32 < room_q32
          ? chiff_walk_phase_q32_ + advanced_q32 : 0xFFFFFFFFu;
      // THIS RUN'S START IS LAST RUN'S END, for both the amount and the slew
      // time: the amount is a pure function of the phase and the phase is
      // continuous, so they are the same numbers. Only the END is derived, and
      // it is carried forward. One curve evaluation and one map evaluation per
      // run instead of two and two.
      const uint32_t amount_q7_25 = chiff_walk_amount_q7_25_;
      chiff_walk_amount_q7_25_ =
        ChiffWalkAmount_q7_25(chiff_walk_start_q7_25_, phase_end_q32);
      const uint32_t slew_time_end_q5_27 = ChiffWalkSlewTimeLog2_q5_27(
        chiff_walk_amount_q7_25_, chiff_slew_time_log2_end_q5_27_);
      slew_time_step_q5_27 = run_samples
        ? (slew_time_end_q5_27 - slew_time_log2_q5_27_) / run_samples : 0;
      decay_q32 = DecayFromIncrement_q32(slew_time_step_q5_27);
      chiff_drive_over_16_q30_ = ChiffWalkDriveOver16_q30(amount_q7_25);
      // Against THIS run's start slew time: slew_time_log2_q5_27_ still holds
      // the run's start (the writeback to the end is at the loop's tail), and
      // amount_q7_25 is the start amount, so the pair is consistent.
      chiff_input_fraction_q30_ = ChiffWalkInputFraction_q30(
        amount_q7_25, slew_time_log2_q5_27_,
        SlewRateFromTimeLog2_q31(slew_time_log2_q5_27_));
      chiff_walk_phase_q32_ = phase_end_q32;
    }

    // The chiff's scaled rms, computed once here because the mean clamp needs
    // it to know how much room to leave. About ten instructions: the sqrt and
    // the 64-bit divide belong to the exact form, which
    // ChiffScaledRmsPerInput approximates away.
    // ONE ENCODING OF THE SLEW IS STORED -- the slew TIME -- and the rate is
    // derived here, once, for the loop to run on. They are the same quantity
    // (rate = 2^-time), and keeping both as state meant keeping two
    // accumulators for it: the loop decayed the rate per sample while the
    // writeback raised the time per run, with nothing holding the two to the
    // same schedule. Derived, the rate cannot drift from it.
    uint32_t slew_time_q5_27 = slew_time_log2_q5_27_;
    // UNCAPPED, unlike the stage rate below: see kChiffFastestSlewTimeLog2.
    int32_t slew_rate_q31 = SlewRateFromTimeLog2_q31(slew_time_q5_27);
    const uint32_t chiff_scaled_rms_per_input_q15_5 =
      ChiffScaledRmsPerInput_q15_5(slew_time_q5_27, slew_rate_q31);
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
    const int32_t stage_rate_q31 = stage_rate_q31_;
    const int32_t input_q30 = ChiffInput_q30();
    // What the filter chases: the input driven by the character axis, in the
    // state's scaled-down domain. The drive already carries the 1/2^shift, so
    // this is <= input_q30 and cannot overflow however hard it is driven.
    const int32_t chiff_input_q30 = static_cast<int32_t>(
      (static_cast<int64_t>(input_q30) * chiff_drive_over_16_q30_) >> 30);
    // ONE LEVEL'S WORTH is what the loop holds, so a draw read as an odd
    // multiple (2 * draw - kChiffDrawMax) multiplies straight into the input it
    // chases. Dividing here rather than in the loop is what keeps the extreme
    // level EQUAL to the input, so |chiff| <= input still holds exactly and the
    // clip point below still binds where it says it does.
    // A constant divisor: GCC turns it into a multiply and a shift, once a run.
    const int32_t chiff_input_per_level_q30 = chiff_input_q30 / kChiffDrawMax;
    // The chiff's rms times 2.121, as a level: the per-input figure
    // times the input. Dividing by 2^15.5 would be a 64-bit division, so
    // multiply by the same constant and shift 31 instead (46341^2 is 2^31 to
    // 3 parts per million).
    // APPROXIMATE, within 0.031 dB of the exact form (see
    // ChiffScaledRmsPerInput). The margin below inherits that error.
    // Computed once per RUN from the run-start slew time, while the rate decays
    // within the run -- so it runs GENEROUS as the run proceeds, which is safe.
    // THIS IS THE LEVEL THE KNOB ASKED FOR, wherever the input is not capped.
    // The input is min(1, level / response), so input * response is
    // min(level, response * full) -- the LAW where the input has room, the bare
    // response where it is pinned at full scale. Kept as the product rather
    // than written as that min: the two differ only in rounding, and the
    // product is what the CAPPED branch -- the worst case, and the one the
    // reserve has to be right for -- needs anyway.
    const int32_t chiff_scaled_rms_q30 = static_cast<int32_t>(
      (static_cast<int64_t>(input_q30)
       * (chiff_scaled_rms_per_input_q15_5 * kOne_q15_5)) >> 31);
    // THE MEAN IS HELD ONE SCALED RMS INSIDE EACH RAIL; the chiff is then
    // added, so it has room by construction instead of being clipped.
    //  - the clamp does NOT feed back, which is why bias may be part of it.
    //    Steering the slew input instead waits on the integrator, which lags a
    //    moving bias badly.
    //  - only the OFFSET is ramped across the run. nominal is an exponential and
    //    a linear chord over 64 samples is percent-level wrong on a 409-sample
    //    stage -- envelope distortion, not rounding.
    //  - EXCEPT when the chiff is wider than the rails allow (lo >= hi): the
    //    clamp is abandoned and the mean is centred instead, so the chiff clips
    //    both sides. A larger margin reaches that regime sooner.
    //  - THE MARGIN IS 2.121 SIGMA, INHERITED RATHER THAN
    //    CHOSEN: the factor applied here is one, and the 2.121 arrives folded
    //    into what ChiffScaledRmsPerInput returns. What it SHOULD be is open --
    //    a larger margin trades attack level for rail headroom, and the trade
    //    has never been characterised across the setting space.
    // THE CLIP POINT, WHICH IS ALSO THE MEAN'S RESERVE -- one number doing
    // both jobs, so a bounded chiff always fits the headroom reserved for it
    // and rail clipping cannot happen at any peak or bias.
    // min() because the peak the chiff can reach is the SMALLER of two bounds:
    // its input (the state is a convex combination of +/- input, so it can
    // never exceed it) and its tail (3*sqrt(2) sigma). The input bound binds at
    // fast rates and the tail bound when slow. Reserving the tail bound at fast
    // rates would over-reserve badly, exactly where the chiff is loudest.
    const int32_t chiff_clip_q30 = std::min<int32_t>(
      input_q30, chiff_scaled_rms_q30 << kChiffClipRmsShift);
    // The loop runs the state scaled down, so its clip point is too.
    const int32_t chiff_clip_scaled_q30 = chiff_clip_q30 >> kChiffStateShift;
    const int32_t bias_slope_q30 = bias_slope_q31 >> 1;
    const int32_t lo_q30 = chiff_clip_q30;
    const int32_t hi_q30 = kValueMax_q30 - chiff_clip_q30;
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
      // WHOLE WORDS DO NOT LEAVE THE LOOP. Fetching the draws inside it is
      // what removes the chunk loop, and the chunk loop was not bookkeeping:
      // the render body pins twelve registers, so every one of them was
      // SPILLED AND RELOADED at each word boundary, which cost more than the
      // bookkeeping it replaced.
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
                                   & kChiffDrawMax));
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
      YARNS_CHIFF_RENDER_SAMPLE(static_cast<int32_t>(draws & kChiffDrawMax));
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
      // The walk's end-of-run slew time becomes the next run's start.
      uint32_t slew_time_log2_end = slew_time_log2_q5_27_
        + slew_time_step_q5_27 * run_samples;
      if (slew_time_log2_end > chiff_slew_time_log2_end_q5_27_) {
        slew_time_log2_end = chiff_slew_time_log2_end_q5_27_;
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
    // out-of-range value: unbounded, the product overflows int32. Bias-free,
    // so the envelope-is-bias-independent invariant still holds.
    value_q30 = nominal_q30 + (chiff_state_q30 << kChiffStateShift);
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
  value_q30_ = ScaleRatio(value_q30_, num, den);
  target_q30_ = ScaleRatio(target_q30_, num, den);
  stage_start_q30_ = ScaleRatio(stage_start_q30_, num, den);
  // chiff_input_fraction_q30_ is DIMENSIONLESS -- a fraction of the full input
  // -- so it does not scale with the levels. The full input does, being half
  // the note's allowed range.
  chiff_input_full_q30_ = ScaleRatio(chiff_input_full_q30_, num, den);
  // min(floor, 0) * s == min(floor * s, 0) for a non-negative s, so the offset
  // scales directly and the floor it came from need not be kept.
  clamp_base_q30_ = ScaleRatio(clamp_base_q30_, num, den);
  for (int i = 0; i < ENV_NUM_STAGES; ++i) {
    stage_target_q30_[i] = ScaleRatio(stage_target_q30_[i], num, den);
  }
}

}  // namespace yarns
