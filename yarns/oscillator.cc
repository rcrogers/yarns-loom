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
// How far TIMBRE sweeps WHISTLE's and PING's Q, and so how far the damp
// correction's reciprocal may go.
static const uint32_t kWhistleQOctaves = 8;
// The widest damp those shapes ask for, which is Q 6.9; kWhistleQOctaves above
// it is Q 1820.
static const uint32_t kWhistleDampMax_u1_14 = 2392;
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
Oscillator::RenderFn Oscillator::fn_table_[] = {
  &Oscillator::RenderFilteredNoise,
  &Oscillator::RenderFilteredNoise,
  &Oscillator::RenderFilteredNoise,
  &Oscillator::RenderFilteredNoise,
  &Oscillator::RenderPhaseDistortionPulse,
  &Oscillator::RenderPhaseDistortionPulse,
  &Oscillator::RenderPhaseDistortionPulse,
  &Oscillator::RenderPhaseDistortionPulse,
  &Oscillator::RenderPhaseDistortionSaw,
  &Oscillator::RenderPhaseDistortionSaw,
  &Oscillator::RenderPhaseDistortionSaw,
  &Oscillator::RenderPhaseDistortionSaw,
  &Oscillator::RenderLPPulse,
  &Oscillator::RenderLPSaw,
  &Oscillator::RenderVariableSine,
  &Oscillator::RenderVariablePulse,
  &Oscillator::RenderVariableSaw,
  &Oscillator::RenderSawPulseMorph,
  &Oscillator::RenderSyncSine,
  // &Oscillator::RenderSyncTriangle,
  &Oscillator::RenderSyncPulse,
  &Oscillator::RenderSyncSaw,
  &Oscillator::RenderWhistle,
  &Oscillator::RenderPing,
  &Oscillator::RenderPing,
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

void StateVariableFilter::RenderInit(int16_t resonance_u15) {
  damp.SetTarget(DampFromResonance(resonance_u15));
  damp.ComputeSlope();
}

void StateVariableFilter::RenderInitCutoff(int16_t cutoff_u15) {
  cutoff.SetTarget(cutoff_u15);
  cutoff.ComputeSlope();
}

void Oscillator::Refresh(int16_t pitch, int16_t timbre_bias, uint16_t gain_bias) {
  pitch_ = pitch;
  // if (shape_ >= OSC_SHAPE_FM) {
  //   pitch_ += lut_fm_carrier_corrections[shape_ - OSC_SHAPE_FM];
  // }
  CONSTRAIN(pitch_, 0, kHighestNote - 1);
  phase_increment_ = ComputePhaseIncrement(pitch_);
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
    return ComputePhaseIncrement(shifted_pitch) >> (32 - 15);
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
  // instead of moving it. Carried as damp, geometrically: a resonance stops at
  // the damp LUT's last entry, which is Q 129.
  if (shape >= OSC_SHAPE_WHISTLE && shape <= OSC_SHAPE_PING_LP) {
    // Below the bottom of the map is the widest setting: the cast wraps a
    // negative timbre into a shift of 65527, which takes the damp to zero, and
    // a resonator with no loss in it grows for as long as the note is held.
    if (timbre < 0) timbre = 0;
    uint32_t octaves_q16 =
        (static_cast<uint32_t>(timbre) * kWhistleQOctaves) << 1;
    int32_t damp = static_cast<int32_t>(kWhistleDampMax_u1_14 * // 2^-octaves
      Interpolate88(lut_expo2_neg_u16, octaves_q16 & 0xffff) >> 16);
    return static_cast<int16_t>(damp >> (octaves_q16 >> 16));
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

void Oscillator::set_shape(OscillatorShape new_shape) {
  if (shape_ == new_shape) return;

  // Remap timbre envelope on the fly so held notes keep an ~equivalent timbre.
  // Rescale divides each level by new_scale/old_scale exactly (no soft-float);
  // it no-ops if old_scale is degenerate (the float path divided by zero).
  int16_t midpoint_timbre = 1 << 14;
  int32_t old_scale = WarpTimbre(midpoint_timbre, shape_);
  int32_t new_scale = WarpTimbre(midpoint_timbre, new_shape);
  timbre_envelope_.Rescale(new_scale, old_scale);

  // scale_for_shape moves when the new shape sums its voices differently, and a
  // held note should change shape without changing loudness.
  gain_envelope_.Rescale(scale_for_shape(new_shape), scale_for_shape(shape_));

  shape_ = new_shape;

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
// buffers are stack locals, which yarns/stack_budget.h accounts for.
void Oscillator::Render(int16_t* audio_mix) {
  // Skipping zero-init: both buffers are fully overwritten by the
  // envelope renders below.
  // Timbre at [p], gain at [p + kAudioBlockSize]: one pointer walks both, and
  // every register held here is one the shape cannot have.
  int16_t timbre_gain[2 * kAudioBlockSize];
  // The shapes are handed the WHOLE array and index the gain half off it, so
  // what they take is input_samples, not either half by itself.
  int16_t* input_samples = &timbre_gain[0];
  int16_t* gain_samples = &timbre_gain[kAudioBlockSize];
  int16_t timbre_bias = WarpTimbre(raw_timbre_bias_);
  timbre_envelope_.RenderSamples(
    input_samples, static_cast<int32_t>(static_cast<uint32_t>(timbre_bias) << 16));

  int16_t gain_bias = gain_envelope_.tremolo(raw_gain_bias_);
  gain_envelope_.RenderSamples(
    gain_samples, static_cast<int32_t>(static_cast<uint32_t>(gain_bias) << 16));

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
// single walk down the two halves. The caller supplies mix_term, since shapes
// differ in where they spend the gain envelope.
#define RENDER_LOOP(mix_term, ...) \
  int16_t next_sample = next_sample_; \
  for (size_t size = kAudioBlockSize; size--;) { \
    int16_t timbre = input_samples[0]; \
    int16_t this_sample = next_sample; \
    next_sample = 0; \
    __VA_ARGS__ \
    int32_t mixed = (mix_term); \
    ++input_samples; \
    *audio_mix = static_cast<int16_t>(*audio_mix + mixed); \
    ++audio_mix; \
  } \
  next_sample_ = next_sample; \

#define RENDER_CORE(...) \
  RENDER_LOOP( \
    (static_cast<int32_t>(this_sample) * \
     input_samples[kAudioBlockSize]) >> 15, /* the other half */ \
    __VA_ARGS__) \

// The body spends the gain half on the way into whatever rings, so the mix
// takes the sample as it stands.
#define RENDER_CORE_EXCITED(...) \
  RENDER_LOOP(this_sample, __VA_ARGS__) \

#define RENDER_PERIODIC(...) \
  uint32_t phase = phase_; \
  uint32_t phase_increment = phase_increment_; \
  RENDER_CORE( \
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
// phase_increment / 2^32. tools/osc_cycles.py locates these by source line and
// charges what they guard at that rate.
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

// Timbre is unsigned here: the shift puts it in the modulator's phase
// increment, where a sign bit reads as a rate hundreds of thousands of times
// too fast.
#define SET_MODULATOR_PHASE_INCREMENT_FROM_TIMBRE \
  uint32_t modulator_phase_increment = timbre << (32 - kEnvelopeSampleBits);

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
  svf.RenderInit(0x7fff);
  uint32_t pw = 0x80000000;
  RENDER_PERIODIC(
    bool self_reset = PhaseWrapped(phase, phase_increment);
    while (true) { EDGES_PULSE(phase, phase_increment) }
    next_sample += phase < pw ? 0 : 0x7fff;
    svf.RenderSample(this_sample, timbre);
    this_sample = svf.lp;
  )
  svf_ = svf;
}

void Oscillator::RenderLPSaw(int16_t* input_samples, int16_t* audio_mix) {
  StateVariableFilter svf = svf_;
  svf.RenderInit(0x6000);
  RENDER_PERIODIC(
    bool self_reset = PhaseWrapped(phase, phase_increment);
    while (true) { EDGES_SAW(phase, phase_increment) }
    next_sample += phase >> 17;
    svf.RenderSample(this_sample, timbre);
    this_sample = svf.lp;
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
    uint32_t saw_width = UINT32_MAX - (pw << 1); // 0-100% width of up-ramp

    bool self_reset = PhaseWrapped(phase, phase_increment);
    // BLEP falling pulse edge only
    while (self_reset) { EDGES_PULSE(phase, phase_increment) }
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

void Oscillator::RenderTanhSine(int16_t* input_samples, int16_t* audio_mix) {
  RENDER_PERIODIC(
    this_sample = sine(phase);
    int16_t baseline = this_sample >> 6;
    this_sample = baseline + ((this_sample - baseline) * timbre >> 15);
    this_sample = Interpolate88(ws_violent_overdrive, this_sample + 32768);
  )
}

void Oscillator::RenderExponentialSine(int16_t* input_samples, int16_t* audio_mix) {
  RENDER_PERIODIC(
    timbre = (timbre >> 1) + (timbre >> 2) + (timbre >> 3) + 0x0fff; // Use top 7/8
    int16_t sine_sample = sine(phase);
    int32_t scaled_sine = sine_sample * timbre;

    int16_t dither = phase ^ (phase >> 16);
    int16_t dither_14 = dither >> (16 - 14);
    int32_t dithered_scaled_sine = (scaled_sine + dither_14) >> 15;

    this_sample = Interpolate88(wav_sizzle, dithered_scaled_sine + 0x8000);
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

void Oscillator::RenderPhaseDistortionPulse(int16_t* input_samples, int16_t* audio_mix) {
  uint8_t filter_type = shape_ - OSC_SHAPE_CZ_PULSE_LP;
  int32_t integrator = pd_square_.integrator;
  RENDER_MODULATED(
    SET_MODULATOR_PHASE_INCREMENT_FROM_TIMBRE;
    modulator_phase += modulator_phase_increment;
    if ((phase << 1) < (phase_increment << 1)) {
      pd_square_.polarity = !pd_square_.polarity;
      modulator_phase = kPhaseResetPulse[filter_type];
    }
    int16_t carrier = sine(modulator_phase);
    uint16_t window = ~(phase >> 15); // Double saw
    int16_t pulse = (carrier * window) >> 16;
    if (pd_square_.polarity) pulse = -pulse;
    uint16_t integrator_gain = modulator_phase_increment >> 16; // Orig 14
    integrator += (pulse * integrator_gain) >> 14; // Orig 16
    CLIP(integrator)
    int16_t output;
    if (filter_type & 2) { // Band- or high-pass
      output = pulse;
    } else {
      // TODO HP is 2dB above LP, which is 2dB above PK
      output = integrator;
      if (filter_type == 1) { // Peaking
        output = (pulse + integrator) >> 1;
      }
    }
    this_sample = output;
  )
  pd_square_.integrator = integrator;
}

void Oscillator::RenderPhaseDistortionSaw(int16_t* input_samples, int16_t* audio_mix) {
  uint8_t filter_type = shape_ - OSC_SHAPE_CZ_SAW_LP;
  RENDER_MODULATED(
    SET_MODULATOR_PHASE_INCREMENT_FROM_TIMBRE;
    modulator_phase += modulator_phase_increment;
    if (PhaseWrapped(phase, phase_increment)) {
      modulator_phase = kPhaseResetSaw[filter_type];
    }
    int16_t carrier = sine(modulator_phase);
    uint16_t window = ~(phase >> 16); // Saw
    int16_t output;
    if (filter_type & 2) { // Band- or high-pass
      output = (window * carrier) >> 16;
    } else {
      // Unsigned: the product is 65535 * 65535 at the corner, and that
      // overflows int32. What it does today is wrap, and the int16 store then
      // truncates the wrap away, so the OUTPUT is right -- MEASURED identical
      // to the same expression in 64 bits over the whole domain. But signed
      // overflow is undefined and GCC optimises on that, so the modular
      // arithmetic this depends on is spelled out instead of assumed.
      output = (static_cast<uint32_t>(window) * (carrier + 32768) >> 16) - 32768;
    }
    this_sample = output;
  )
}

// Below this the cutoff coefficient stops tracking and the resonance is the
// only pitch the shape has: at MIDI 24 the peak sits at 43.9 Hz for a note of
// 32.7.
// The soft limiter's curve reaches this many times scale_, so an excursion of
// up to that much is compressed instead of clipped. The curve is tanh(k*x)
// with k equal to this, which is what makes its gain 1 for small signals, so
// changing one means changing the other. yarns/resources/waveshapers.py
// generates the table.
static const int32_t kSoftLimitHeadroom = 4;

// The curve is passed rather than read here so the caller can pin its address
// to a register: GCC re-materializes it inside the sample loop otherwise, once
// for each of the two entries read.
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

static const int32_t kWhistleLowestPitch = 30 << 7;
// The resonator's gain at resonance rises as 1/sqrt(damp), and rises again
// with pitch, by 2.85 dB an octave.
//
// The damp term is corrected at the input, by scaling the excitation, which
// moves the filter state and leaves the level alone. The pitch term is
// corrected here at the output, which moves the level and leaves the state
// alone -- so the state still rails above MIDI 84, where that term is
// largest.
static int32_t WhistleStateToOutput(
    int32_t pitch, int32_t scale_u15,
    int32_t damp_drive_u15) {
  // Half an octave of level per octave of pitch, which holds rms flat to
  // MIDI 84 and under-corrects above it.
  const int32_t pitch_correction_numerator = 1;
  const int32_t pitch_correction_denominator = 2;
  // How far into the curve the signal is driven, which is the level: noise
  // visits its peak rarely, and everything under it is unspent until something
  // bends the peak.
  const int32_t level_into_knee_u15 = 18800;
  int32_t octaves_q16 = (pitch - kWhistleLowestPitch) * 65536 / (12 * 128);
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

void Oscillator::RenderWhistle(int16_t* input_samples, int16_t* audio_mix) {
  StateVariableFilter svf = svf_;
  int32_t resonant_pitch = pitch_ < kWhistleLowestPitch ? kWhistleLowestPitch : pitch_;
  svf.RenderInitCutoff(SVF::CutoffFromFreq(resonant_pitch));
  // For a fixed drive, bp grows as sqrt(Q), so at the tightest damp it hits its
  // rail and only a few of its bits are signal. Scaling the excitation down by
  // sqrt(damp) and scaling the output back up by the same factor keeps bp in
  // range and leaves the level unchanged.
  //
  // The timbre envelope can overshoot below the smallest damp the warp
  // produces. Clamping it here keeps that make-up factor finite: it is a
  // reciprocal, and an unclamped damp lets it jump 50x from one block to the
  // next.
  const uint32_t damp_min_u1_14 =
      kWhistleDampMax_u1_14 >> kWhistleQOctaves;
  // This is the damp at the start of the block, but the filter below reads a
  // new damp every sample. A fast-moving TIMBRE makes the two differ.
  uint32_t damp_at_block_start_u1_14 = static_cast<uint32_t>(
      input_samples[0] > 0 ? input_samples[0] : 0);
  if (damp_at_block_start_u1_14 < damp_min_u1_14) {
    damp_at_block_start_u1_14 = damp_min_u1_14;
  }
  if (damp_at_block_start_u1_14 > kWhistleDampMax_u1_14) {
    damp_at_block_start_u1_14 = kWhistleDampMax_u1_14;
  }
  const int32_t damp_drive_u15 = IntegerSqrt(
      (damp_at_block_start_u1_14 << 15) / kWhistleDampMax_u1_14 * 32768u);
  // The excitation is scaled by damp_drive_u15 going in and the output by its
  // reciprocal coming out, so the state is held in units of that drive. When
  // the drive moves, the state already in the filter is still in the old
  // units, and the reciprocal at the output no longer cancels what produced
  // it. Rescale it into the new units.
  if (previous_damp_drive_u15_ > 0 && damp_drive_u15 != previous_damp_drive_u15_) {
    // Clipped like every other write to these. The rescale can carry the state
    // 16x past int16, and the next cutoff * bp then reaches 2.27e9 at MIDI 108,
    // outside int32. A state the rescale puts out of range is one the filter
    // would have railed at had the drive been there all along.
    svf.bp = stmlib::Clip16(static_cast<int32_t>(svf.bp) * damp_drive_u15
        / previous_damp_drive_u15_);
    svf.lp = stmlib::Clip16(static_cast<int32_t>(svf.lp) * damp_drive_u15
        / previous_damp_drive_u15_);
  }
  previous_damp_drive_u15_ = damp_drive_u15;
  const int32_t state_to_output_q15 = WhistleStateToOutput(
      resonant_pitch, incoherent_scale_u15_, damp_drive_u15);
  const int32_t state_into_curve_q15 = static_cast<int32_t>(DivU64ByU32(
      stmlib::MulU32(static_cast<uint32_t>(state_to_output_q15), INT16_MAX),
      static_cast<uint32_t>(state_to_output_q15) * INT16_MAX,
      static_cast<uint32_t>(scale_) * kSoftLimitHeadroom));
  // bp is int16 and the drive reaches 77k, so the full-precision product does
  // not fit int32. Three bits off the drive make it fit with room to spare --
  // a constant, so what they cost is 0.17% of gain at worst and nothing that
  // varies -- and the state keeps every bit the filter carried for it.
  const int32_t kDriveHeadroomBits = 3;
  const int32_t drive_into_curve_q12 =
      state_into_curve_q15 >> kDriveHeadroomBits;
  const int32_t scale_u15 = coherent_scale_u15_;
  // Where the pin sits is measured, not free: hoisted here this loop is 68
  // cycles a sample and 74 with it at the call site, and RenderPing is the
  // other way round.
  const int16_t* curve = ws_soft_limit;
  asm volatile ("" : "+r"(curve));
  uint32_t noise_state = noise_state_;
  RENDER_CORE_EXCITED(
    // Noise of its own, because a whistle sustains and the chiff decays.
    noise_state = NextXorshift32(noise_state);
    int32_t excitation = static_cast<int16_t>(noise_state >> 16)
        * input_samples[kAudioBlockSize] >> 15;
    excitation = excitation * damp_drive_u15 >> 15;
    svf.RenderSampleAtPitch(excitation, timbre);
    const int32_t state_in_curve = stmlib::Clip16(
        svf.bp * drive_into_curve_q12 >> (15 - kDriveHeadroomBits));
    this_sample = SoftLimit(curve, state_in_curve, scale_u15);
  )
  noise_state_ = noise_state;
  svf_ = svf;
}

// Above ~MIDI 63 the ring needs EXCITER AMOUNT: a bare envelope is too smooth
// to carry energy at the note.
void Oscillator::RenderPing(int16_t* input_samples, int16_t* audio_mix) {
  StateVariableFilter svf = svf_;
  int32_t resonant_pitch = pitch_ < kWhistleLowestPitch ? kWhistleLowestPitch : pitch_;
  svf.RenderInitCutoff(SVF::CutoffFromFreq(resonant_pitch));
  // The low-pass passes the exciter's DC, so once the ring dies away its state
  // sits at the excitation's own level -- a thump under a percussive envelope,
  // a standing offset under a sustained one. The band-pass rejects the DC.
  const bool is_band_pass = shape_ == OSC_SHAPE_PING_BP;
  // svf.bp and svf.lp are both clipped to INT16_MAX, so a gain that maps
  // INT16_MAX onto scale_ uses the whole of their range without exceeding it.
  // A ring only touches that peak briefly, so kPingDriveMultiple drives past
  // it and leaves the curve to compress what goes over: 5.4 dB louder in the
  // band-pass, 4.2 dB in the low-pass.
  const int32_t kUnityStateToOutput_q12 =
      (kEnvelopeSampleMax << 12) / INT16_MAX;
  const int32_t kPingDriveMultiple = 2;
  const int32_t state_to_output_q12 = kUnityStateToOutput_q12
      * kPingDriveMultiple * coherent_scale_u15_ >> 15;
  // state_in_curve therefore peaks at INT16_MAX * kPingDriveMultiple /
  // kSoftLimitHeadroom, which stays inside the table as long as the multiple
  // does not exceed the headroom.
  STATIC_ASSERT(kPingDriveMultiple <= kSoftLimitHeadroom,
                ping_drive_leaves_curve);
  const int32_t state_into_curve_q12 = state_to_output_q12 * INT16_MAX
      / (scale_ * kSoftLimitHeadroom);
  const int32_t scale_u15 = coherent_scale_u15_;
  RENDER_CORE_EXCITED(
    // The resonant step response overshoots its input, so the excitation is
    // halved to leave room for the overshoot.
    svf.RenderSampleAtPitch(input_samples[kAudioBlockSize] >> 1, timbre);
    const int32_t state_in_curve =
        (is_band_pass ? svf.bp : svf.lp) * state_into_curve_q12 >> 12;
    const int16_t* curve = ws_soft_limit;
    asm volatile ("" : "+r"(curve));
    this_sample = SoftLimit(curve, state_in_curve, scale_u15);
  )
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

void Oscillator::RenderFilteredNoise(int16_t* input_samples, int16_t* audio_mix) {
  StateVariableFilter svf = svf_;
  svf.RenderInit(pitch_ << 1);
  OscillatorShape shape = shape_;
  RENDER_CORE(
    svf.RenderSample(Random::GetSample(), timbre);
    switch (shape) {
      case OSC_SHAPE_NOISE_LP: this_sample = svf.lp; break;
      case OSC_SHAPE_NOISE_NOTCH: this_sample = svf.notch; break;
      case OSC_SHAPE_NOISE_BP: this_sample = svf.bp; break;
      case OSC_SHAPE_NOISE_HP: this_sample = svf.hp; break;
      default: break;
    }
  )
  svf_ = svf;
}

}  // namespace yarns
