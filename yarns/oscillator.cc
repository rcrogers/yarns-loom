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

namespace yarns {

using namespace stmlib;

static const size_t kNumZones = 15;

static const uint16_t kHighestNote = 128 * 128;
static const uint16_t kPitchTableStart = 116 * 128;
static const uint16_t kOctave = 12 * 128;

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
  &Oscillator::RenderVariablePulse,
  &Oscillator::RenderVariableSaw,
  &Oscillator::RenderSawPulseMorph,
  &Oscillator::RenderSyncSine,
  // &Oscillator::RenderSyncTriangle,
  &Oscillator::RenderSyncPulse,
  &Oscillator::RenderSyncSaw,
  &Oscillator::RenderFoldSine,
  &Oscillator::RenderFoldTriangle,
  &Oscillator::RenderDiracComb,
  &Oscillator::RenderTanhSine,
  &Oscillator::RenderExponentialSine,
  &Oscillator::RenderFM,
};

STATIC_ASSERT(
  sizeof(Oscillator::fn_table_) / sizeof(Oscillator::RenderFn) == OSC_SHAPE_FM + 1,
  oscillator_fn_table_size_mismatch
);

void StateVariableFilter::Init() {
  damp.Init();
}

// 15-bit params
void StateVariableFilter::RenderInit(int16_t resonance) {
  damp.SetTarget(Interpolate824(lut_svf_damp, resonance << 17) >> 1);
  damp.ComputeSlope();
}

void Oscillator::Refresh(int16_t pitch, int16_t timbre_bias, uint16_t gain_bias) {
  pitch_ = pitch;
  // if (shape_ >= OSC_SHAPE_FM) {
  //   pitch_ += lut_fm_carrier_corrections[shape_ - OSC_SHAPE_FM];
  // }
  raw_gain_bias_ = gain_bias;
  raw_timbre_bias_ = timbre_bias;
}

int16_t Oscillator::WarpTimbre(int16_t timbre, OscillatorShape shape) const {
  // Limit cutoff range for filtered noise
  if (shape >= OSC_SHAPE_NOISE_NOTCH && shape <= OSC_SHAPE_NOISE_HP) {
    int32_t cutoff_freq = 0x1000 + (timbre >> 1); // 1/8..5/8
    return Interpolate824(lut_svf_cutoff, cutoff_freq << 17) >> 1;
  }

  // LP filter cutoff tracks pitch
  if (shape >= OSC_SHAPE_LP_PULSE && shape <= OSC_SHAPE_LP_SAW) {
    int32_t cutoff_freq = (pitch_ >> 1) + (timbre >> 1);
    CONSTRAIN(cutoff_freq, 0, 0x7fff);
    return Interpolate824(lut_svf_cutoff, cutoff_freq << 17) >> 1;
  }

  // Phase distortion modulator tracks pitch
  if (shape >= OSC_SHAPE_CZ_PULSE_LP && shape <= OSC_SHAPE_CZ_SAW_HP) {
    int16_t timbre_offset = timbre - 2048;
    int32_t shifted_pitch = pitch_ + (timbre_offset >> 2) + (timbre_offset >> 4) + (timbre_offset >> 8);
    if (shifted_pitch >= kHighestNote) shifted_pitch = kHighestNote - 1;
    return ComputePhaseIncrement(shifted_pitch) >> (32 - 15);
  }

  // Sync modulator tracks pitch
  if (shape >= OSC_SHAPE_SYNC_SINE && shape <= OSC_SHAPE_SYNC_SAW) {
    int32_t modulator_pitch = pitch_ + (timbre >> 3);
    CONSTRAIN(modulator_pitch, 0, kHighestNote - 1);
    return ComputePhaseIncrement(modulator_pitch) >> (32 - 15);
  }

  if (
    shape == OSC_SHAPE_FOLD_SINE ||
    shape == OSC_SHAPE_FOLD_TRIANGLE ||
    shape == OSC_SHAPE_EXP_SINE ||
    shape >= OSC_SHAPE_FM
  ) {
    // Additive synthesis reduces timbre as pitch increases
    int32_t lowness = 0x7fff - (pitch_ << 1);
    CONSTRAIN(lowness, 0, 0x7fff);
    return timbre * lowness >> 15;
  }

  return timbre;
}

