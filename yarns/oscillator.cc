// Copyright 2012 Emilie Gillet.
// Copyright 2021 Chris Rogers.
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
//
// -----------------------------------------------------------------------------
//
// Oscillator.

#include "yarns/oscillator.h"

#ifdef TEST
// SVF_PROBE=n reports the filter's state every nth render, for questions about
// what is IN the filter rather than what reached the output. The resonators mute
// their output with gain while still driving the filter, so a state that rings
// is invisible from outside; this is how the charge was confirmed to be real.
#include <cstdio>
#include <cstdlib>
static int g_svf_probe = getenv("SVF_PROBE") ? atoi(getenv("SVF_PROBE")) : 0;
static long g_svf_probe_n = 0;
// WHISTLE_FORCE_DRIVE=1 keeps WHISTLE's noise drive at full scale regardless of
// gain, to separate two things that happen together at a shape switch: the noise
// STOPPING, and the filter's coefficients changing meaning.
static int g_force_drive = getenv("WHISTLE_FORCE_DRIVE") ? 1 : 0;
// WHISTLE_NO_MAKEUP=1 drops the output's 1/damp_drive, to measure how much of a
// ring WHISTLE did not drive itself is that make-up.
static int g_no_makeup = getenv("WHISTLE_NO_MAKEUP") ? 1 : 0;
// WHISTLE_HALF_MAKEUP=1 spends the square root of the make-up at the output,
// flattening how much louder a tighter Q comes out, with the state untouched.
static int g_half_makeup = getenv("WHISTLE_HALF_MAKEUP") ? 1 : 0;
// WHISTLE_LIMIT_MODE=4 / WHISTLE_LIMIT_SHIFT=s throttle WHISTLE's excitation
// once a block by the ring's energy in curve units, which leaves Q alone: by
// WHISTLE_LIMIT_LAW with x = (bp^2 + lp^2) >> s as a fraction of 2^15,
// 1 = 1 - x, 2 = 1/(1 + x), 3 = 1/sqrt(1 + x).
static int g_limit_law =
    getenv("WHISTLE_LIMIT_LAW") ? atoi(getenv("WHISTLE_LIMIT_LAW")) : 1;
static int g_limit_mode =
    getenv("WHISTLE_LIMIT_MODE") ? atoi(getenv("WHISTLE_LIMIT_MODE")) : 0;
static int g_limit_shift =
    getenv("WHISTLE_LIMIT_SHIFT") ? atoi(getenv("WHISTLE_LIMIT_SHIFT")) : 16;
// CURVE_DRIVE_Q8=n scales how hard WHISTLE and PING drive the soft limiter,
// 256 = as built: to hear saturation apart from everything else a ring
// carries. The product is taken wide so a scale past 256 cannot wrap it.
static int g_curve_drive_q8 =
    getenv("CURVE_DRIVE_Q8") ? atoi(getenv("CURVE_DRIVE_Q8")) : 256;
#define TEST_CURVE_DRIVE(x) ((x) * g_curve_drive_q8 >> 8)
#else
static const int g_force_drive = 0;
static const int g_no_makeup = 0;
static const int g_half_makeup = 0;
#define TEST_CURVE_DRIVE(x) (x)
#endif

#include "stmlib/utils/dsp.h"
#include "stmlib/utils/random.h"
#include "stmlib/dsp/dsp.h"

#include "yarns/resources.h"
#include "yarns/utils.h"