void Oscillator::set_shape(OscillatorShape new_shape) {
  if (shape_ == new_shape) return;

  // Remap timbre envelope on the fly so held notes keep an ~equivalent timbre
  int16_t midpoint_timbre = 1 << 14;
  float old_scale = static_cast<float>(WarpTimbre(midpoint_timbre, shape_));
  float new_scale = static_cast<float>(WarpTimbre(midpoint_timbre, new_shape));
  float scaling_factor = new_scale / old_scale;
  timbre_envelope_.Rescale(scaling_factor);

  shape_ = new_shape;
}

uint32_t Oscillator::ComputePhaseIncrement(int16_t midi_pitch) const {
  int16_t num_shifts = 0;
  while (midi_pitch >= kHighestNote) {
    midi_pitch -= kOctave;
    --num_shifts;
  }
  int32_t ref_pitch = midi_pitch;
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
    num_shifts = std::min(__builtin_clzl(phase_increment), static_cast<int>(-num_shifts));
    phase_increment <<= num_shifts;
  }
  return phase_increment;
}

void Oscillator::Render(int16_t* audio_mix) {
  if (pitch_ >= kHighestNote) {
    pitch_ = kHighestNote - 1;
  } else if (pitch_ < 0) {
    pitch_ = 0;
  }
  phase_increment_ = ComputePhaseIncrement(pitch_);
  
  int16_t timbre_samples[kAudioBlockSize] = {0};
  int16_t timbre_bias = WarpTimbre(raw_timbre_bias_);
  timbre_envelope_.RenderSamples(timbre_samples, timbre_bias << 16);

  uint8_t fn_index = shape_;
  CONSTRAIN(fn_index, 0, OSC_SHAPE_FM);
  RenderFn fn = fn_table_[fn_index];
  int16_t audio_samples[kAudioBlockSize] = {0};
  (this->*fn)(timbre_samples, audio_samples);

  int16_t gain_samples[kAudioBlockSize] = {0};
  int16_t gain_bias = gain_envelope_.tremolo(raw_gain_bias_);
  gain_envelope_.RenderSamples(gain_samples, gain_bias << 16);
  
  q15_multiply_accumulate<kAudioBlockSize>(gain_samples, audio_samples, audio_mix);
}

#define RENDER_CORE(body) \
  int32_t next_sample = next_sample_; \
  for (size_t size = kAudioBlockSize; size--;) { \
    int16_t timbre = *timbre_samples++; \
    int32_t this_sample = next_sample; \
    next_sample = 0; \
    body \
    *audio_samples++ = this_sample; \
  } \
  next_sample_ = next_sample; \

#define RENDER_PERIODIC(body) \
  uint32_t phase = phase_; \
  uint32_t phase_increment = phase_increment_; \
  RENDER_CORE( \
    phase += phase_increment; \
    body \
  ) \
  phase_ = phase; \

// NB: 'modulator' is detuned from canonical pitch. In sync, it's the
// follower/output oscillator
#define RENDER_MODULATED(body) \
  uint32_t modulator_phase = modulator_phase_; \
  RENDER_PERIODIC(body); \
  modulator_phase_ = modulator_phase; \

#define EDGES_SAW(ph, ph_incr) \
  if (!self_reset) break; \
  self_reset = false; \
  uint32_t t = ph / (ph_incr >> 16); \
  this_sample -= ThisBlepSample(t); \
  next_sample -= NextBlepSample(t); \

#define EDGES_PULSE(ph, ph_incr) \
  if (!high_) { \
    if (ph < pw) break; \
    uint32_t t = (ph - pw) / (ph_incr >> 16); \
    this_sample += ThisBlepSample(t); \
    next_sample += NextBlepSample(t); \
    high_ = true; \
  } \
  if (high_) { \
    if (!self_reset) break; \
    self_reset = false; \
    uint32_t t = ph / (ph_incr >> 16); \
    this_sample -= ThisBlepSample(t); \
    next_sample -= NextBlepSample(t); \
    high_ = false; \
  }