namespace yarns {

using namespace stmlib;

static const size_t kNumZones = 15;

static const uint16_t kPitchTableStart = 116 * 128;
static const uint16_t kOctave = 12 * 128;
// SYNC's modulator frequency, as a multiple of the carrier's: _q3_12, so up
// to 8x. The span TIMBRE asks for is 2.67 octaves, or 6.35x.
static const int kSyncRatioFractionalBits = 12;
// CZ's modulator frequency, as a multiple of the carrier's: _u5_10 on the
// timbre channel, whose 15 bits the envelope guarantees non-negative, so up to
// 32x. The map asks for 80x and is held to this, which is what bounds the
// modulator below MIDI 72. Resolution is 3.5 cents at every pitch.
static const int kCzRatioFractionalBits = 10;

// The phase-distortion accumulator keeps 1 - 2^-this of itself every sample,
// which bounds at 2^this a DC gain an ideal integrator leaves unbounded -- its
// input carries a small pitch-dependent offset that would otherwise accumulate
// without limit. A 28 Hz corner at this value, and the LARGEST shift the render
// uses: it takes less than this as the note rises, never more.
static const int kPdLeakyIntegratorShift = 8;
// The widest damp the resonator shapes ask for: the format's own largest, which
// is Chamberlin's fully damped end. u1.14 holds 1.99994, or Q 0.50002, against
// a theoretical floor of Q 0.5 -- one LSB short of the whole useful range.
static const uint32_t kResonatorDampMax_u1_14 = 32767;
// Halvings of damp across TIMBRE, which is what takes the map to zero: the
// widest damp shifted right this many times is nothing, and a damp of nothing is
// a lossless resonator -- self-oscillation, which the timbre envelope sweeps
// THROUGH at its peak rather than parking on.
//
// Sized to the 15-bit timbre SIGNAL, not to the 7-bit TIMBRE INIT knob. The knob
// is one coarse contributor to that signal, and sizing the map to it would spend
// the top of the range on values the envelope and the LFO can already reach.
static const uint32_t kResonatorQOctaves = kEnvelopeSampleBits;

// WHISTLE's drive law is a separate quantity from the map above, and it is
// bounded where the map is not: the drive is a reciprocal at the output, so the
// map's zero would divide by it.
//   - the REFERENCE is the damp at which the drive is unity. It must be at
//     least the widest the warp can ask for, or the drive exceeds one at the
//     wide end and amplifies the excitation into the filter.
//   - the FLOOR is where the drive stops following the map, and it is stated as
//     the MAKE-UP's ceiling because that is the quantity that matters: the
//     make-up is 1/drive, and an unbounded one reached 50x on the timbre
//     envelope's slew and amplified whatever was still in the filter. A cap of
//     2^this is a floor of reference >> 2*this.
static const uint32_t kWhistleDriveReference_u1_14 = kResonatorDampMax_u1_14;
static const uint32_t kWhistleDriveMakeUpBits = 4;
static const uint32_t kWhistleDriveFloor_u1_14 =
    kResonatorDampMax_u1_14 >> (2 * kWhistleDriveMakeUpBits);
// The audio sample's peak: the magnitude the transfer gain is derived
// against, and the width the fold knee is scaled in.
static const int kSamplePeakBits = 15;
static const int kTransferMaxGainBits = 4; // 16x max gain
// Transfer peak phase (1/4 cycle = 2^30)
static const uint32_t kTransferPeakPhase = 1u << (32 - 2);
// Biased variants add a DC bias (after amplification) to shift the operating
// point on the transfer function, creating asymmetric harmonic content.
static const uint32_t kTransferAsymmetricBias = kTransferPeakPhase / 2;  // 1/8 cycle -- max asymmetry
// Order of the carrier and transfer curves within the transfer shape enum.
enum TransferCurve {
  TRANSFER_CURVE_TRI,
  TRANSFER_CURVE_SINE,
  TRANSFER_CURVE_EXP
};

/* static */
const Oscillator::RenderFn Oscillator::fn_table_[] = {
  &Oscillator::RenderFilteredNoise,
  &Oscillator::RenderFilteredNoise,
  &Oscillator::RenderFilteredNoise,
  &Oscillator::RenderFilteredNoise,
  &Oscillator::RenderWhistle,
  &Oscillator::RenderWind,
  &Oscillator::RenderBowed,
  &Oscillator::RenderPing,
  &Oscillator::RenderPing,
  &Oscillator::RenderPing,
  &Oscillator::RenderLPPulse,
  &Oscillator::RenderLPSaw,
  &Oscillator::RenderPhaseDistortionPulse,
  &Oscillator::RenderPhaseDistortionPulse,
  &Oscillator::RenderPhaseDistortionPulse,
  &Oscillator::RenderPhaseDistortionPulse,
  &Oscillator::RenderPhaseDistortionSaw,
  &Oscillator::RenderPhaseDistortionSaw,
  &Oscillator::RenderPhaseDistortionSaw,
  &Oscillator::RenderPhaseDistortionSaw,
  &Oscillator::RenderVariableSine,
  &Oscillator::RenderVariablePulse,
  &Oscillator::RenderVariableSaw,
  &Oscillator::RenderSawPulseMorph,
  &Oscillator::RenderSyncSine,
  // &Oscillator::RenderSyncTriangle,
  &Oscillator::RenderSyncPulse,
  &Oscillator::RenderSyncSaw,
  // &Oscillator::RenderFoldSine,
  // &Oscillator::RenderFoldTriangle,
  &Oscillator::RenderDiracComb,
  &Oscillator::RenderTanhSine,
  &Oscillator::RenderExponentialSine,
  &Oscillator::RenderTransfer,
  &Oscillator::RenderTransfer,
  &Oscillator::RenderTransfer,
  &Oscillator::RenderTransfer,
  &Oscillator::RenderTransfer,
  &Oscillator::RenderTransfer,
  &Oscillator::RenderTransfer,
  &Oscillator::RenderTransfer,
  &Oscillator::RenderTransfer,
  &Oscillator::RenderTransfer,
  &Oscillator::RenderTransfer,
  &Oscillator::RenderTransfer,
  &Oscillator::RenderTransfer,
  &Oscillator::RenderTransfer,
  &Oscillator::RenderTransfer,
  &Oscillator::RenderTransfer,
  &Oscillator::RenderTransfer,
  &Oscillator::RenderTransfer,
  &Oscillator::RenderFM,
};

STATIC_ASSERT(
  sizeof(Oscillator::fn_table_) / sizeof(Oscillator::RenderFn) == OSC_SHAPE_FM + 1,
  oscillator_fn_table_size_mismatch
);

void StateVariableFilter::Init() {
  SVF::Init();
  damp.Init();
  cutoff.Init();
}

void StateVariableFilter::RenderInitDamp(int16_t damp_u1_14) {
  damp.SetTarget(damp_u1_14);
  damp.ComputeSlope();
}

void StateVariableFilter::RenderInitCutoff(int16_t cutoff_u15) {
  cutoff.SetTarget(cutoff_u15);
  cutoff.ComputeSlope();
}

void Oscillator::Refresh(int16_t pitch, uint16_t pitch_frac,
                         int16_t timbre_bias, uint16_t gain_bias) {
  pitch_ = pitch;
  // if (shape_ >= OSC_SHAPE_FM) {
  //   pitch_ += lut_fm_carrier_corrections[shape_ - OSC_SHAPE_FM];
  // }
  CONSTRAIN(pitch_, 0, kHighestNote - 1);
  phase_increment_ = ComputePhaseIncrement(pitch_);
  // A pitch unit is a factor 2^(1/1536), and pitch_frac is a 16-bit fraction of
  // one -- linearised, which over a fraction of 1/128 of a semitone is exact to
  // well under a count. 7573 is 256 * 65536 * (2^(1/1536) - 1). Refresh runs
  // once a block, so the wide multiply keeps every bit for nothing.
  phase_increment_ += static_cast<uint32_t>(
      (static_cast<uint64_t>(phase_increment_ >> 16) * 7573 * pitch_frac) >> 24);
  raw_gain_bias_ = gain_bias;
  raw_timbre_bias_ = timbre_bias;
}

// The per-sample timbre is signed: NoteOn warps the destination, so a negative
// TIMBRE MOD ENVELOPE puts one below zero. A map that reads it as an absolute
// position -- a width, a cutoff, a damp -- answers its bottom there.
//
// A shape whose parameter is continuous through zero, a depth or an offset,
// does not take this.
static inline int16_t TimbreAtOrAboveZero(int16_t timbre) {
  return timbre < 0 ? 0 : timbre;
}

// Damp from a resonance control, geometrically: every shape whose resonance is
// variable reads this one map, so the control means the same thing in all of
// them. The widest damp shifted right kResonatorQOctaves times is nothing, and
// nothing is a lossless resonator -- the top of the control self-oscillates.
//
// The shift spreads the octaves over 2^kEnvelopeSampleBits and the domain is one
// short of that, so the last unit would land a hair inside the final octave and
// never reach zero. The correction is that shortfall.
static int16_t DampFromResonance(int32_t resonance_u15) {
  // Below the bottom of the map is the widest setting: the cast wraps a negative
  // value into a shift of 65527, which lands on the same zero the TOP means, at
  // the opposite end from where it was asked for.
  if (resonance_u15 < 0) resonance_u15 = 0;
  const uint32_t octaves_q16 =
      ((static_cast<uint32_t>(resonance_u15) * kResonatorQOctaves)
          << (16 - kEnvelopeSampleBits))
      + (static_cast<uint32_t>(resonance_u15) >> 10);
  const int32_t damp = static_cast<int32_t>(kResonatorDampMax_u1_14 * // 2^-octaves
      Interpolate88(lut_expo2_neg_u16, octaves_q16 & 0xffff) >> 16);
  return static_cast<int16_t>(damp >> (octaves_q16 >> 16));
}

int16_t Oscillator::WarpTimbre(
    int16_t timbre, OscillatorShape shape, int16_t pitch) const {
  // Limit cutoff range for filtered noise
  if (shape >= OSC_SHAPE_NOISE_NOTCH && shape <= OSC_SHAPE_NOISE_HP) {
    // Off the bottom below timbre -8192, where 1/8 of the range has been
    // subtracted away and the frequency goes negative -- which CutoffFromFreq
    // then shifts left into its table index, so the cutoff lands wherever the
    // wrap puts it. A negative TIMBRE MOD ENVELOPE reaches it: NoteOn warps the
    // destination, which is only constrained to int16.
    int32_t cutoff_freq = 0x1000 + (TimbreAtOrAboveZero(timbre) >> 1); // 1/8..5/8
    return SVF::CutoffFromFreq(cutoff_freq);
  }

  // LP filter cutoff tracks pitch
  if (shape >= OSC_SHAPE_LP_PULSE && shape <= OSC_SHAPE_LP_SAW) {
    int32_t cutoff_freq = (pitch >> 1) + (timbre >> 1);
    CONSTRAIN(cutoff_freq, 0, 0x7fff);
    return SVF::CutoffFromFreq(cutoff_freq);
  }

  // Phase distortion modulator tracks pitch
  if (shape >= OSC_SHAPE_CZ_PULSE_LP && shape <= OSC_SHAPE_CZ_SAW_HP) {
    // int32, because timbre - 2048 leaves int16 below timbre -30720 and wraps
    // positive there: the modulator jumps a whole map's width the wrong way,
    // which a negative TIMBRE MOD ENVELOPE reaches. Widening keeps the sweep
    // monotone instead of clamping it, because this map already runs below the
    // carrier at low timbre -- the knob's own bottom is pitch - 648 -- so
    // continuing down is what the control means.
    int32_t timbre_offset = timbre - 2048;
    int32_t shifted_pitch = pitch + (timbre_offset >> 2) + (timbre_offset >> 4) + (timbre_offset >> 8);
    if (shifted_pitch >= kHighestNote) shifted_pitch = kHighestNote - 1;
    // The ratio is taken against a playable carrier, and the pitch handed in
    // need not be one.
    int32_t carrier_pitch = pitch;
    CONSTRAIN(carrier_pitch, 0, kHighestNote - 1);
    const uint32_t carrier =
        ComputePhaseIncrement(static_cast<int16_t>(carrier_pitch));
    const uint32_t modulator = ComputePhaseIncrement(
        static_cast<int16_t>(shifted_pitch));
    int32_t ratio = static_cast<int32_t>(DivU64ByU32(
        modulator >> (32 - kCzRatioFractionalBits),
        modulator << kCzRatioFractionalBits,
        carrier));
    CONSTRAIN(ratio, 0, kEnvelopeSampleMax);
    return static_cast<int16_t>(ratio);
  }

  // Sync modulator tracks pitch
  if (shape >= OSC_SHAPE_SYNC_SINE && shape <= OSC_SHAPE_SYNC_SAW) {
    int32_t modulator_pitch = pitch + (timbre >> 3);
    CONSTRAIN(modulator_pitch, 0, kHighestNote - 1);
    // How many times the master's frequency, rather than the frequency itself.
    // A frequency has to cover the whole audible range in fifteen bits, so its
    // steps are worth 1/32768 of the top of that range wherever the note sits
    // -- at a low note that is a third of a semitone. A multiple only has to
    // cover this shape's own span, so one step is worth the same fraction of a
    // semitone at every pitch.
    const uint64_t scaled =
        static_cast<uint64_t>(ComputePhaseIncrement(modulator_pitch))
            << kSyncRatioFractionalBits;
    return static_cast<int16_t>(DivU64ByU32(
        static_cast<uint32_t>(scaled >> 32), static_cast<uint32_t>(scaled),
        ComputePhaseIncrement(pitch)));
  }

  // TIMBRE is Q: the cutoff tracks the note, so the control tightens the ring
  // instead of moving it. Carried as DAMP, geometrically, which is what lets
  // the top of the control reach zero -- the shift runs out of bits before the
  // exponential runs out of range, and zero damp is a lossless resonator.
  // TIMBRE is the loop shapes' offset, linear: RenderLoop halves it into the
  // table's units.
  if (shape == OSC_SHAPE_WIND || shape == OSC_SHAPE_BOWED) {
    return TimbreAtOrAboveZero(timbre);
  }
  if (shape >= OSC_SHAPE_WHISTLE && shape <= OSC_SHAPE_PING_HP) {
    return DampFromResonance(timbre);
  }

  if (
    shape == OSC_SHAPE_EXP_SINE ||
    (shape >= OSC_SHAPE_TRI_THRU_TRI && shape <= OSC_SHAPE_EXP_THRU_EXP_BIASED) ||
    shape >= OSC_SHAPE_FM
  ) {
    // Below zero is the bottom of the fold, which is no fold. Two things go
    // wrong without this: one path returns the timbre unchanged, so a negative
    // reaches the transfer render and is shifted left there as a value its own
    // name calls unsigned; and `knee + timbre` reaches ZERO at timbre == -knee,
    // which is a signed divide by zero.
    timbre = TimbreAtOrAboveZero(timbre);
    // Soft-knee compression: unity gain at low timbre, asymptotes to
    // pitch-dependent ceiling.  f(t) = knee * t / (knee + t), computed as
    // t - t^2/(knee + t) to avoid 32-bit overflow.
    //
    // Crest factor compensates for carrier/transfer steepness:
    // tri=2 (derivative discontinuities), sine=1, expo=3 (peak slope).
    // Combined factor is carrier * transfer.
    uint8_t crest_factor;
    if (shape >= OSC_SHAPE_TRI_THRU_TRI &&
        shape <= OSC_SHAPE_EXP_THRU_EXP_BIASED) {
      crest_factor = transfer_crest_factor_;
    } else if (shape == OSC_SHAPE_EXP_SINE) {
      crest_factor = 3;
    } else {
      crest_factor = 1;  // FM
    }
    uint32_t max_folds = 0x80000000u / ComputePhaseIncrement(pitch) / crest_factor;
    if (max_folds > 0x80000u) return timbre;
    int32_t knee = static_cast<int32_t>(max_folds << (kSamplePeakBits - kTransferMaxGainBits));
    if (knee <= 0) return 0;
    return timbre - (timbre * timbre / (knee + timbre));
  }

  return timbre;
}

const uint32_t kPhaseResetSaw[] = {
  0, // Low-pass: -cos
  0x40000000, // Peaking: sin
  0x40000000, // Band-pass: sin
  0x80000000, // High-pass: cos
};

const uint32_t kPhaseResetPulse[] = {
  0x40000000,
  0x80000000,
  0x40000000,
  0x80000000,
};

// What the wrap steps by. The window runs to zero before each reset and is full
// after it, so the reset phase decides the jump alone -- evaluated from the
// same expressions the loops use, at a full window, so the two cannot drift.
static int32_t WrapStep(int32_t reset_carrier, bool signed_arm) {
  // Unsigned for the same reason the low-pass arm is: the product is
  // 65535 * 65535 at the corner, which does not fit int32.
  return signed_arm
      ? (UINT16_MAX * reset_carrier) >> 16
      : static_cast<int32_t>(
            (static_cast<uint32_t>(UINT16_MAX) *
             static_cast<uint32_t>(reset_carrier + 32768)) >> 16);
}

void Oscillator::set_shape(OscillatorShape new_shape) {
  if (shape_ == new_shape) return;

  // Remap timbre envelope on the fly so held notes keep an ~equivalent timbre.
  // Rescale divides each level by new_scale/old_scale exactly (no soft-float);
  // it no-ops if old_scale is degenerate (the float path divided by zero).
  int16_t midpoint_timbre = 1 << 14;
  int32_t old_scale = WarpTimbre(midpoint_timbre, shape_);
  int32_t new_scale = WarpTimbre(midpoint_timbre, new_shape);
  timbre_envelope_.Rescale(new_scale, old_scale);

  // The scale moves when the new shape sums its voices differently, and a
  // held note should change shape without changing loudness.
  gain_envelope_.Rescale(gain_envelope_peak_codes_u16(new_shape),
                         gain_envelope_peak_codes_u16(shape_));

  shape_ = new_shape;

  // A silent voice takes up the new shape from rest. A noise shape keeps
  // filtering noise behind its output gate, and a resonator gates its input
  // rather than its output, so the old state would ring out of the new shape
  // as a pluck from nothing. A sounding voice keeps its state: dropping it
  // would cut the note.
  if (!sounding()) ResetFilterState();

  transfer_crest_factor_ = 1;
  if (new_shape >= OSC_SHAPE_TRI_THRU_TRI &&
      new_shape <= OSC_SHAPE_EXP_THRU_EXP_BIASED) {
    static const uint8_t slope_factor[] = {2, 1, 3}; // tri, sine, expo
    uint8_t index = new_shape - OSC_SHAPE_TRI_THRU_TRI;
    transfer_carrier_ = index % 3;
    transfer_function_ = index / 6;
    transfer_bias_ = (index % 6) >= 3 ? kTransferAsymmetricBias : 0;
    transfer_crest_factor_ = slope_factor[transfer_carrier_]
        * slope_factor[transfer_function_];
    // Halve max transfer gain when triangle is involved (carrier or transfer)
    // to compensate for its derivative discontinuities.
    transfer_gain_shift_ =
        (transfer_carrier_ == TRANSFER_CURVE_TRI ||
         transfer_function_ == TRANSFER_CURVE_TRI) ? 1 : 0;
  }
}

uint32_t Oscillator::ComputePhaseIncrement(int16_t midi_pitch) const {
  int16_t num_shifts = 0;
  while (midi_pitch >= kHighestNote) {
    midi_pitch -= kOctave;
    --num_shifts;
  }
  int16_t ref_pitch = midi_pitch;
  ref_pitch -= kPitchTableStart;
  while (ref_pitch < 0) {
    ref_pitch += kOctave;
    ++num_shifts;
  }
  
  uint32_t a = lut_oscillator_increments[ref_pitch >> 4];
  uint32_t b = lut_oscillator_increments[(ref_pitch >> 4) + 1];
  uint32_t phase_increment = a + \
      (static_cast<int32_t>(b - a) * (ref_pitch & 0xf) >> 4);
  if (num_shifts > 0) phase_increment >>= num_shifts;
  else if (num_shifts < 0) {
    // __builtin_clz, NOT clzl: identical on target, where long is 32 bits,
    // but clzl reads 32 too many on an LP64 host and the harnesses run there.
    num_shifts = std::min(__builtin_clz(phase_increment), static_cast<int>(-num_shifts));
    phase_increment <<= num_shifts;
  }
  return phase_increment;
}

// Both envelopes are evaluated up-front into stack buffers, then the wave
// render reads gain per sample and multiply-accumulates into audio_mix. The
// buffers are stack locals.
void Oscillator::Render(int16_t* audio_mix) {
  // Skipping zero-init: both buffers are fully overwritten by the
  // envelope renders below.
  // Timbre at [p], gain at [p + kAudioBlockSize]: one pointer walks both, and
  // every register held here is one the shape cannot have.
  // A shape that rings its gain takes it wide, in the same place.
  union {
    int16_t narrow[2 * kAudioBlockSize];
    struct {
      int16_t timbre[kAudioBlockSize];
      int32_t gain_q30[kAudioBlockSize];
    } wide;
  } timbre_gain;
  STATIC_ASSERT(sizeof(timbre_gain.wide.timbre)
                == kAudioBlockSize * sizeof(int16_t), wide_gain_follows_timbre);
  // The shapes are handed the WHOLE array and index the gain half off it, so
  // what they take is input_samples, not either half by itself.
  int16_t* input_samples = &timbre_gain.narrow[0];
  int16_t* gain_samples = &timbre_gain.narrow[kAudioBlockSize];
  int16_t timbre_bias = WarpTimbre(raw_timbre_bias_);
  timbre_envelope_.RenderSamples(
    input_samples, static_cast<int32_t>(static_cast<uint32_t>(timbre_bias) << 16));

  int16_t gain_bias = gain_envelope_.tremolo(raw_gain_bias_);
  const int32_t gain_bias_q31 =
    static_cast<int32_t>(static_cast<uint32_t>(gain_bias) << 16);
  const bool gain_wide = takes_wide_gain(shape_);
  if (gain_wide) {
    gain_envelope_.RenderSamples(timbre_gain.wide.gain_q30, gain_bias_q31);
  } else {
    gain_envelope_.RenderSamples(gain_samples, gain_bias_q31);
  }

#ifdef TEST
  // The hooks below read and write the narrow format whatever the shape takes.
  const int kWideGainShift = kEnvelopeValueBits - kEnvelopeSampleBits;
  // GAIN_PROBE=n prints every gain sample of the first n blocks rendered.
  static int gain_probe_blocks =
      getenv("GAIN_PROBE") ? atoi(getenv("GAIN_PROBE")) : 0;
  if (gain_probe_blocks > 0) {
    --gain_probe_blocks;
    for (size_t i = 0; i < kAudioBlockSize; ++i)
      fprintf(stderr, "%d\n", gain_wide
          ? timbre_gain.wide.gain_q30[i] >> kWideGainShift : gain_samples[i]);
  }
  // CTL_DUMP=1 prints every sample's timbre and gain, as the shape reads them.
  static const bool ctl_dump = getenv("CTL_DUMP") != NULL;
  if (ctl_dump) {
    for (size_t i = 0; i < kAudioBlockSize; ++i)
      fprintf(stderr, "%d %d\n", input_samples[i], gain_wide
          ? timbre_gain.wide.gain_q30[i] >> kWideGainShift : gain_samples[i]);
  }
  // CTL_REPLAY=path reads CTL_DUMP's format back and hands the shape THAT in
  // place of the envelopes: a timbre from one render and a gain from another.
  // One voice; past the file's end the envelopes' own stand.
  static FILE* ctl_replay =
      getenv("CTL_REPLAY") ? fopen(getenv("CTL_REPLAY"), "r") : NULL;
  if (ctl_replay) {
    for (size_t i = 0; i < kAudioBlockSize; ++i) {
      // The gain may carry a fraction: a wide gain keeps it, a narrow one
      // floors it, as the envelope's own shift does.
      int timbre;
      double gain;
      if (fscanf(ctl_replay, "%d %lf", &timbre, &gain) != 2) break;
      input_samples[i] = static_cast<int16_t>(timbre);
      if (gain_wide) {
        timbre_gain.wide.gain_q30[i] = static_cast<int32_t>(
            __builtin_floor(__builtin_ldexp(gain, kWideGainShift)));
      } else {
        gain_samples[i] = static_cast<int16_t>(__builtin_floor(gain));
      }
    }
  }
#endif
  uint8_t fn_index = shape_;
  CONSTRAIN(fn_index, 0, OSC_SHAPE_FM);
  RenderFn fn = fn_table_[fn_index];
  (this->*fn)(input_samples, audio_mix);
}

// Gain is read at input_samples[kAudioBlockSize], which is why there is no
// second parameter for it.
//
// Per-sample MAC into audio_mix: mix[i] += (this_sample * gain[i]) >> 15.
// The product shift folds into ARM's barrel-shifted ADD operand
// (add r, mix, prod, asr #15).
// The scaffolding every shape shares: the BLEP carry, the timbre read, and the
// single walk down both halves. The caller supplies mix_term because shapes
// differ in where they spend the gain envelope -- those that amplify their
// output by it take the wrapper below, and the resonators spend it on the way
// into what rings, so theirs is this_sample as it stands.
#define RENDER_CORE(mix_term, ...) \
  int32_t next_sample = next_sample_; \
  for (size_t size = kAudioBlockSize; size--;) { \
    int16_t timbre = input_samples[0]; \
    int32_t this_sample = next_sample; \
    next_sample = 0; \
    __VA_ARGS__ \
    /* Wide through the body: a band-limited edge overshoots the step it \
       corrects, and the correction's two halves only cancel that step if \
       neither wraps. Both mix terms are 16 bits -- the resonators' is \
       this_sample itself, and this_sample * gain >> 15 is at most \
       this_sample -- so it narrows here, saturating. */ \
    CONSTRAIN(this_sample, -32768, 32767) \
    int32_t mixed = (mix_term); \
    ++input_samples; \
    *audio_mix = static_cast<int16_t>(*audio_mix + mixed); \
    ++audio_mix; \
  } \
  next_sample_ = next_sample; \

// Declared after the body so it sits against the mix term that spends it:
// hoisted to the top of the loop it is live across every shape's body, which
// costs 96 bytes across thirteen of them and buys nothing.
#define RENDER_WITH_GAIN_AMPLIFYING_OUTPUT(...) \
  RENDER_CORE( \
    (static_cast<int32_t>(this_sample) * gain) >> 15, \
    __VA_ARGS__ \
    const int16_t gain = input_samples[kAudioBlockSize];) \

#define RENDER_PERIODIC(...) \
  uint32_t phase = phase_; \
  uint32_t phase_increment = phase_increment_; \
  RENDER_WITH_GAIN_AMPLIFYING_OUTPUT( \
    phase += phase_increment; \
    __VA_ARGS__ \
  ) \
  phase_ = phase; \

// NB: 'modulator' is detuned from canonical pitch. In sync, it's the
// follower/output oscillator
#define RENDER_MODULATED(...) \
  uint32_t modulator_phase = modulator_phase_; \
  RENDER_PERIODIC(__VA_ARGS__); \
  modulator_phase_ = modulator_phase; \

// True on the sample a phase accumulator wrapped, which happens at a rate of
// phase_increment / 2^32.
static inline bool PhaseWrapped(uint32_t phase, uint32_t phase_increment) {
  return phase < phase_increment;
}

// How far into this sample the edge fell, in 0..65535: the phase past the edge
// against the phase one sample covers.
//
// The fast form divides by the increment's high half, which rounds to zero for
// a modulator advancing less than 65536 phase units a sample -- reachable when
// a negative TIMBRE MOD ENVELOPE collapses the sync ratio while the follower
// sits within one increment of its wrap. UDIV answers zero for a zero divisor,
// which lands a half-scale BLEP where none belongs. Below the threshold the
// division runs at full width: the numerator is under one increment there, so
// the shift has room, and a zero increment takes the same arm as an edge a
// whole sample old.
static inline uint32_t EdgeTime(
    uint32_t phase_past_edge, uint32_t phase_increment) {
  if (phase_increment >= (1 << 16)) {
    return phase_past_edge / (phase_increment >> 16);
  }
  if (phase_past_edge >= phase_increment) return UINT16_MAX;
  return (phase_past_edge << 16) / phase_increment;
}

#define EDGES_SAW(ph, ph_incr) \
  if (!self_reset) break; \
  self_reset = false; \
  uint32_t t = EdgeTime(ph, ph_incr); \
  this_sample -= ThisBlepSample(t); \
  next_sample -= NextBlepSample(t); \

#define EDGES_PULSE(ph, ph_incr) \
  if (!high_) { \
    if (ph < pw) break; \
    uint32_t t = EdgeTime(ph - pw, ph_incr); \
    this_sample += ThisBlepSample(t); \
    next_sample += NextBlepSample(t); \
    high_ = true; \
  } \
  if (high_) { \
    if (!self_reset) break; \
    self_reset = false; \
    uint32_t t = EdgeTime(ph, ph_incr); \
    this_sample -= ThisBlepSample(t); \
    next_sample -= NextBlepSample(t); \
    high_ = false; \
  }

// The carrier is what carries the fraction, so a gliding note moves the
// modulator with it. The clamp is the range in envelope.h and nothing else:
// every value in it must produce a product this width holds.
#define SET_MODULATOR_PHASE_INCREMENT_FROM_TIMBRE \
  uint32_t modulator_phase_increment = carrier_increment_u22 * \
      static_cast<uint32_t>( \
          static_cast<uint32_t>(timbre) < widest_ratio_u5_10 \
              ? static_cast<uint32_t>(timbre) : widest_ratio_u5_10);

// The carrier scaled to pair with a raw ratio, and the widest ratio whose
// product with it still fits. Both fall out of the width; how high the
// modulator may go is a question about sound, and WarpTimbre answers it.
#define SET_CZ_RATIO_LIMITS \
  const uint32_t carrier_increment_u22 = \
      phase_increment_ >> kCzRatioFractionalBits; \
  const uint32_t widest_ratio_u5_10 = carrier_increment_u22 \
      ? UINT32_MAX / carrier_increment_u22 : UINT32_MAX;

// SYNC's timbre is a multiple of the carrier's frequency, so the modulator's
// increment is the carrier's scaled by it.
#define SET_MODULATOR_PHASE_INCREMENT_FROM_RATIO \
  uint32_t modulator_phase_increment = static_cast<uint32_t>( \
      (static_cast<uint64_t>(phase_increment) * \
       static_cast<uint32_t>(timbre)) >> kSyncRatioFractionalBits);

#define SYNC(discontinuity_code, edges_code, extra_transition_code) \
  bool sync_reset = false; \
  bool self_reset = false; \
  bool transition_during_reset = false; \
  uint32_t reset_time = 0; \
  SET_MODULATOR_PHASE_INCREMENT_FROM_RATIO; \
  if (PhaseWrapped(phase, phase_increment)) { \
    sync_reset = true; \
    reset_time = FractionU32(phase, phase_increment) >> 16; \
    uint32_t modulator_phase_at_reset = modulator_phase + \
      (65535 - reset_time) * (modulator_phase_increment >> 16); \
    if (modulator_phase_at_reset < modulator_phase || (extra_transition_code)) { \
      transition_during_reset = true; \
    } \
    int32_t discontinuity = (discontinuity_code); \
    this_sample += discontinuity * ThisBlepSample(reset_time) >> 15; \
    next_sample += discontinuity * NextBlepSample(reset_time) >> 15; \
  } \
  modulator_phase += modulator_phase_increment; \
  self_reset = PhaseWrapped(modulator_phase, modulator_phase_increment); \
  /* Block additional BLEP if modulator was reset by master alone */ \
  bool reset_by_master_only = sync_reset && !transition_during_reset; \
  /* HOISTED BY HAND, because -fno-move-loop-invariants means GCC will not:
   * nothing in edges_code writes reset_by_master_only, so the test was
   * loop-invariant and re-run every edge, and the value had to stay live
   * across a body that already spills. The loop only ever leaves by break,
   * so guarding it is the same program. */ \
  if (!reset_by_master_only) { \
    while (true) { \
      edges_code; \
    } \
  } \
  if (sync_reset) { \
    modulator_phase = reset_time * (modulator_phase_increment >> 16); \
    high_ = false; \
  } \

void Oscillator::RenderLPPulse(int16_t* input_samples, int16_t* audio_mix) {
  StateVariableFilter svf = svf_;
  svf.RenderInitDamp(126); // Q 130: a sharp peak, 19x the saw's
  uint32_t pw = 0x80000000;
  RENDER_PERIODIC(
    bool self_reset = PhaseWrapped(phase, phase_increment);
    while (true) { EDGES_PULSE(phase, phase_increment) }
    next_sample += phase < pw ? 0 : 0x7fff;
    this_sample = svf.RenderSample<SVF_LP>(this_sample, timbre);
  )
  svf_ = svf;
}

void Oscillator::RenderLPSaw(int16_t* input_samples, int16_t* audio_mix) {
  StateVariableFilter svf = svf_;
  svf.RenderInitDamp(2391); // Q 6.9: a gentle one, against the pulse's 130
  RENDER_PERIODIC(
    bool self_reset = PhaseWrapped(phase, phase_increment);
    while (true) { EDGES_SAW(phase, phase_increment) }
    next_sample += phase >> 17;
    this_sample = svf.RenderSample<SVF_LP>(this_sample, timbre);
  )
  svf_ = svf;
}

// One cycle compressed into `width` of the period, then held at the value the
// cycle ends on, which for a sine is zero. Nothing is discontinuous at either
// end, so there is no edge to BLEP: what TIMBRE sweeps is a formant over the
// silence.
void Oscillator::RenderVariableSine(int16_t* input_samples, int16_t* audio_mix) {
  RENDER_PERIODIC(
    // The formant sits at 1/width the fundamental, which is what the knob
    // chooses. Squared, so the onset is gentle: the bottom quarter of the knob
    // stays within 12% of a plain sine. Half the table puts the top at 8.4x,
    // under Nyquist to MIDI 100.
    timbre = TimbreAtOrAboveZero(timbre);
    uint16_t index = static_cast<uint16_t>(timbre * timbre >> 15);
    uint16_t width = UINT16_MAX - Interpolate88(lut_env_expo_u16, index); // 100-12%
    // A width of zero fails the compare rather than reaching the divide.
    this_sample = (phase >> 16) < width ? sine((phase / width) << 16) : 0;
  )
}

void Oscillator::RenderVariablePulse(int16_t* input_samples, int16_t* audio_mix) {
  RENDER_PERIODIC(
    timbre = TimbreAtOrAboveZero(timbre);
    timbre = timbre + (timbre >> 1); // 3/4
    uint32_t pw = (UINT16_MAX - Interpolate88(lut_env_expo_u16, timbre)) << 15; // 50-0%
    bool self_reset = PhaseWrapped(phase, phase_increment);
    while (true) { EDGES_PULSE(phase, phase_increment) }
    next_sample += phase < pw ? 0 : 0x7fff;
    // * 2 and not << 1: the value is signed and negative below 0x4000,
    // and shifting a negative left is undefined. Same instruction.
    this_sample = (this_sample - 0x4000) * 2;
  )
}

void Oscillator::RenderVariableSaw(int16_t* input_samples, int16_t* audio_mix) {
  RENDER_PERIODIC(
    bool self_reset = PhaseWrapped(phase, phase_increment);
    while (true) { EDGES_SAW(phase, phase_increment) }
    timbre = TimbreAtOrAboveZero(timbre);
    timbre = timbre + (timbre >> 1); // 3/4
    uint16_t saw_width = UINT16_MAX - Interpolate88(lut_env_expo_u16, timbre); // 100-0%
    if ((phase >> 16) < saw_width) next_sample += (phase / saw_width) >> 1;
    else next_sample += 0x7fff;
    // * 2 and not << 1: the value is signed and negative below 0x4000,
    // and shifting a negative left is undefined. Same instruction.
    this_sample = (this_sample - 0x4000) * 2;
  )
}

// Shape: low flat + up-ramp + high flat + fall.  Timbre increases width of
// flats + slope of up-ramp
//
// ⟋|⟋| -> _/‾|_/‾| -> _|‾|_|‾|
void Oscillator::RenderSawPulseMorph(int16_t* input_samples, int16_t* audio_mix) {
  RENDER_PERIODIC(
    // Prevent saw from reaching an infinitely steep rise, else we'd have to
    // clumsily transition into a BLEP of what is now a rising pulse edge
    timbre = TimbreAtOrAboveZero(timbre);
    timbre = timbre + (timbre >> 1) + (timbre >> 2) + (timbre >> 3) + (timbre >> 4); // 31/32

    // Exponential timbre curve, biased high
    uint32_t pw = Interpolate88(lut_env_expo_u16, timbre) << 15; // 0-50% width of each flat part
    // The ramp may not rise in less than a sample. Below that it is a step, and
    // the BLEP below corrects the falling edge only -- at MIDI 97 the top of
    // the knob rose in 0.14 samples and the shape aliased at -4.1 dB against
    // -25 dB over the rest of its range.
    const uint32_t widest_flat = (UINT32_MAX - phase_increment) >> 1;
    if (pw > widest_flat) pw = widest_flat;
    // The slope divides by the width's high half, so the region must end where
    // that half does: the ramp's last 65536 phase units would otherwise divide
    // to more than full scale. The remainder joins the flat that follows.
    uint32_t saw_width = (UINT32_MAX - (pw << 1)) & 0xffff0000; // 0-100% width of up-ramp

    bool self_reset = PhaseWrapped(phase, phase_increment);
    // One edge, the fall at the wrap: the ramp's two corners are slope breaks,
    // which need no step correction. EDGES_PULSE was inert here -- its rising
    // arm broke on `phase < pw` at every wrap, and where pw is zero its two
    // corrections cancelled -- so this shape was not band-limited at all.
    while (true) { EDGES_SAW(phase, phase_increment) }
    if (phase < pw) next_sample += 0;
    else if (phase < pw + saw_width) next_sample += ((phase - pw) / (saw_width >> 16)) >> 1;
    else next_sample += 0x7fff;
    // * 2 and not << 1: the value is signed and negative below 0x4000,
    // and shifting a negative left is undefined. Same instruction.
    this_sample = (this_sample - 0x4000) * 2;
  )
}

void Oscillator::RenderSyncSine(int16_t* input_samples, int16_t* audio_mix) {
  RENDER_MODULATED(
    SYNC(
      sine(0) - sine(modulator_phase_at_reset),
      break, // No edges
      false // No extra transition
    );
    (void) transition_during_reset; (void) sync_reset; (void) self_reset;
    // Accumulate, do not assign: this_sample arrives holding the BLEP residual
    // SYNC wrote a line ago. Only a shape with no discontinuity may assign.
    next_sample += sine(modulator_phase);
  )
}

void Oscillator::RenderSyncPulse(int16_t* input_samples, int16_t* audio_mix) {
  uint32_t pw = 0x80000000;
  RENDER_MODULATED(
    SYNC(
      0 - (modulator_phase_at_reset < pw ? 0 : 32767),
      EDGES_PULSE(modulator_phase, modulator_phase_increment),
      !high_ && modulator_phase_at_reset >= pw
    );
    next_sample += modulator_phase < pw ? 0 : 32767;
    // * 2 and not << 1: the value is signed and negative below 16384,
    // and shifting a negative left is undefined. Same instruction.
    this_sample = (this_sample - 16384) * 2;
  )
}

void Oscillator::RenderSyncTriangle(int16_t* input_samples, int16_t* audio_mix) {
  RENDER_MODULATED(
    SYNC(
      triangle(0) - triangle(modulator_phase_at_reset),
      break, // No edges
      false // No extra transition
    );
    (void) transition_during_reset; (void) sync_reset; (void) self_reset;
    next_sample += triangle(modulator_phase);
  )
}

void Oscillator::RenderSyncSaw(int16_t* input_samples, int16_t* audio_mix) {
  RENDER_MODULATED(
    SYNC(
      0 - (modulator_phase_at_reset >> 17),
      EDGES_SAW(modulator_phase, modulator_phase_increment),
      false // No extra transition
    );
    next_sample += modulator_phase >> 17;
    // * 2 and not << 1: the value is signed and negative below 16384,
    // and shifting a negative left is undefined. Same instruction.
    this_sample = (this_sample - 16384) * 2;
  )
}

// void Oscillator::RenderFoldTriangle(int16_t* input_samples, int16_t* audio_mix) {
//   RENDER_PERIODIC(
//     this_sample = triangle(phase);
//     this_sample = this_sample * timbre >> 15;
//     this_sample = Interpolate88(ws_tri_fold, this_sample + 32768);
//   )
// }

// void Oscillator::RenderFoldSine(int16_t* input_samples, int16_t* audio_mix) {
//   RENDER_PERIODIC(
//     this_sample = sine(phase);
//     this_sample = this_sample * timbre >> 15;
//     this_sample = Interpolate88(ws_sine_fold, this_sample + 32768);
//   )
// }

// 2^f for f in [0, 1), u0.32, as 2^30 * (1 .. 2): a degree-4 fit by Horner,
// one UMULL a degree, within 3.6e-6 of 2^f -- 0.12 of an output LSB in ST's
// tanh.
static inline uint32_t Exp2Fraction_q30(uint32_t fraction_u32) {
  uint32_t value = 14693065;
  value = 55531496 + MulHighU(value, fraction_u32);
  value = 259438920 + MulHighU(value, fraction_u32);
  value = 744070390 + MulHighU(value, fraction_u32);
  return 1073745686 + MulHighU(value, fraction_u32);
}

// tanh(32 x), computed: the table it replaces held 257 points across a curve
// that is flat beyond |x| = 0.19, so the whole knee fell on ~26 of them and the
// interpolation between added up to 26 dB of aliasing over the curve itself at
// low TIMBRE. x is formed in Q30 from the sine and the TIMBRE's gain -- 1/64 to
// all of it -- with nothing dropped before the curve; with e = 2^(64 x log2 e)
// = m 2^n, tanh = (m - 2^-n) / (m + 2^-n), one divide. At 32 |x| >= 6 it rounds
// to full scale and is taken as that.
void Oscillator::RenderTanhSine(int16_t* input_samples, int16_t* audio_mix) {
  const uint32_t kSaturation_q30 = 201326592;  // 32 x = 6
  const uint32_t kLog2E_u1_31 = 3098164009u;
  RENDER_PERIODIC(
    const int32_t drive_q15 = 512 + ((32256 * timbre) >> 15);
    const int32_t x_q30 = sine(phase) * drive_q15;
    const uint32_t magnitude_q30 = static_cast<uint32_t>(x_q30 < 0 ? -x_q30 : x_q30);
    int32_t shaped = 32766;
    if (magnitude_q30 < kSaturation_q30) {
      // 64 x log2 e in q24: the magnitude is 2^30 x.
      const uint32_t exponent_q24 = MulHighU(magnitude_q30 << 1, kLog2E_u1_31);
      const uint32_t whole = exponent_q24 >> 24;
      const uint32_t mantissa_q30 = Exp2Fraction_q30(exponent_q24 << 8);
      const uint32_t reciprocal_q30 = (1u << 30) >> whole;
      const uint32_t tanh_q16 = (mantissa_q30 - reciprocal_q30)
          / ((mantissa_q30 + reciprocal_q30 + (1u << 15)) >> 16);
      shaped = static_cast<int32_t>((tanh_q16 * 32766 + (1u << 15)) >> 16);
    }
    this_sample = x_q30 < 0 ? -shaped : shaped;
  )
}

// sin(exp(u)), u = 5 pi / 6 + pi x / 2, computed: exp(u) / 2 pi cycles is
// C0 2^(a x), a = pi / (2 ln 2), C0 = e^(5 pi / 6) / 2 pi, which runs 0.45 to
// 10.5 cycles across x -- its fraction is the phase of a sine. The table it
// replaces held ~8 points a cycle at the top, and the interpolation between
// added up to 29 dB of aliasing over the curve itself at full TIMBRE; the
// phase-derived dither it carried broke that error up into noise rather than
// lowering it, and goes with it. x comes off the sine at full width: the phase
// moves 16 cycles a unit of x, so the 16-bit sine's own rounding was the next
// limit. Scaled as the table was, its mean taken out, by the curve's true peak.
//
// C0 2^f by Horner, C0 folded into the fit, as cycles in q29 (it reaches 4.4):
// within 3.6e-6 -- under what the sine's own rounding already moves the phase.
static inline uint32_t SizzleCycles_q29(uint32_t fraction_u32) {
  uint32_t value = 16028129;
  value = 60577284 + MulHighU(value, fraction_u32);
  value = 283012461 + MulHighU(value, fraction_u32);
  value = 811679267 + MulHighU(value, fraction_u32);
  return 1171310032 + MulHighU(value, fraction_u32);
}

void Oscillator::RenderExponentialSine(int16_t* input_samples, int16_t* audio_mix) {
  // a in q14, carried by the TIMBRE gain: the exponent is then one SMULL off
  // the sine. 15 bits of a reshape the TIMBRE map by 0.003%.
  const int32_t kExponentGain_q14 = 37129;
  const int32_t kMean_q15 = -2602;
  const int32_t kScale_q15 = 30356;
  RENDER_PERIODIC(
    timbre = (timbre >> 1) + (timbre >> 2) + (timbre >> 3) + 0x0fff; // Use top 7/8
    // a x in q28: sin * 2^31 times gain * a * 2^29, high word.
    const int32_t exponent_q28 = MulHighS(sine_q31(phase), timbre * kExponentGain_q14);
    const int32_t whole = exponent_q28 >> 28;  // -3 .. 2
    const uint32_t cycles_q29 =
        SizzleCycles_q29(static_cast<uint32_t>(exponent_q28) << 4);
    const uint32_t sizzle_phase = cycles_q29 << (3 + whole);
    this_sample = (sine(sizzle_phase) - kMean_q15) * kScale_q15 >> 15;
  )
}




// Transfer waveshaping: input sample is amplified and used as phase for a
// transfer function (sine or triangle). The transfer function's output
// becomes the final sample.
//
// Key property: all transfer functions satisfy f(0) = 0 (zero-crossing at
// phase 0, peak at phase 1/4). This ensures input 0 produces output 0.
//
// At min timbre, the input peak maps to the transfer peak (phase 1/4),
// placing the signal at the fold threshold: maximum amplitude before
// wavefolding begins. Output equals input (clean pass-through).

inline uint32_t amplify_for_transfer(
    int16_t sample, int16_t dynamic_gain_u15, uint32_t bias) {
  // min_gain derivation (at min timbre, input peak maps to transfer peak):
  //   input_peak * min_gain * 2^kTransferMaxGainBits + bias = phase_peak
  //   2^15 * min_gain * 2^4 = 2^30 - bias
  //   min_gain = (2^30 - bias) >> 19
  int32_t min_gain =
      (kTransferPeakPhase - bias) >> (kSamplePeakBits + kTransferMaxGainBits);

  int32_t gain = min_gain + (dynamic_gain_u15 << 1) - (dynamic_gain_u15 >> (kTransferMaxGainBits - 1));

  // Signed multiply, then reinterpret as phase (wrapping is intentional)
  uint32_t amped_sample = ((uint32_t)(sample * gain)) << kTransferMaxGainBits;

  return amped_sample + bias;
}

void Oscillator::RenderTransfer(int16_t* input_samples, int16_t* audio_mix) {
  const uint8_t carrier_index = transfer_carrier_;
  const uint8_t transfer_index = transfer_function_;
  uint32_t bias = transfer_bias_;
  uint8_t gain_shift = transfer_gain_shift_;
  // The shape is fixed for the whole block, so the choice is made here and the
  // loop carries no dispatch. It used to switch twice per sample on indices set
  // before the loop, which fragmented the body into basic blocks joined by
  // taken branches.
  //
  // Sine and expo are the same code with a different quadrant table, so that
  // choice is a pointer. Triangle is separate code, so the specialisation is
  // 2x2: one loop per (carrier is triangle?, transfer is triangle?).
  // Both indices come from `% 3` and `/ 6`, so neither can leave [0, 2].
  const uint16_t* carrier_table =
      carrier_index == TRANSFER_CURVE_SINE ? lut_sine_quadrant_u16 : lut_expo_quadrant_u16;
  const uint16_t* transfer_table =
      transfer_index == TRANSFER_CURVE_SINE ? lut_sine_quadrant_u16 : lut_expo_quadrant_u16;

#define TRANSFER_LOOP(CARRIER, TRANSFER) \
  RENDER_PERIODIC( \
    this_sample = CARRIER; \
    uint32_t transfer_phase = \
        amplify_for_transfer(this_sample, timbre >> gain_shift, bias); \
    this_sample = TRANSFER; \
  )

  if (carrier_index == TRANSFER_CURVE_TRI) {
    if (transfer_index == TRANSFER_CURVE_TRI) {
      TRANSFER_LOOP(triangle(phase), triangle(transfer_phase))
    } else {
      TRANSFER_LOOP(triangle(phase),
                    quadrant_lookup(transfer_table, transfer_phase))
    }
  } else {
    if (transfer_index == TRANSFER_CURVE_TRI) {
      TRANSFER_LOOP(quadrant_lookup(carrier_table, phase),
                    triangle(transfer_phase))
    } else {
      TRANSFER_LOOP(quadrant_lookup(carrier_table, phase),
                    quadrant_lookup(transfer_table, transfer_phase))
    }
  }
#undef TRANSFER_LOOP
}

void Oscillator::RenderFM(int16_t* input_samples, int16_t* audio_mix) {
  uint8_t fm_shape = shape_ - OSC_SHAPE_FM;
  int16_t interval = lut_fm_modulator_intervals[fm_shape];
  uint32_t modulator_phase_increment = ComputePhaseIncrement(pitch_ + interval);

  // Compensate for higher FM ratios having sweet spot at lower index
  uint8_t index_2x_upshift = lut_fm_index_2x_upshifts[fm_shape];
  uint8_t index_shift = index_2x_upshift >> 1;
  bool index_shift_halfbit = index_2x_upshift & 1;
  RENDER_MODULATED(
    modulator_phase += modulator_phase_increment;
    int16_t modulator = sine(modulator_phase);
    uint32_t phase_mod = modulator * timbre;
    phase_mod =
      (phase_mod << index_shift) +
      // Conditional multiplication by 1.5 to approximate sqrt(2)
      (index_shift_halfbit ? (phase_mod << (index_shift - 1)) : 0);
    this_sample = sine(phase + phase_mod);
  )
}

void Oscillator::RenderPhaseDistortionPulse(int16_t* input_samples, int16_t* audio_mix) {
  // Forces GCC to keep the table's address in a register, which it otherwise
  // reloads every sample. Worth two cycles a sample here.
  const uint16_t* sine_table = lut_sine_quadrant_u16;
  asm volatile ("" : "+r"(sine_table));
  SET_CZ_RATIO_LIMITS
  uint8_t filter_type = shape_ - OSC_SHAPE_CZ_PULSE_LP;
  const bool output_is_pulse = filter_type & 2;
  // The step belongs to the pulse. The integrator turns a step in its input into
  // a change of slope, so the low-pass output steps by none of it and the
  // peaking output by half -- and half is zero here, since peaking resets at a
  // half turn, where the sine is zero. High-pass resets there too.
  const int32_t wrap_step = output_is_pulse
      ? WrapStep(sine(kPhaseResetPulse[filter_type]), true) : 0;
  // The accumulator's 1/f gain lifts whatever sits nearest DC, and what sits
  // there is a harmonic past Nyquist folded back -- so the corner follows the
  // note, which attenuates hardest exactly where the lift is greatest. `clz` is
  // log2 of the increment, landing the corner between f0/3.3 and f0/4.7; the
  // spread is the shift moving a whole octave at a time.
  //
  // A min, not an assignment: a larger shift is a lower corner, and `clz`
  // exceeds kPdLeakyIntegratorShift below MIDI 48, which would put the corner
  // under the 28 Hz that constant sets.
  //   - it cannot reach zero, where the accumulator would keep none of itself
  //     and stop integrating: the increment cannot reach 2^31, so `clz` is at
  //     least 1.
  const int leaky_integrator_shift =
      std::min(__builtin_clz(phase_increment_), kPdLeakyIntegratorShift);
  int32_t integrator = pd_square_.integrator;
  RENDER_MODULATED(
    SET_MODULATOR_PHASE_INCREMENT_FROM_TIMBRE;
    modulator_phase += modulator_phase_increment;
    if ((phase << 1) < (phase_increment << 1)) {
      pd_square_.polarity = !pd_square_.polarity;
      modulator_phase = kPhaseResetPulse[filter_type];
      // The window resets twice a period, so the edge is timed against the
      // doubled phase the test above uses. The polarity has just flipped, and
      // it decides which way this reset jumps.
      const uint32_t t = EdgeTime(phase << 1, phase_increment << 1);
      const int32_t step = pd_square_.polarity ? -wrap_step : wrap_step;
      this_sample += ThisBlepSample(t) * step >> 15;
      next_sample += NextBlepSample(t) * step >> 15;
    }
    int16_t carrier = quadrant_lookup(sine_table, modulator_phase);
    uint16_t window = ~(phase >> 15); // Double saw
    int16_t pulse = (carrier * window) >> 16;
    if (pd_square_.polarity) pulse = -pulse;
    uint16_t integrator_gain = modulator_phase_increment >> 16; // Orig 14
    // An ideal integrator has infinite gain at DC, and its input is not quite
    // zero-mean: the two half-periods hold different numbers of samples, so the
    // polarity flip cannot cancel them exactly, and an unbounded integrator
    // accumulates the difference without limit. The leak caps the DC gain at
    // 2^kPdLeakyIntegratorShift -- a 28 Hz corner, below every note but the
    // lowest few.
    //
    // Rounded, not truncated: an arithmetic shift is a floor, which biases a
    // zero-mean signal by exactly half a count every sample.
    integrator -= integrator >> leaky_integrator_shift;
    integrator += (pulse * integrator_gain + (1 << 13)) >> 14; // Orig 16
    CLIP(integrator)
    int16_t output;
    if (output_is_pulse) {
      output = pulse;
    } else {
      // TODO HP is 2dB above LP, which is 2dB above PK
      output = integrator;
      if (filter_type == 1) { // Peaking
        output = (pulse + integrator) >> 1;
      }
    }
    // A sample ahead, like the saw below and every other BLEP shape here: the
    // correction is split across this_sample and next_sample and only lands on
    // the edge if the waveform is delayed to match.
    next_sample += output;
  )
  pd_square_.integrator = integrator;
}

void Oscillator::RenderPhaseDistortionSaw(int16_t* input_samples, int16_t* audio_mix) {
  SET_CZ_RATIO_LIMITS
  uint8_t filter_type = shape_ - OSC_SHAPE_CZ_SAW_LP;
  // Band- and high-pass take the signed arm; the other two carry a DC pedestal.
  // Which arm it is decides both the output and the size of the wrap's step.
  const bool signed_arm = filter_type & 2;
  const int32_t wrap_step =
      WrapStep(sine(kPhaseResetSaw[filter_type]), signed_arm);
  RENDER_MODULATED(
    SET_MODULATOR_PHASE_INCREMENT_FROM_TIMBRE;
    modulator_phase += modulator_phase_increment;
    if (PhaseWrapped(phase, phase_increment)) {
      modulator_phase = kPhaseResetSaw[filter_type];
      // Added, not subtracted: this ramp falls and the wrap steps UP.
      const uint32_t t = EdgeTime(phase, phase_increment);
      this_sample += ThisBlepSample(t) * wrap_step >> 15;
      next_sample += NextBlepSample(t) * wrap_step >> 15;
    }
    int16_t carrier = sine(modulator_phase);
    uint16_t window = ~(phase >> 16); // Saw
    int16_t output;
    if (signed_arm) {
      output = (window * carrier) >> 16;
    } else {
      // Unsigned: the product needs all 32 bits, and signed overflow is
      // undefined. carrier is biased into range and the bias taken off after.
      output = (static_cast<uint32_t>(window) * (carrier + 32768) >> 16) - 32768;
    }
    // Written a sample ahead, and emitted next time round. The correction above
    // is split across this_sample and next_sample, so it only lands on the edge
    // if the waveform is delayed the same way every other BLEP shape here
    // delays it. Assigning this_sample instead put the two halves a sample away
    // from the step and made the aliasing worse, not better.
    next_sample += output;
  )
}

// Below this the cutoff coefficient stops tracking and the resonance is the
// only pitch the shape has: at MIDI 24 the peak sits at 43.9 Hz for a note of
// 32.7.
// The soft limiter's curve reaches this many times the scale, so an excursion of
// up to that much is compressed instead of clipped. The curve is tanh(k*x)
// with k equal to this, which is what makes its gain 1 for small signals, so
// changing one means changing the other. yarns/resources/waveshapers.py
// generates the table.
static const int32_t kSoftLimitHeadroom = 4;
// GCC re-materialises the table's address where it is read -- once per entry,
// so twice a sample inside SoftLimit. The blackbox pins it to a register
// instead, and SoftLimit takes it as a parameter so this can be the caller's.
//
// Where to call it is measured, not free. Above the loop it holds a register
// across everything else in there: WHISTLE is 68 cycles a sample hoisted and 74
// at the call site, and PING is 64 at the call site and 74 hoisted.
static inline const int16_t* TableAsRegister(const int16_t* table) {
  asm volatile ("" : "+r"(table));
  return table;
}
static inline const int16_t* SoftLimitTableAsRegister() {
  return TableAsRegister(ws_soft_limit);
}

// A resonator's output: the curve input through the soft limiter, held to the
// table's domain because the drive takes it past the curve's end.
#ifdef TEST
}  // namespace yarns
#include <cmath>
namespace yarns {
// RESONATOR_SHAPER picks the transfer instead, in units where 8192 is the
// curve's unity: 1 sine fold, 2 triangle fold, 3/4 the same biased by
// RESONATOR_BIAS_Q8 (256 = one unity) -- a bias's DC is taken back out.
static int g_shaper = getenv("RESONATOR_SHAPER") ? atoi(getenv("RESONATOR_SHAPER")) : 0;
static double g_shaper_bias =
    getenv("RESONATOR_BIAS_Q8") ? atoi(getenv("RESONATOR_BIAS_Q8")) / 256.0 : 0.5;
static inline double Triangle(double u) {  // slope 1 through 0, peak 1 at u = 1
  double p = fmod(u + 1, 4);
  if (p < 0) p += 4;
  return p < 2 ? p - 1 : 3 - p;
}
// 5: RenderTransfer's expo quadrant, 1 - (1 - x)^3 mirrored to a period,
// its input scaled for a small-signal slope of 1 like the others: it rises to
// 1 at u = 3 and folds back.
static inline double ExpoFold(double u) {
  double p = fmod(u / 12.0, 1.0);
  if (p < 0) p += 1;
  const double q = p * 4;
  const int quadrant = static_cast<int>(q);
  const double x = q - quadrant;
  const double rise = 1 - (1 - x) * (1 - x) * (1 - x);
  const double fall = 1 - x * x * x;
  return quadrant == 0 ? rise : quadrant == 1 ? fall
      : quadrant == 2 ? -rise : -fall;
}
// PRE_SHAPER shapes the excitation before the resonator: 1 tanh, 2 sine fold,
// 3 expo fold, at PRE_DRIVE_Q8/256 unities per 8192 counts, normalised back
// to a small-signal gain of 1.
static int g_pre_shaper = getenv("PRE_SHAPER") ? atoi(getenv("PRE_SHAPER")) : 0;
static double g_pre_drive =
    getenv("PRE_DRIVE_Q8") ? atoi(getenv("PRE_DRIVE_Q8")) / 256.0 : 1;
static inline int32_t PreShape(int32_t excitation_q15_14) {
  if (!g_pre_shaper) return excitation_q15_14;
  const double unit = 8192.0 * (1 << 14) / g_pre_drive;
  const double u = excitation_q15_14 / unit;
  const double y = g_pre_shaper == 1 ? tanh(u)
      : g_pre_shaper == 2 ? sin(u) : ExpoFold(u);
  return static_cast<int32_t>(y * unit);
}
// IN_LOOP_SAT=k100 saturates the resonator's bp every sample, inside the loop,
// at k100/100 curve unities: bp = K tanh(bp / K). The drive into the curve
// names K in state units.
static double g_in_loop_sat = getenv("IN_LOOP_SAT") ? atoi(getenv("IN_LOOP_SAT")) / 100.0 : 0;
static inline void InLoopSaturate(int32_t* bp_q15_14, int32_t drive_q32) {
  if (!g_in_loop_sat) return;
  const double k = g_in_loop_sat * 8192.0 * 4294967296.0 / drive_q32;
  *bp_q15_14 = static_cast<int32_t>(k * tanh(*bp_q15_14 / k));
}
#else
static inline void InLoopSaturate(int32_t*, int32_t) { }
static inline int32_t PreShape(int32_t excitation_q15_14) { return excitation_q15_14; }
#endif
static inline int32_t SoftLimit(
    const int16_t* curve, int32_t state_in_curve, int32_t scale_u15);
static inline int32_t ResonatorShape(
    const int16_t* curve, int32_t curve_input, int32_t scale_u15) {
#ifdef TEST
  if (g_shaper) {
    const double u = curve_input / 8192.0;
    const double b = g_shaper >= 3 ? g_shaper_bias : 0;
    // 6: linear, held to the ceiling -- no shaping, for a source that makes
    // its own harmonics.
    // 7: expo fold biased by RESONATOR_BIAS_Q8, like 3 for sine.
    const double y = g_shaper == 6 ? (u > 1 ? 1 : u < -1 ? -1 : u)
        : g_shaper == 7 ? ExpoFold(u + g_shaper_bias) - ExpoFold(g_shaper_bias)
        : g_shaper == 5 ? ExpoFold(u)
        : (g_shaper == 1 || g_shaper == 3)
        ? sin(u + b) - sin(b) : Triangle(u + b) - Triangle(b);
    // A bias moves one swing past full scale: 1 + |f(b)|. Scaled back so the
    // larger swing reaches the voice's ceiling and no further.
    const double bias_swing = g_shaper == 3 ? 1 + fabs(sin(b))
        : g_shaper == 7 ? 1 + fabs(ExpoFold(g_shaper_bias)) : 1;
    return static_cast<int32_t>(y / bias_swing * scale_u15);
  }
#endif
  return SoftLimit(curve, stmlib::Clip16(curve_input), scale_u15);
}

// The curve's domain: the state scaled so the loudest one the shape can make
// lands at the top of the table. In whatever Q state_to_output is in -- the
// product leaves int32 for WHISTLE's, so it is taken 64 bits wide.
static int32_t StateIntoCurve(int32_t state_to_output, int32_t scale) {
  return static_cast<int32_t>(DivU64ByU32(
      stmlib::MulU32(static_cast<uint32_t>(state_to_output), INT16_MAX),
      static_cast<uint32_t>(state_to_output) * INT16_MAX,
      static_cast<uint32_t>(scale) * kSoftLimitHeadroom));
}

static inline int32_t SoftLimit(
    const int16_t* curve, int32_t state_in_curve, int32_t scale_u15) {
  const uint16_t index = static_cast<uint16_t>(state_in_curve + 32768);
  // Interpolate88's body, off ONE pointer and without its narrowing return.
  // The result lies between two entries of a table that fits int16, so the
  // narrowing cannot bite, and it costs a round trip through the stack in the
  // sample loop.
  const int16_t* entry = &curve[index >> 8];
  const int32_t below = entry[0];
  const int32_t above = entry[1];
  return (below + ((above - below) * (index & 0xff) >> 8)) * scale_u15 >> 15;
}

// Where the pitch term below reads zero octaves, and so the level it hands
// back is the whole of level_into_knee_u15. It only ever ATTENUATES -- the
// octaves are floored at zero and spent as a right shift -- so notes under it
// are handed the same level as it, and it sits low enough that few are.
//
// How low is bounded by the knee, not by taste: moving it down an octave costs
// the knee half an octave of level to keep every note above it unchanged, and
// the knee is u15. From 18800 that reaches MIDI 10.8, so MIDI 12 is the floor
// of what the mechanism can express.
static const int32_t kWhistleTiltReferencePitch = 12 << 7;
// The resonator's gain at resonance rises as 1/sqrt(damp), and rises again
// with pitch, by 2.85 dB an octave.
//
// The damp term is corrected at the input, by scaling the excitation, which
// moves the filter state and leaves the level alone. The pitch term is
// corrected here at the output, which moves the level and leaves the state
// alone -- so the state still rails above MIDI 84, where that term is
// largest.
// The curve is driven 2^this past the level WhistleStateToOutput names. Zero:
// WHISTLE stays clean, the curve only catching peaks, and the loop shapes carry
// the saturated sounds. Each bit spent here would saturate the tone further --
// steadier amplitude, brighter, a shorter rise -- in the shift that brings the
// product back to the curve's scale, as fractional bits the product does not
// keep.
static const int32_t kWhistleCurveDriveBits = 0;

static int32_t WhistleStateToOutput(
    int32_t pitch, int32_t scale_u15,
    int32_t damp_drive_u15) {
  // Holds rms flat to MIDI 84.
  const int32_t pitch_correction_numerator = 1;
  const int32_t pitch_correction_denominator = 2;
  // How far into the curve's knee the level law puts the signal: noise
  // visits its peak rarely, and everything under it is unspent until something
  // bends the peak.
  const int32_t level_into_knee_u15 = 31618;
  int32_t octaves_q16 =
      (pitch - kWhistleTiltReferencePitch) * 65536 / (12 * 128);
  octaves_q16 = octaves_q16 * pitch_correction_numerator
      / pitch_correction_denominator;
  if (octaves_q16 < 0) octaves_q16 = 0;
  int32_t level_u15 = level_into_knee_u15 *
      (Interpolate88(lut_expo2_neg_u16, octaves_q16 & 0xffff) >> 1) >> 15;
  int32_t whole_octaves = octaves_q16 >> 16;
  const int32_t level_at_pitch_u15 =
      whole_octaves >= 20
          ? 0
          : ((level_u15 >> whole_octaves) * scale_u15 >> 15);
  return damp_drive_u15
      ? static_cast<int32_t>(
            (static_cast<uint32_t>(level_at_pitch_u15) << 15) / damp_drive_u15)
      : level_at_pitch_u15;
}

#ifdef TEST
}  // namespace yarns
#include <cmath>
namespace yarns {
// JET_M=m100 turns WHISTLE into a blown resonator: the excitation adds
// d * K * tanh(m * bp / K), m = gain * m100 / 100, so the loop cancels the
// damping at m = 1 at every Q and saturates at K. JET_TARGET is the overblown
// amplitude in curve units (32768 = the curve's end); JET_NOISE_Q8 scales the
// turbulence noise (256 = as built).
static double g_jet_m = getenv("JET_M") ? atoi(getenv("JET_M")) / 100.0 : 0;
static double g_jet_target =
    getenv("JET_TARGET") ? atoi(getenv("JET_TARGET")) : 32768;
static int g_jet_noise_q8 =
    getenv("JET_NOISE_Q8") ? atoi(getenv("JET_NOISE_Q8")) : 256;
static double g_jet_k = 0;
// JET_K_POW=p100: the saturation level follows the blowing pressure as
// K * gain^(p100 / 100) -- a jet's flow rises with pressure. 0 = fixed K.
static double g_jet_k_pow =
    getenv("JET_K_POW") ? atoi(getenv("JET_K_POW")) / 100.0 : 0;
static inline int32_t JetFeedback(int32_t bp_q15_14, int16_t gain, int16_t damp) {
  if (!g_jet_m) return 0;
  const double blow = gain / 32767.0;
  const double m = blow * g_jet_m;
  const double k = g_jet_k * pow(blow > 1e-6 ? blow : 1e-6, g_jet_k_pow);
  return static_cast<int32_t>(damp / 16384.0 * k * tanh(m * bp_q15_14 / k));
}
static inline int32_t JetNoise(int32_t excitation) {
  return g_jet_m ? excitation * g_jet_noise_q8 >> 8 : excitation;
}
// MULTI=n rings n resonators at harmonics of the note (MULTI_ODD=1: odd ones
// only), floating point, each with the timbre's damping; the jet reads their
// SUM, the bore's pressure, and the output is that sum. JET_* as above.
static int g_multi = getenv("MULTI") ? atoi(getenv("MULTI")) : 0;
static int g_multi_odd = getenv("MULTI_ODD") ? 1 : 0;
// JET_LINEAR=1 drops the jet's tanh: d * m * sum, m held below 1 so the loop
// only regenerates the breath and never self-oscillates.
static int g_jet_linear = getenv("JET_LINEAR") ? 1 : 0;
// MULTI_DAMP_POW=p100: mode h damps at d * h^(p100/100), as a bore's upper
// modes are lossier; mode h then self-oscillates only past m = h^p.
static double g_multi_damp_pow =
    getenv("MULTI_DAMP_POW") ? atoi(getenv("MULTI_DAMP_POW")) / 100.0 : 0;
// BOW=1 swaps the jet for bow friction: F = P * d * A0 * phi((vb - v) / v0),
// phi(x) = x exp(1/2 - x^2/2), which peaks at 1 when the slip is v0 and falls
// past it -- the falling side is what pumps. v is the modes' summed velocity
// (bp), A0 the JET_TARGET amplitude in state units, v0 = A0 / 2, vb = blow *
// BOW_XMAX100/100 * v0, P = BOW_PRESSURE100/100. BOW_OUT=lp takes the output
// from lp (displacement) rather than bp (velocity).
static int g_bow = getenv("BOW") ? 1 : 0;
static double g_bow_xmax =
    getenv("BOW_XMAX100") ? atoi(getenv("BOW_XMAX100")) / 100.0 : 3;
static double g_bow_pressure =
    getenv("BOW_PRESSURE100") ? atoi(getenv("BOW_PRESSURE100")) / 100.0 : 1;
static int g_bow_out_lp = getenv("BOW_OUT") ? !strcmp(getenv("BOW_OUT"), "lp") : 0;
// LOOP_MEAN_PROBE=1: RenderLoop prints its DC blocker's mean once a block.
static int g_loop_mean_probe = getenv("LOOP_MEAN_PROBE") ? 1 : 0;
// LOOP_DC_BEFORE_GAIN=1: the loop shapes take their DC out of the curve before
// the gain rather than out of the gained tap, under headroom 1 (oscillator.h).
int g_loop_dc_before_gain = getenv("LOOP_DC_BEFORE_GAIN") ? 1 : 0;
// BOW_D_REF=d_u1_14 couples the bow with that fixed damping in place of the
// timbre's d, so the loop gain rises with Q; BOW_D_PROBE=1 prints d per sample.
static double g_bow_d_ref =
    getenv("BOW_D_REF") ? atoi(getenv("BOW_D_REF")) / 16384.0 : 0;
static int g_bow_d_probe = getenv("BOW_D_PROBE") ? 1 : 0;
// EXCITER_TAP=1 outputs the exciter instead of the modes: the bow's friction
// or the jet's flow, divided by P * d (or d) so its level does not follow the
// coupling, through a 20 Hz DC blocker. The modes then only keep the time.
static int g_exciter_tap = getenv("EXCITER_TAP") ? 1 : 0;
struct MultiSlot { const void* owner; double bp[5], lp[5], dc_x, dc_y; };
static MultiSlot g_multi_slots[8];
static inline double MultiWhistle(const void* owner, int16_t pitch, int16_t gain,
    int16_t damp, int32_t noise_q15_14, int32_t drive_q32) {
  MultiSlot* slot = 0;
  for (int i = 0; i < 8 && !slot; ++i)
    if (g_multi_slots[i].owner == owner) slot = &g_multi_slots[i];
  for (int i = 0; i < 8 && !slot; ++i)
    if (!g_multi_slots[i].owner) { slot = &g_multi_slots[i]; slot->owner = owner; }
  const double f0 = 440.0 * pow(2.0, (pitch / 128.0 - 69) / 12);
  const double d = damp / 16384.0;
  if (g_bow_d_probe) fprintf(stderr, "d=%g\n", d);
  const double bow_d = g_bow_d_ref ? g_bow_d_ref : d;
  double sum = 0;
  for (int k = 0; k < g_multi; ++k) sum += slot->bp[k];
  const double blow = gain / 32767.0;
  const double kk = g_jet_k * pow(blow > 1e-6 ? blow : 1e-6, g_jet_k_pow);
  const double m_linear = blow * g_jet_m < 0.95 ? blow * g_jet_m : 0.95;
  const double v0 = g_jet_k * (4 / M_PI) / 2;
  const double slip = (blow * g_bow_xmax * v0 - sum) / v0;
  const double jet = g_bow
      ? g_bow_pressure * bow_d * g_jet_k * (4 / M_PI) * slip * exp(0.5 - 0.5 * slip * slip)
      : !g_jet_m ? 0
      : g_jet_linear ? d * m_linear * sum
      : d * kk * tanh(blow * g_jet_m * sum / kk);
  double out = 0;
  if (g_exciter_tap) {
    // The bow's term less its value at rest, so a gain step moves no DC.
    const double rest = blow * g_bow_xmax;
    const double exciter = g_bow
        ? g_jet_k * (4 / M_PI) * (slip * exp(0.5 - 0.5 * slip * slip)
              - rest * exp(0.5 - 0.5 * rest * rest))
        : kk * tanh(blow * g_jet_m * sum / kk);
    // One-pole DC blocker, 20 Hz at 45 kHz.
    const double y = exciter - slot->dc_x + 0.99721 * slot->dc_y;
    slot->dc_x = exciter;
    slot->dc_y = y;
    out = y;
  }
  for (int k = 0; k < g_multi; ++k) {
    const double h = g_multi_odd ? 2 * k + 1 : k + 1;
    if (h * f0 > 45000 / 6) continue;
    const double c = 2 * sin(M_PI * h * f0 / 45000);
    const double dk = d * pow(h, g_multi_damp_pow);
    const double notch = noise_q15_14 + jet - (dk < 2 ? dk : 2) * slot->bp[k];
    slot->lp[k] += c * slot->bp[k];
    slot->bp[k] += c * (notch - slot->lp[k]);
    if (!g_exciter_tap) out += g_bow_out_lp ? slot->lp[k] : slot->bp[k];
  }
  return out * static_cast<double>(drive_q32) / 4294967296.0;
}
#else
static const int g_multi = 0;
static inline double MultiWhistle(const void*, int16_t, int16_t, int16_t,
    int32_t, int32_t) { return 0; }
static inline int32_t JetFeedback(int32_t, int16_t, int16_t) { return 0; }
static inline int32_t JetNoise(int32_t excitation) { return excitation; }
#endif

// The loop shapes: a band-pass at the note fed back through a curve, biased by
// TIMBRE. One loop unit is 2^kLoopUnitBits state counts and also the curve
// table's argument unit: the tables run over 4 units each side of zero.
static const int32_t kLoopUnitBits = 13;
static const int32_t kLoopUnitShift_q15_14 =
    kLoopUnitBits + ResonatorState::kFractionalBits;
// Q 25.
static const int16_t kLoopDamp_u1_14 = 655;
// TIMBRE halved: the span the offset and the loop gain are both read across.
static const int32_t kLoopOffsetBits = kEnvelopeSampleBits - 1;
// Input noise of 2^-this loop units at full gain.
static const int32_t kLoopNoiseBits = 7;
// The direct term's unit, which the noise rides on in the same product: one
// loop unit at full gain.
static const int32_t kLoopDirectBits = kLoopUnitShift_q15_14 - 15;
static const int32_t kLoopDirect = 1 << kLoopDirectBits;
// A signed 32-bit draw shifted to 2^-kLoopNoiseBits of the direct unit.
static const int32_t kLoopNoiseShift = 31 - kLoopDirectBits + kLoopNoiseBits;
// What d's product drops off its operand first, so that any int32 times d
// fits 32 bits.
static const int32_t kLoopDampPreShift = 10;
STATIC_ASSERT((INT32_MAX >> kLoopDampPreShift) * kLoopDamp_u1_14 <= INT32_MAX,
              loop_damp_product_fits);
// The jittered damping's pre-shift: the state's and the jitter's terms
// together reach about 4 units before d, so each gives up two more bits than
// kLoopDampPreShift's.
static const int32_t kLoopJitterPreShift = 12;
// The pitch-scaled draw: the noise's top 16 bits times the scale, whose 11
// fraction bits leave the draw at the noise times scale, 2^-5.
static const int32_t kLoopRoughnessFractionBits = 11;
static const int32_t kLoopRoughnessShift = 16 - kLoopRoughnessFractionBits;
// The multiplier's ends are held to at least 2^-this of the block's larger gain.
static const int32_t kLoopRampGuardBits = 2;
// The DC blocker's running mean forgets 2^-this a sample: 28 Hz at 45 kHz.
static const int32_t kLoopDcBlockerShift = 8;

// The curve at x - 32768, x held to the table's span: the bias lets one USAT
// do the clip.
static inline int32_t LoopCurve(const int16_t* curve, int32_t biased_x) {
  const uint32_t index = stmlib::ClipU16(biased_x);
  const int16_t* entry = &curve[index >> 8];
  return entry[0] + ((entry[1] - entry[0]) * static_cast<int32_t>(index & 0xff) >> 8);
}

// bp to the curve's argument, loop gain / (gain * the curve's slope at the
// offset), as the multiplier whose product with a q15_14 state has the
// argument in its high word. Saturated where every argument it makes is past
// the table anyway.
static int32_t LoopArgumentMultiplier(
    int32_t gain, int32_t slope_q15, int32_t loop_gain_q15) {
  return ScaleRatio(loop_gain_q15, 1u << (32 - ResonatorState::kFractionalBits),
                    static_cast<uint32_t>(gain * slope_q15 >> 15));
}

// A 17-entry table across TIMBRE, at half_timbre, interpolated.
template <typename T>
static inline int32_t LoopTableAt(const T* table, int32_t half_timbre) {
  const int32_t index = half_timbre >> (kLoopOffsetBits - 4);
  const int32_t fraction = half_timbre & ((1 << (kLoopOffsetBits - 4)) - 1);
  return table[index] + ((table[index + 1] - table[index]) * fraction
      >> (kLoopOffsetBits - 4));
}

// What a loop shape is, to RenderLoop:
//   Curve()             the curve's table, pinned to a register
//   kOffsetTop_q15      the offset at full TIMBRE, as a share of 2 loop units
//   kLoopGain*_q15      the small-signal loop gain at the offset's two ends,
//                       which the curve's slope there is divided back out of
//   SlopeAtRest_q15()   the curve's slope at the offset, from the table and
//                       the curve's value there
//   PitchCorrection()   pitch units (1/128 semitone) the resonator is tuned
//                       up by at a TIMBRE, against the flat pull of the loop
//   Direct()            the direct term, at a TIMBRE: what gain strikes the
//                       filter with, as many loop units at full gain
//   kFollowsFallingGain when the gain falls, the state falls with it rather
//                       than the multiplier rising to meet it: the curve's
//                       input stays where the loop put it. A falling gain
//                       otherwise overdrives the curve until the state, which
//                       decays only at the resonator's own rate, catches up
//   kDampTimesLoopGain  0: d is kLoopDamp_u1_14. Otherwise Q follows the loop
//                       gain: d = min(kLoopDamp_u1_14, this / loop gain), this
//                       in u1.14 times q15, so d * loop gain -- which the
//                       loop's flat pull goes as -- holds once the gain is up
//   kRoughnessByPitch   the input noise and the jitter scaled by
//                       sqrt(f(A3) / f), -3 dB an octave about A3: both are
//                       white, so what reaches the note is the slice the
//                       resonance passes, which widens with the pitch
//   kJitterShift        white jitter on the whole feedback, its value at rest
//                       included: times 1 + jitter * noise, noise uniform in
//                       -1..1; jitter 2^(31 - this) / kLoopDamp_u1_14, or with
//                       kRoughnessByPitch 2^(16 - this) at A3; 0 for none
//   kCycleDetune        each cycle's length drawn anew: at the state's upward
//                       zero crossing the resonator retunes by noise * this
//                       / 2^31 of its cutoff, noise uniform in -1..1 and held
//                       until the next crossing; 0 for none
//
// WIND: tanh, offset 0..2, so the bottom stays a sine and the top squares off.
struct WindLoop {
  static const int16_t* Curve() { return SoftLimitTableAsRegister(); }
  static const int32_t kOffsetTop_q15 = 32768;
  static const int32_t kLoopGainBottom_q15 = 42598;  // 1.3
  static const int32_t kLoopGainTop_q15 = 81920;  // 2.5
  // sech^2, the slope of tanh, is 1 - tanh^2.
  static int32_t SlopeAtRest_q15(const int16_t*, int32_t, int32_t rest) {
    return 32768 - (rest * rest >> 15);
  }
  // Within 1.7 cents of the note at every TIMBRE, the same as at none.
  static int32_t PitchCorrection(int32_t) { return 0; }
  // tanh overdriven only saturates: a fast release brightens as it fades.
  static const bool kFollowsFallingGain = false;
  static int32_t Direct(int32_t) { return kLoopDirect; }
  static const int32_t kDampTimesLoopGain = 0;
  static const bool kRoughnessByPitch = false;
  static const int32_t kJitterShift = 0;
  static const int32_t kCycleDetune = 0;
};

// BOWED: fold_back, u e^(1/2 - u^2 / 2), offset 0..0.7 -- bright, even-rich,
// and pulled flat as it brightens, where tanh would square off. The jitter is
// bow pressure wavering, which roughens every partial into a scrape.
struct BowedLoop {
  static const int16_t* Curve() { return TableAsRegister(ws_fold_back); }
  static const int32_t kOffsetTop_q15 = 11469;  // 0.7 of 2
  static const int32_t kLoopGainBottom_q15 = 42598;  // 1.3
  // 40: the harmonics keep coming as TIMBRE rises; at 7 the top was a plateau.
  static const int32_t kLoopGainTop_q15 = 1310720;
  // The secant across one table step each side, 1/16 of a unit: the table's
  // own slope there, which no closed form of the curve's value gives.
  static int32_t SlopeAtRest_q15(const int16_t* curve, int32_t biased_offset,
                                 int32_t) {
    return 16 * (LoopCurve(curve, biased_offset + 256)
        - LoopCurve(curve, biased_offset - 256));
  }
  // The fold-back's flat pull goes as d * loop gain, which kDampTimesLoopGain
  // holds: measured held, A2..A5, -1.4 cents at TIMBRE 20, -1.3 at 64, -1.0
  // at 105 -- as flat as the pull at none, so nothing to correct. (At Q 25
  // throughout, it reached 10.5 cents at the top and took a table.)
  static int32_t PitchCorrection(int32_t) { return 0; }
  // The strike sized to the loop's settled level. The level a fast attack
  // strikes the filter to does not depend on the loop gain, but the level the
  // loop settles at falls as the loop gain rises, so a full strike overdrove
  // the fold-back -- its output a burst of folded harmonics, shifting as the
  // ring decayed, which read as the note's pitch swooping. Measured on this
  // map: the settled peak over the strike's, attack 0, A3 (pitch-independent
  // to 1%), as a share of a full strike, u0.15.
  static int32_t Direct(int32_t half_timbre) {
    static const uint16_t kTable_u15[17] = {
        32767, 28780, 20975, 16600, 13772, 11764, 10237, 9018, 8012,
        7153, 6403, 5741, 5151, 4610, 4109, 3647, 3211 };
    return kLoopDirect * LoopTableAt(kTable_u15, half_timbre) >> 15;
  }
  // The fold-back overdriven folds over: a pluck's decay, faster than the
  // resonator's, threw its pitch about by hundreds of cents.
  static const bool kFollowsFallingGain = true;
  // k 0.2, in u1.14 times q15: Q 25 up to a loop gain of 5, then d * L = 0.2.
  // That halves the flat start a strong loop has before it settles, and holds
  // the settled pull to a constant.
  static const int32_t kDampTimesLoopGain = 107374182;
  static const bool kRoughnessByPitch = true;
  // 2^-1: jitter 0.5 at A3.
  static const int32_t kJitterShift = 17;
  // The stick-slip cycle's own irregularity, which a bowed string's scrape
  // is: each period's length +-20 cents, 2^(20/1200) - 1 of 2^31. A per-sample
  // jitter roughens the tone but does not move the cycle; this moves only it.
  // Linear in the draw, so the flat end reaches -20.2 cents.
  // OFF: +-20 cents (24952558) costs 7 cycles a sample, 85 -> 92, which the
  // budget does not have. 0 compiles the draw out.
  static const int32_t kCycleDetune = 0;
};

void Oscillator::RenderWind(int16_t* input_samples, int16_t* audio_mix) {
  RenderLoop<WindLoop>(input_samples, audio_mix);
}

void Oscillator::RenderBowed(int16_t* input_samples, int16_t* audio_mix) {
  RenderLoop<BowedLoop>(input_samples, audio_mix);
}

template <typename Loop>
void Oscillator::RenderLoop(int16_t* input_samples, int16_t* audio_mix) {
  StateVariableFilter svf = svf_;
  ResonatorState state;
  state.Load(svf);
  const int16_t* curve = Loop::Curve();

  // The offset once a block, from the TIMBRE the block opens on. The
  // multiplier walks between its values at the block's two ends, because
  // gain cancels out of the loop only where the multiplier tracks it: a block's
  // worth of mismatch amplitude-modulates a rising note at the block rate.
  // Each end is held to at least 2^-kLoopRampGuardBits of the larger, so a
  // note that opens on a gain near nothing does not saturate the block.
  // Held at zero from below: the TIMBRE indexes BOWED's tables, and one below
  // zero read before them -- what lay there differed between host and target.
  // The warp keeps the firmware's above zero; this keeps any caller's there.
  const int32_t half_timbre = std::max(0, input_samples[0] >> 1);
  const int32_t note_cutoff_q0_31 = SVF::CutoffFromFreq_q0_31(
      pitch_ + Loop::PitchCorrection(half_timbre));
  // This cycle's length, carried across blocks: a cycle outlasts many.
  int32_t cutoff_q0_31 = Loop::kCycleDetune
      ? note_cutoff_q0_31
          + 2 * MulHighS(note_cutoff_q0_31, loop_cycle_detune_q31_)
      : note_cutoff_q0_31;
  const int32_t direct = Loop::Direct(half_timbre);
  const int32_t biased_offset =
      (half_timbre * Loop::kOffsetTop_q15 >> 15) + 32768;
  const int32_t rest = LoopCurve(curve, biased_offset);
  const int32_t slope_q15 = Loop::SlopeAtRest_q15(curve, biased_offset, rest);
  const int32_t loop_gain_q15 = Loop::kLoopGainBottom_q15 + static_cast<int32_t>(
      static_cast<int64_t>(Loop::kLoopGainTop_q15 - Loop::kLoopGainBottom_q15)
          * half_timbre >> kLoopOffsetBits);
  int32_t damp_u1_14 = kLoopDamp_u1_14;
  if (Loop::kDampTimesLoopGain) {
    damp_u1_14 = std::min(damp_u1_14, Loop::kDampTimesLoopGain / loop_gain_q15);
  }
  // sqrt(f(A3) / f) = 2^(-semitones from A3 / 24): it halves every two
  // octaves. A table across two octaves, interpolated, shifted by how many
  // two-octave spans lie between the note and A3 -- a few cycles a block, where
  // the ratio's divide and square root cost ~220 (measured under QEMU). With
  // kLoopRoughnessFractionBits fraction bits, held to 1/8..4.
  int32_t roughness = 0;
  if (Loop::kRoughnessByPitch) {
    // 2^(-i / 24) for i semitones, 2^15 the unit.
    static const uint16_t kHalvingAcrossTwoOctaves_u15[25] = {
        32768, 31835, 30929, 30048, 29193, 28362, 27554, 26770, 26008,
        25268, 24548, 23849, 23170, 22511, 21870, 21247, 20643, 20055,
        19484, 18929, 18390, 17867, 17358, 16864, 16384 };
    const int32_t kTwoOctaves = 24 << 7;
    // Enough two-octave spans below A3 that MIDI 0 counts up from zero.
    const int32_t kSpansBelowA3 = 4;
    const int32_t from = pitch_ - (57 << 7) + kSpansBelowA3 * kTwoOctaves;
    const int32_t spans = from / kTwoOctaves;
    const int32_t within = from - spans * kTwoOctaves;
    const uint16_t* entry = &kHalvingAcrossTwoOctaves_u15[within >> 7];
    const int32_t halving_u15 =
        entry[0] + ((entry[1] - entry[0]) * (within & 0x7f) >> 7);
    roughness = halving_u15
        >> (spans - kSpansBelowA3 + 15 - kLoopRoughnessFractionBits);
    CONSTRAIN(roughness, 1 << (kLoopRoughnessFractionBits - 3),
              1 << (kLoopRoughnessFractionBits + 2));
  }
  const int32_t first_gain = input_samples[kAudioBlockSize];
  const int32_t last_gain = input_samples[2 * kAudioBlockSize - 1];
  const int32_t gain_floor = std::max(first_gain, last_gain) >> kLoopRampGuardBits;
  int32_t multiplier = LoopArgumentMultiplier(
      std::max(first_gain, gain_floor), slope_q15, loop_gain_q15);
  const int32_t multiplier_slope = (LoopArgumentMultiplier(
      std::max(last_gain, gain_floor), slope_q15, loop_gain_q15) - multiplier)
      / static_cast<int32_t>(kAudioBlockSize - 1);

  uint32_t noise_state = noise_state_;
  int32_t tap_mean_q8 = loop_tap_mean_q8_;
  int32_t bp_q15_14 = state.bp_q15_14, lp_q15_14 = state.lp_q15_14;
  // A falling gain under a curve that folds back: the state comes down by the
  // block's fall as the block opens, so the multiplier's rise over the block
  // meets it rather than overdriving the curve (see kFollowsFallingGain).
  // lp also holds the direct term's DC, which the input still supplies as
  // the block opens: only the ring around it comes down, or the step pings the
  // resonator once a block -- a tone at the block rate.
  if (Loop::kFollowsFallingGain && last_gain < first_gain) {
    const int32_t direct_q15_14 = first_gain * direct;
    bp_q15_14 = ScaleRatio(bp_q15_14, last_gain, first_gain);
    lp_q15_14 = direct_q15_14
        + ScaleRatio(lp_q15_14 - direct_q15_14, last_gain, first_gain);
  }
  RENDER_CORE(this_sample,
    (void) timbre;  // read once a block, above
    const int32_t gain = input_samples[kAudioBlockSize];
    // The curve less its value at rest: what the loop feeds back and what it
    // outputs, scaled by gain.
    const int32_t curve_value =
        LoopCurve(curve, biased_offset + MulHighS(bp_q15_14, multiplier));
    const int32_t shaped = gain * (curve_value - rest);
    noise_state = NextXorshift32(noise_state);
    // One draw serves the input noise and the jitter; scaled with the pitch it
    // is the draw times the scale, 2^-kLoopRoughnessShift.
    const int32_t draw = Loop::kRoughnessByPitch
        ? (static_cast<int32_t>(noise_state) >> 16) * roughness
        : static_cast<int32_t>(noise_state);
    const int32_t excitation_q15_14 = gain * (direct + (draw >> (kLoopNoiseShift
        - (Loop::kRoughnessByPitch ? kLoopRoughnessShift : 0))));
    // Chamberlin, signed and unclipped: the loop bounds the state, and the
    // output is gated by gain, so a ring need not decay to exactly nothing.
    // The feedback and the damping share d: d * (feedback - bp), the feedback
    // shifted from shaped's units to the state's.
    lp_q15_14 += 2 * MulHighS(cutoff_q0_31, bp_q15_14);
    const int32_t feedback_q15_14 = shaped >> (15 + 15 - kLoopUnitShift_q15_14);
    int32_t damping_q15_14;
    if (Loop::kJitterShift) {
      // Bow pressure wavering: the whole friction force jitters, the curve's
      // value at rest included, d * jitter * noise times gain * curve. The
      // jitter is the draw >> kJitterShift, in the state's pre-shifted units
      // once the product drops what the draw's scale and that shift left over;
      // d then takes both terms at once. The draw is the excitation's: that
      // noise is 2^-kLoopNoiseBits of a unit, so what the two share is lost
      // under the jitter's own.
      const int32_t force_q15_14 =
          gain * curve_value >> (15 + 15 - kLoopUnitShift_q15_14);
      damping_q15_14 = ((((bp_q15_14 - feedback_q15_14) >> kLoopJitterPreShift)
          - ((force_q15_14 >> kLoopJitterPreShift) * (draw >> Loop::kJitterShift)
              >> (31 - kLoopRoughnessShift - 16)))
          * damp_u1_14) >> (14 - kLoopJitterPreShift);
    } else {
      damping_q15_14 = ((bp_q15_14 - feedback_q15_14) >> kLoopDampPreShift)
          * damp_u1_14 >> (14 - kLoopDampPreShift);
    }
    const int32_t bp_before_q15_14 = bp_q15_14;
    bp_q15_14 += 2 * MulHighS(
        cutoff_q0_31, excitation_q15_14 - lp_q15_14 - damping_q15_14);
    // A new cycle: draw its length. The draw is this sample's excitation's,
    // whose noise is 2^-kLoopNoiseBits of a unit -- lost under the ring.
    // Rare, and marked so: laid out inline, the loop costs two cycles more.
    if (Loop::kCycleDetune
        && __builtin_expect((bp_before_q15_14 & ~bp_q15_14) < 0, 0)) {
      const int32_t cycle_detune_q31 = 2 * MulHighS(
          static_cast<int32_t>(noise_state), Loop::kCycleDetune);
      loop_cycle_detune_q31_ = cycle_detune_q31;
      cutoff_q0_31 = note_cutoff_q0_31
          + 2 * MulHighS(note_cutoff_q0_31, cycle_detune_q31);
    }
    // At half scale, less its running mean.
#ifdef TEST
    if (g_loop_dc_before_gain) {
      // The DC taken out of the curve's value before the gain spends it: the
      // output is gain * (curve - its running mean), both inside the curve's
      // span, so it never passes the gain -- the headroom is 1.
      tap_mean_q8 += curve_value - (tap_mean_q8 >> kLoopDcBlockerShift);
      this_sample = gain * (curve_value - (tap_mean_q8 >> kLoopDcBlockerShift)) >> 16;
    } else {
#endif
    const int32_t tap = shaped >> 16;
    tap_mean_q8 += tap - (tap_mean_q8 >> kLoopDcBlockerShift);
    this_sample = tap - (tap_mean_q8 >> kLoopDcBlockerShift);
#ifdef TEST
    }
#endif
    multiplier += multiplier_slope;
  )
  state.bp_q15_14 = bp_q15_14;
  state.lp_q15_14 = lp_q15_14;
  state.Store(&svf);
  noise_state_ = noise_state;
  loop_tap_mean_q8_ = tap_mean_q8;
#ifdef TEST
  // LOOP_MEAN_PROBE=1 prints, once a block, the DC blocker's mean and the gain
  // peak it is bounded against, both in output codes.
  if (g_loop_mean_probe) {
    fprintf(stderr, "LOOPMEAN %ld %d\n", static_cast<long>(tap_mean_q8 >> kLoopDcBlockerShift),
            static_cast<int>(gain_envelope_peak_codes_u16(shape_)));
  }
#endif
  svf_ = svf;
}

void Oscillator::RenderWhistle(int16_t* input_samples, int16_t* audio_mix) {
  StateVariableFilter svf = svf_;
#ifdef TEST
  if (g_svf_probe && (g_svf_probe_n++ % g_svf_probe) == 0)
    fprintf(stderr, "WHIST bp=%ld lp=%ld rem=%ld,%ld gain=%d\n",
            (long) svf.bp, (long) svf.lp, (long) svf.bp_step_remainder_u15, (long) svf.lp_step_remainder_u15, (int) input_samples[kAudioBlockSize]);
#endif
  svf.RenderInitCutoff(SVF::CutoffFromFreq(pitch_));
  // sqrt(damp / reference), bounded at both ends by the constants it reads:
  // the excitation is scaled by it going in and the output by its reciprocal
  // coming out. It reads the damp the block opens on, where the filter below
  // reads a new one every sample -- a fast-moving TIMBRE makes the two differ.
  uint32_t damp_at_block_start_u1_14 = static_cast<uint32_t>(
      input_samples[0] > 0 ? input_samples[0] : 0);
  if (damp_at_block_start_u1_14 < kWhistleDriveFloor_u1_14) {
    damp_at_block_start_u1_14 = kWhistleDriveFloor_u1_14;
  }
  if (damp_at_block_start_u1_14 > kWhistleDriveReference_u1_14) {
    damp_at_block_start_u1_14 = kWhistleDriveReference_u1_14;
  }
  const int32_t damp_drive_u15 = IntegerSqrt(
      (damp_at_block_start_u1_14 << 15) / kWhistleDriveReference_u1_14 * 32768u);
  // The state is held in units of the drive, so a drive that moves leaves what
  // is already in the filter in the old ones, where the output's reciprocal no
  // longer cancels what produced it.
  if (previous_damp_drive_u15_ > 0 && damp_drive_u15 != previous_damp_drive_u15_) {
    // The rescale can carry the state 16x past int16, and the next cutoff * bp
    // then reaches 2.27e9 at MIDI 108. A state it puts out of range is one the
    // filter would have railed at had the drive been there all along.
    svf.bp = stmlib::Clip16(static_cast<int32_t>(svf.bp) * damp_drive_u15
        / previous_damp_drive_u15_);
    svf.lp = stmlib::Clip16(static_cast<int32_t>(svf.lp) * damp_drive_u15
        / previous_damp_drive_u15_);
  }
  previous_damp_drive_u15_ = damp_drive_u15;
  const int32_t state_to_output_q15 = WhistleStateToOutput(
      pitch_, incoherent_scale_u15_, g_no_makeup ? 0
          : g_half_makeup ? static_cast<int32_t>(IntegerSqrt(
                static_cast<uint32_t>(damp_drive_u15) << 15))
          : damp_drive_u15);
  const int32_t state_into_curve_q15 =
      StateIntoCurve(state_to_output_q15, coherent_scale_codes_u16_);
  // state_into_curve is u3.15: the rms-to-peak ratio and the make-up it
  // carries, over the curve's headroom, hold it under 8. Those integer bits come
  // off the drive rather than the state, so the state keeps every bit the filter
  // carried for it and s0.15 times the drive stays under 2^30.
  const int32_t kDriveHeadroomBits = 3;
  STATIC_ASSERT(
      (kIncoherentScaleRatioMax_u2_14 >> 14) * (1 << kWhistleDriveMakeUpBits)
          <= kSoftLimitHeadroom << kDriveHeadroomBits,
      whistle_drive_fits_its_bits);
  const int32_t drive_into_curve_q12 =
      TEST_CURVE_DRIVE(state_into_curve_q15 >> kDriveHeadroomBits);
  const int32_t scale_u15 = coherent_scale_u15_;
  const int16_t* curve = SoftLimitTableAsRegister();
  int32_t excitation_drive_u15 = damp_drive_u15;
#ifdef TEST
  if (g_limit_mode == 4) {
    const double bp_in_curve = stmlib::Clip16(static_cast<int32_t>(
        static_cast<int64_t>(svf.bp) * drive_into_curve_q12
            >> (12 - kWhistleCurveDriveBits)));
    const double lp_in_curve = stmlib::Clip16(static_cast<int32_t>(
        static_cast<int64_t>(svf.lp) * drive_into_curve_q12
            >> (12 - kWhistleCurveDriveBits)));
    const double x = (bp_in_curve * bp_in_curve + lp_in_curve * lp_in_curve)
        / __builtin_ldexp(1.0, g_limit_shift) / 32768.0;
    double g = g_limit_law == 1 ? (x < 1 ? 1 - x : 0)
        : g_limit_law == 2 ? 1 / (1 + x) : 1 / __builtin_sqrt(1 + x);
    excitation_drive_u15 = static_cast<int32_t>(damp_drive_u15 * g);
    if (getenv("WHISTLE_LIMIT_PROBE")) fprintf(stderr, "%.4f %.4f\n", x, g);
  }
#endif
  // The drive shifted so the product with a q15_14 state has the curve input
  // as its high word: one SMULL.
  const int32_t drive_into_curve_q32 = drive_into_curve_q12
      << (32 - 12 + kWhistleCurveDriveBits - ResonatorState::kFractionalBits);
  // Half a curve unit in the state's units, so the high word rounds rather than
  // floors: a ring decayed to its last fraction of a count, or an lp stranded
  // there, reads as zero.
  const int32_t half_curve_unit_q15_14 = static_cast<int32_t>(
      (1u << 31) / static_cast<uint32_t>(drive_into_curve_q32));
  uint32_t noise_state = noise_state_;
#ifdef TEST
  // K in state units: 4/pi of it is the overblown amplitude, mapped to curve
  // units by the output product (state * q12 / 2^22).
  g_jet_k = g_jet_target / (4 / M_PI) / (drive_into_curve_q12 / 4194304.0);
#endif
  ResonatorState state;
  state.Load(svf);
  RENDER_CORE(this_sample,
    const int16_t gain = input_samples[kAudioBlockSize];
    // Noise of its own, because WHISTLE sustains and the chiff decays.
    noise_state = NextXorshift32(noise_state);
    int32_t excitation =
        static_cast<int16_t>(noise_state >> 16) *
            (g_force_drive ? 32767 : gain) >> 15;
    const int32_t breath_q15_14 = JetNoise(excitation * excitation_drive_u15
        >> (15 - ResonatorState::kFractionalBits));
    const int32_t excitation_q15_14 =
        breath_q15_14 + JetFeedback(state.bp_q15_14, gain, timbre);
    const int32_t state_q15_14 = svf.RenderSampleAtPitch<SVF_BP>(
        &state, PreShape(excitation_q15_14), timbre);
    InLoopSaturate(&state.bp_q15_14, drive_into_curve_q32);
    this_sample = ResonatorShape(curve, g_multi
        ? static_cast<int32_t>(MultiWhistle(this, pitch_, gain, timbre,
              breath_q15_14, drive_into_curve_q32))
        : MulHighS(state_q15_14 + half_curve_unit_q15_14, drive_into_curve_q32),
        scale_u15);
  )
  state.Store(&svf);
  noise_state_ = noise_state;
  svf_ = svf;
}

// Above ~MIDI 63 the ring needs EXCITER AMOUNT: a bare envelope is too smooth
// to carry energy at the note.
void Oscillator::RenderPing(int16_t* input_samples, int16_t* audio_mix) {
  StateVariableFilter svf = svf_;
#ifdef TEST
  if (g_svf_probe && (g_svf_probe_n++ % g_svf_probe) == 0)
    fprintf(stderr, "PING bp=%ld lp=%ld gain0=%d\n",
            (long) svf.bp, (long) svf.lp, (long) svf.bp_step_remainder_u15, (long) svf.lp_step_remainder_u15, (int) input_samples[kAudioBlockSize]);
#endif
  svf.RenderInitCutoff(SVF::CutoffFromFreq(pitch_));
  // The ratio of the two peaks, so it follows either one if it moves.
  const int32_t kUnityStateToOutput_q12 =
      (kEnvelopeSampleMax << 12) / INT16_MAX;
  // A full-peak ring drives the curve this many times past unity, well past
  // its end: saturation holds the ring at the ceiling and brightens its onset,
  // and a lower envelope peak walks it back into the curve. Taken after the
  // curve's scale, so the drive is u3.12 and s0.15 times it stays under 2^30.
  const int32_t kPingDriveMultiple = 32;
  STATIC_ASSERT(kPingDriveMultiple <= kSoftLimitHeadroom << 3,
                ping_drive_fits_its_bits);
  const int32_t state_into_curve_q12 = TEST_CURVE_DRIVE(StateIntoCurve(
      kUnityStateToOutput_q12 * coherent_scale_u15_ >> 15,
      coherent_scale_codes_u16_) * kPingDriveMultiple);
  const int32_t scale_u15 = coherent_scale_u15_;
  // The exciter carries the gain envelope's DC, which lp passes: once the ring
  // dies away its state sits at the excitation's own level -- a thump under a
  // percussive envelope, a standing offset under a sustained one. bp and hp
  // reject it.
  // The resonant step response overshoots its input, so the excitation is
  // half the gain, and every bit of it the state can hold.
  // A narrow gain count is one state unit.
  const int kGainQ30ToHalfQ15_14 = kEnvelopeValueBits - kEnvelopeSampleBits
      + 1 - ResonatorState::kFractionalBits;
  const int32_t* gain_q30 =
      reinterpret_cast<const int32_t*>(input_samples + kAudioBlockSize);
#define PING_LOOP(OUTPUT) \
  RENDER_CORE(this_sample, \
    const int32_t state_q15_14 = svf.RenderSampleAtPitch<OUTPUT>( \
        &state, PreShape(*gain_q30++ >> kGainQ30ToHalfQ15_14), timbre); \
    InLoopSaturate(&state.bp_q15_14, state_into_curve_q32); \
    const int16_t* curve = SoftLimitTableAsRegister(); \
    this_sample = ResonatorShape(curve, MulHighS( \
        state_q15_14 + half_curve_unit_q15_14, state_into_curve_q32), scale_u15); \
  )
  // Shifted so the product with a q15_14 state has the curve input as its
  // high word: one SMULL.
  const int32_t state_into_curve_q32 = state_into_curve_q12
      << (32 - 12 - ResonatorState::kFractionalBits);
  const int32_t half_curve_unit_q15_14 = static_cast<int32_t>(
      (1u << 31) / static_cast<uint32_t>(state_into_curve_q32));
  ResonatorState state;
  state.Load(svf);
  switch (shape_) {
    case OSC_SHAPE_PING_LP: { PING_LOOP(SVF_LP) } break;
    case OSC_SHAPE_PING_BP: { PING_LOOP(SVF_BP) } break;
    case OSC_SHAPE_PING_HP: { PING_LOOP(SVF_HP) } break;
    default: break;
  }
#undef PING_LOOP
  state.Store(&svf);
  svf_ = svf;
}

void Oscillator::RenderDiracComb(int16_t* input_samples, int16_t* audio_mix) {
  RENDER_PERIODIC(
    int32_t zone_14 = pitch_ + ((32767 - timbre) >> 3);
    uint16_t crossfade = zone_14 << 6; // Ignore highest 4 bits
    size_t index = zone_14 >> 10; // Use highest 4 bits
    CONSTRAIN(index, 0, kNumZones - 1);
    const int16_t* wave_1 = waveform_table[WAV_BANDLIMITED_COMB_0 + index];
    index += 1;
    CONSTRAIN(index, 0, kNumZones - 1);
    const int16_t* wave_2 = waveform_table[WAV_BANDLIMITED_COMB_0 + index];
    this_sample = Crossfade(wave_1, wave_2, phase, crossfade);
  )
}

// The state into the soft limiter. Unity is 1/kSoftLimitHeadroom, the curve's
// small-signal gain; half again past that holds the level the hard clip used to
// produce, within 0.7 dB at every cutoff and every note.
//
// The stopband is what bounds the multiple. The curve's products are harmonics
// of what the filter passed, so they land where it is meant to be quiet, and a
// dark low-pass is where they stand highest above the signal: measured worst at
// 10.8 dB into 200 Hz-1 kHz at an eighth of the cutoff range, against 4.6 dB at
// unity and 19.2 dB at twice this.
static const int32_t kNoiseStateIntoCurve_q12 =
    3 * 4096 / (2 * kSoftLimitHeadroom);

void Oscillator::RenderFilteredNoise(int16_t* input_samples, int16_t* audio_mix) {
  StateVariableFilter svf = svf_;
  // The keyboard is this shape's resonance control, and it reads the shared
  // map, so the top of the keyboard self-oscillates.
  svf.RenderInitDamp(DampFromResonance(pitch_ << 1));
  const int16_t* curve = SoftLimitTableAsRegister();
  // Its own stream, held in a register: stmlib::Random keeps its state in a
  // static, and drawing from the shared one moves every other consumer's draws
  // along with it.
  uint32_t block_noise_state = noise_state_;
  // Which output the shape takes is fixed for the block, so it picks the loop
  // rather than being asked inside it.
#define NOISE_LOOP(OUTPUT) \
  RENDER_WITH_GAIN_AMPLIFYING_OUTPUT( \
    block_noise_state = NextXorshift32(block_noise_state); \
    const int32_t state = svf.RenderSample<OUTPUT>( \
        static_cast<int16_t>(block_noise_state >> 16), timbre); \
    this_sample = SoftLimit( \
        curve, state * kNoiseStateIntoCurve_q12 >> 12, kEnvelopeSampleMax); \
  )
#ifdef TEST
  if (g_svf_probe && (g_svf_probe_n++ % g_svf_probe) == 0)
    fprintf(stderr, "NOISE bp=%ld lp=%ld cutoff=%d\n",
            (long) svf.bp, (long) svf.lp, (int) input_samples[0]);
#endif
  switch (shape_) {
    case OSC_SHAPE_NOISE_NOTCH: { NOISE_LOOP(SVF_NOTCH) } break;
    case OSC_SHAPE_NOISE_LP: { NOISE_LOOP(SVF_LP) } break;
    case OSC_SHAPE_NOISE_BP: { NOISE_LOOP(SVF_BP) } break;
    case OSC_SHAPE_NOISE_HP: { NOISE_LOOP(SVF_HP) } break;
    default: break;
  }
#undef NOISE_LOOP
  noise_state_ = block_noise_state;
  svf_ = svf;
}

}  // namespace yarns