#define SET_MODULATOR_PHASE_INCREMENT_FROM_TIMBRE \
  uint32_t modulator_phase_increment = timbre << (32 - 15);

#define SYNC(discontinuity_code, edges_code, extra_transition_code) \
  bool sync_reset = false; \
  bool self_reset = false; \
  bool transition_during_reset = false; \
  uint32_t reset_time = 0; \
  SET_MODULATOR_PHASE_INCREMENT_FROM_TIMBRE; \
  if (phase < phase_increment) { \
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
  self_reset = modulator_phase < modulator_phase_increment; \
  /* Block additional BLEP if modulator was reset by master alone */ \
  bool reset_by_master_only = sync_reset && !transition_during_reset; \
  while (!reset_by_master_only) { \
    edges_code; \
  } \
  if (sync_reset) { \
    modulator_phase = reset_time * (modulator_phase_increment >> 16); \
    high_ = false; \
  } \

#define TRIANGLE_UNIPOLAR(phase) \
  (((phase >> 16) << 1) ^ ((phase >> 16) & 0x8000 ? 0xffff : 0x0000))

#define TRIANGLE_BIPOLAR(phase) \
  TRIANGLE_UNIPOLAR(phase) - 0x8000

void Oscillator::RenderLPPulse(int16_t* timbre_samples, int16_t* audio_samples) {
  StateVariableFilter svf = svf_;
  svf.RenderInit(0x7fff);
  uint32_t pw = 0x80000000;
  RENDER_PERIODIC(
    bool self_reset = phase < phase_increment;
    while (true) { EDGES_PULSE(phase, phase_increment) }
    next_sample += phase < pw ? 0 : 0x7fff;
    svf.RenderSample(this_sample, timbre);
    this_sample = svf.lp;
  )
  svf_ = svf;
}

void Oscillator::RenderLPSaw(int16_t* timbre_samples, int16_t* audio_samples) {
  StateVariableFilter svf = svf_;
  svf.RenderInit(0x6000);
  RENDER_PERIODIC(
    bool self_reset = phase < phase_increment;
    while (true) { EDGES_SAW(phase, phase_increment) }
    next_sample += phase >> 17;
    svf.RenderSample(this_sample, timbre);
    this_sample = svf.lp;
  )
  svf_ = svf;
}

void Oscillator::RenderVariablePulse(int16_t* timbre_samples, int16_t* audio_samples) {
  RENDER_PERIODIC(
    timbre = timbre + (timbre >> 1); // 3/4
    uint32_t pw = (UINT16_MAX - Interpolate88(lut_env_expo, timbre)) << 15; // 50-0%
    bool self_reset = phase < phase_increment;
    while (true) { EDGES_PULSE(phase, phase_increment) }
    next_sample += phase < pw ? 0 : 0x7fff;
    this_sample = (this_sample - 0x4000) << 1;
  )
}

void Oscillator::RenderVariableSaw(int16_t* timbre_samples, int16_t* audio_samples) {
  RENDER_PERIODIC(
    bool self_reset = phase < phase_increment;
    while (true) { EDGES_SAW(phase, phase_increment) }
    timbre = timbre + (timbre >> 1); // 3/4
    uint16_t saw_width = UINT16_MAX - Interpolate88(lut_env_expo, timbre); // 100-0%
    if ((phase >> 16) < saw_width) next_sample += (phase / saw_width) >> 1;
    else next_sample += 0x7fff;
    this_sample = (this_sample - 0x4000) << 1;
  )
}

// Shape: low flat + up-ramp + high flat + fall.  Timbre increases width of
// flats + slope of up-ramp
//
// ⟋|⟋| -> _/‾|_/‾| -> _|‾|_|‾|
void Oscillator::RenderSawPulseMorph(int16_t* timbre_samples, int16_t* audio_samples) {
  RENDER_PERIODIC(
    // Prevent saw from reaching an infinitely steep rise, else we'd have to
    // clumsily transition into a BLEP of what is now a rising pulse edge
    timbre = timbre + (timbre >> 1) + (timbre >> 2) + (timbre >> 3) + (timbre >> 4); // 31/32

    // Exponential timbre curve, biased high
    uint32_t pw = Interpolate88(lut_env_expo, timbre) << 15; // 0-50% width of each flat part
    uint32_t saw_width = UINT32_MAX - (pw << 1); // 0-100% width of up-ramp

    bool self_reset = phase < phase_increment;
    // BLEP falling pulse edge only
    while (self_reset) { EDGES_PULSE(phase, phase_increment) }
    if (phase < pw) next_sample += 0;
    else if (phase < pw + saw_width) next_sample += ((phase - pw) / (saw_width >> 16)) >> 1;
    else next_sample += 0x7fff;
    this_sample = (this_sample - 0x4000) << 1;
  )
}

void Oscillator::RenderSyncSine(int16_t* timbre_samples, int16_t* audio_samples) {
  RENDER_MODULATED(
    SYNC(
      wav_sine[0] - Interpolate824(wav_sine, modulator_phase_at_reset),
      break, // No edges
      false // No extra transition
    );
    (void) transition_during_reset; (void) sync_reset; (void) self_reset;
    this_sample = Interpolate824(wav_sine, modulator_phase);
  )
}

void Oscillator::RenderSyncPulse(int16_t* timbre_samples, int16_t* audio_samples) {
  uint32_t pw = 0x80000000;
  RENDER_MODULATED(
    SYNC(
      0 - (modulator_phase_at_reset < pw ? 0 : 32767),
      EDGES_PULSE(modulator_phase, modulator_phase_increment),
      !high_ && modulator_phase_at_reset >= pw
    );
    next_sample += modulator_phase < pw ? 0 : 32767;
    this_sample = (this_sample - 16384) << 1;
  )
}

void Oscillator::RenderSyncTriangle(int16_t* timbre_samples, int16_t* audio_samples) {
  RENDER_MODULATED(
    SYNC(
      TRIANGLE_BIPOLAR(0) - TRIANGLE_BIPOLAR(modulator_phase_at_reset),
      break, // No edges
      false // No extra transition
    );
    (void) transition_during_reset; (void) sync_reset; (void) self_reset;
    this_sample = TRIANGLE_BIPOLAR(modulator_phase);
  )
}

void Oscillator::RenderSyncSaw(int16_t* timbre_samples, int16_t* audio_samples) {
  RENDER_MODULATED(
    SYNC(
      0 - (modulator_phase_at_reset >> 17),
      EDGES_SAW(modulator_phase, modulator_phase_increment),
      false // No extra transition
    );
    next_sample += modulator_phase >> 17;
    this_sample = (this_sample - 16384) << 1;
  )
}

void Oscillator::RenderFoldTriangle(int16_t* timbre_samples, int16_t* audio_samples) {
  RENDER_PERIODIC(
    this_sample = TRIANGLE_UNIPOLAR(phase);
    this_sample += 32768;
    this_sample = this_sample * timbre >> 15;
    this_sample = Interpolate88(ws_tri_fold, this_sample + 32768);
  )
}

void Oscillator::RenderFoldSine(int16_t* timbre_samples, int16_t* audio_samples) {
  RENDER_PERIODIC(
    this_sample = Interpolate824(wav_sine, phase);
    this_sample = this_sample * timbre >> 15;
    this_sample = Interpolate88(ws_sine_fold, this_sample + 32768);
  )
}

void Oscillator::RenderTanhSine(int16_t* timbre_samples, int16_t* audio_samples) {
  RENDER_PERIODIC(
    this_sample = Interpolate824(wav_sine, phase);
    int16_t baseline = this_sample >> 6;
    this_sample = baseline + ((this_sample - baseline) * timbre >> 15);
    this_sample = Interpolate88(ws_violent_overdrive, this_sample + 32768);
  )
}

void Oscillator::RenderExponentialSine(int16_t* timbre_samples, int16_t* audio_samples) {
  RENDER_PERIODIC(
    timbre = (timbre >> 1) + (timbre >> 2) + (timbre >> 3) + 0x0fff;
    this_sample = Interpolate824(wav_sine, phase);
    this_sample = this_sample * timbre >> 15;
    this_sample = Interpolate88(wav_sizzle, this_sample + 32768);
  )
}

void Oscillator::RenderFM(int16_t* timbre_samples, int16_t* audio_samples) {
  uint8_t fm_shape = shape_ - OSC_SHAPE_FM;
  int16_t interval = lut_fm_modulator_intervals[fm_shape];
  uint32_t modulator_phase_increment = ComputePhaseIncrement(pitch_ + interval);

  // Compensate for higher FM ratios having sweet spot at lower index
  uint8_t index_2x_upshift = lut_fm_index_2x_upshifts[fm_shape];
  uint8_t index_shift = index_2x_upshift >> 1;
  bool index_shift_halfbit = index_2x_upshift & 1;
  RENDER_MODULATED(
    modulator_phase += modulator_phase_increment;
    int16_t modulator = Interpolate824(wav_sine, modulator_phase);
    uint32_t phase_mod = modulator * timbre;
    phase_mod =
      (phase_mod << index_shift) +
      // Conditional multiplication by 1.5 to approximate sqrt(2)
      (index_shift_halfbit ? (phase_mod << (index_shift - 1)) : 0);
    this_sample = Interpolate824(wav_sine, phase + phase_mod);
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

void Oscillator::RenderPhaseDistortionPulse(int16_t* timbre_samples, int16_t* audio_samples) {
  uint8_t filter_type = shape_ - OSC_SHAPE_CZ_PULSE_LP;
  int32_t integrator = pd_square_.integrator;
  RENDER_MODULATED(
    SET_MODULATOR_PHASE_INCREMENT_FROM_TIMBRE;
    modulator_phase += modulator_phase_increment;
    if ((phase << 1) < (phase_increment << 1)) {
      pd_square_.polarity = !pd_square_.polarity;
      modulator_phase = kPhaseResetPulse[filter_type];
    }
    int32_t carrier = Interpolate824(wav_sine, modulator_phase);
    uint16_t window = ~(phase >> 15); // Double saw
    int32_t pulse = (carrier * window) >> 16;
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

void Oscillator::RenderPhaseDistortionSaw(int16_t* timbre_samples, int16_t* audio_samples) {
  uint8_t filter_type = shape_ - OSC_SHAPE_CZ_SAW_LP;
  RENDER_MODULATED(
    SET_MODULATOR_PHASE_INCREMENT_FROM_TIMBRE;
    modulator_phase += modulator_phase_increment;
    if (phase < phase_increment) {
      modulator_phase = kPhaseResetSaw[filter_type];
    }
    int32_t carrier = Interpolate824(wav_sine, modulator_phase);
    uint16_t window = ~(phase >> 16); // Saw
    int16_t output;
    if (filter_type & 2) { // Band- or high-pass
      output = (window * carrier) >> 16;
    } else {
      output = (window * (carrier + 32768) >> 16) - 32768;
    }
    this_sample = output;
  )
}

void Oscillator::RenderDiracComb(int16_t* timbre_samples, int16_t* audio_samples) {
  RENDER_PERIODIC(
    int32_t zone_14 = (pitch_ + ((32767 - timbre) >> 1));
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

void Oscillator::RenderFilteredNoise(int16_t* timbre_samples, int16_t* audio_samples) {
  StateVariableFilter svf = svf_;
  svf.RenderInit(pitch_ << 1);
  OscillatorShape shape = shape_;
  // int32_t scale = Interpolate824(lut_svf_scale, pitch_ << 18);
  // int32_t gain_correction = cutoff > scale ? scale * 32767 / cutoff : 32767;
  RENDER_CORE(
    svf.RenderSample(Random::GetSample(), timbre);
    switch (shape) {
      case OSC_SHAPE_NOISE_LP: this_sample = svf.lp; break;
      case OSC_SHAPE_NOISE_NOTCH: this_sample = svf.notch; break;
      case OSC_SHAPE_NOISE_BP: this_sample = svf.bp; break;
      case OSC_SHAPE_NOISE_HP: this_sample = svf.hp; break;
      default: break;
    }
    // CLIP(this_sample);
    // result = result * gain_correction >> 15;
    // result = Interpolate88(ws_moderate_overdrive, result + 32768);
  )
  svf_ = svf;
}

}  // namespace yarns
