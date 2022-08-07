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
  &Oscillator::RenderSyncPulse,
  &Oscillator::RenderSyncSaw,
  &Oscillator::RenderFoldSine,
  &Oscillator::RenderFoldTriangle,
  &Oscillator::RenderDiracComb,
  &Oscillator::RenderTanhSine,
  &Oscillator::RenderExponentialSine,
  &Oscillator::RenderFM,
};

void StateVariableFilter::Init(uint8_t interpolation_slope) {
  cutoff.Init(interpolation_slope);
  damp.Init(interpolation_slope);
}

// 15-bit params
void StateVariableFilter::RenderInit(int16_t frequency, int16_t resonance) {
  cutoff.SetTarget(Interpolate824(lut_svf_cutoff, frequency << 17) >> 1);
  damp.SetTarget(Interpolate824(lut_svf_damp, resonance << 17) >> 1);
  cutoff.ComputeSlope();
  damp.ComputeSlope();
}

void StateVariableFilter::RenderSample(int16_t in) {
  cutoff.Tick();
  damp.Tick();
  notch = (in >> 1) - (bp * damp.value() >> 15);
  lp += cutoff.value() * bp >> 15;
  CONSTRAIN(lp, -16384, 16383);
  hp = notch - lp;
  bp += cutoff.value() * hp >> 15;
}

void Oscillator::Refresh(int16_t pitch, int16_t timbre, uint16_t gain) {
    pitch_ = pitch;
    // if (shape_ >= OSC_SHAPE_FM) {
    //   pitch_ += lut_fm_carrier_corrections[shape_ - OSC_SHAPE_FM];
    // }
    gain_.SetTarget((scale_ * gain) >> 17);

    int32_t strength = 32767;
    if (shape_ == OSC_SHAPE_FOLD_SINE || shape_ >= OSC_SHAPE_EXP_SINE) {
      strength -= 6 * (pitch_ - (92 << 7));
      CONSTRAIN(strength, 0, 32767);
      timbre = timbre * strength >> 15;
    } else {
      switch (shape_) {
        case OSC_SHAPE_FOLD_TRIANGLE:
          strength -= 7 * (pitch_ - (80 << 7));
          CONSTRAIN(strength, 0, 32767);
          timbre = timbre * strength >> 15;
          break;
        default:
          break;
      }
    }
    timbre_.SetTarget(timbre);
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

void Oscillator::Render() {
  if (audio_buffer_.writable() < kAudioBlockSize) return;
  
  if (pitch_ >= kHighestNote) {
    pitch_ = kHighestNote - 1;
  } else if (pitch_ < 0) {
    pitch_ = 0;
  }
  phase_increment_ = ComputePhaseIncrement(pitch_);
  
  uint8_t fn_index = shape_;
  CONSTRAIN(fn_index, 0, OSC_SHAPE_FM);
  RenderFn fn = fn_table_[fn_index];
  (this->*fn)();
}

#define SET_TIMBRE \
  int16_t timbre = timbre_.target();

#define RENDER_CORE(body) \
  int32_t next_sample = next_sample_; \
  size_t size = kAudioBlockSize; \
  while (size--) { \
    int32_t this_sample = next_sample; \
    next_sample = 0; \
    body \
    audio_buffer_.Overwrite((gain * this_sample) >> 15); \
  } \
  next_sample_ = next_sample; \

#define RENDER_WITH_PHASE_GAIN(body) \
  gain_.ComputeSlope(); \
  uint32_t phase = phase_; \
  uint32_t phase_increment = phase_increment_; \
  uint32_t modulator_phase = modulator_phase_; \
  uint32_t modulator_phase_increment = modulator_phase_increment_; \
  RENDER_CORE( \
    phase += phase_increment; \
    gain_.Tick(); \
    uint16_t gain = gain_.value(); \
    body \
  ) \
  phase_ = phase; \
  phase_increment_ = phase_increment; \
  modulator_phase_ = modulator_phase; \
  modulator_phase_increment_ = modulator_phase_increment;

#define RENDER_WITH_PHASE_GAIN_TIMBRE(body) \
  timbre_.ComputeSlope(); \
  RENDER_WITH_PHASE_GAIN( \
    timbre_.Tick(); \
    int16_t timbre = timbre_.value(); \
    body \
  )

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

#define SYNC(discontinuity_code, edges) \
  bool sync_reset = false; \
  bool self_reset = false; \
  bool transition_during_reset = false; \
  uint32_t reset_time = 0; \
  if (phase < phase_increment) { \
    sync_reset = true; \
    uint8_t master_sync_time = phase / (phase_increment >> 7); \
    reset_time = static_cast<uint32_t>(master_sync_time) << 9; \
    uint32_t reset_modulator_phase = modulator_phase + \
      (65535 - reset_time) * (modulator_phase_increment >> 16); \
    if (reset_modulator_phase < modulator_phase) { \
      transition_during_reset = true; \
    } \
    int32_t discontinuity = discontinuity_code; \
    this_sample += discontinuity * ThisBlepSample(reset_time) >> 15; \
    next_sample += discontinuity * NextBlepSample(reset_time) >> 15; \
  } \
  modulator_phase += modulator_phase_increment; \
  self_reset = modulator_phase < modulator_phase_increment; \
  while (transition_during_reset || !sync_reset) { \
    edges; \
  } \
  if (sync_reset) { \
    modulator_phase = reset_time * (modulator_phase_increment >> 16); \
    high_ = false; \
  }

#define SET_TRACKING_FILTER_CUTOFF \
  SET_TIMBRE; \
  int32_t cutoff = (pitch_ >> 1) + (timbre >> 1); \
  CONSTRAIN(cutoff, 0, 0x7fff);

void Oscillator::RenderLPPulse() {
  StateVariableFilter svf = svf_;
  SET_TRACKING_FILTER_CUTOFF;
  svf.RenderInit(cutoff, 0x7fff);
  uint32_t pw = 0x80000000;
  RENDER_WITH_PHASE_GAIN(
    bool self_reset = phase < phase_increment;
    while (true) { EDGES_PULSE(phase, phase_increment) }
    next_sample += phase < pw ? 0 : 0x7fff;
    this_sample = (this_sample - 0x4000) << 1;
    svf.RenderSample(this_sample);
    this_sample = svf.lp << 1;
  )
  svf_ = svf;
}

void Oscillator::RenderLPSaw() {
  StateVariableFilter svf = svf_;
  SET_TRACKING_FILTER_CUTOFF;
  svf.RenderInit(cutoff, 0x6000);
  RENDER_WITH_PHASE_GAIN(
    bool self_reset = phase < phase_increment;
    while (true) { EDGES_SAW(phase, phase_increment) }
    next_sample += phase >> 17;
    this_sample = (this_sample - 0x4000) << 1;
    svf.RenderSample(this_sample);
    this_sample = svf.lp << 1;
  )
  svf_ = svf;
}

void Oscillator::RenderVariablePulse() {
  RENDER_WITH_PHASE_GAIN_TIMBRE(
    timbre = timbre + (timbre >> 1); // 3/4
    uint32_t pw = (UINT16_MAX - Interpolate88(lut_env_expo, timbre)) << 15; // 50-0%
    bool self_reset = phase < phase_increment;
    while (true) { EDGES_PULSE(phase, phase_increment) }
    next_sample += phase < pw ? 0 : 0x7fff;
    this_sample = (this_sample - 0x4000) << 1;
  )
}

void Oscillator::RenderVariableSaw() {
  RENDER_WITH_PHASE_GAIN_TIMBRE(
    bool self_reset = phase < phase_increment;
    while (true) { EDGES_SAW(phase, phase_increment) }
    timbre = timbre + (timbre >> 1); // 3/4
    uint16_t saw_width = UINT16_MAX - Interpolate88(lut_env_expo, timbre); // 100-0%
    if ((phase >> 16) < saw_width) next_sample += (phase / saw_width) >> 1;
    else next_sample += 0x7fff;
    this_sample = (this_sample - 0x4000) << 1;
  )
}

// Rotates the rising edge's slope from saw to pulse
// ⟋|⟋| -> _/‾|_/‾| -> _|‾|_|‾|
void Oscillator::RenderSawPulseMorph() {
  RENDER_WITH_PHASE_GAIN_TIMBRE(
    // Prevent saw from reaching an infinitely steep rise, else we'd have to
    // clumsily transition into a BLEP of what is now a rising pulse edge
    timbre = timbre + (timbre >> 1) + (timbre >> 2) + (timbre >> 3) + (timbre >> 4); // 31/32

    // Exponential timbre curve, biased high
    uint32_t pw = Interpolate88(lut_env_expo, timbre) << 15; // 0-50%
    uint32_t saw_width = UINT32_MAX - (pw << 1); // 0-100%

    bool self_reset = phase < phase_increment;
    // BLEP falling pulse edge only
    while (self_reset) { EDGES_PULSE(phase, phase_increment) }
    if (phase < pw) next_sample += 0;
    else if (phase < pw + saw_width) next_sample += ((phase - pw) / (saw_width >> 16)) >> 1;
    else next_sample += 0x7fff;
    this_sample = (this_sample - 0x4000) << 1;
  )
}

#define SET_SYNC_INCREMENT \
  SET_TIMBRE; \
  int32_t modulator_pitch = pitch_ + (timbre >> 3); \
  CONSTRAIN(modulator_pitch, 0, kHighestNote - 1); \
  modulator_phase_increment_ = ComputePhaseIncrement(modulator_pitch);

void Oscillator::RenderSyncSine() {
  SET_SYNC_INCREMENT;
  RENDER_WITH_PHASE_GAIN(
    SYNC(
      wav_sine[0] - Interpolate824(wav_sine, reset_modulator_phase),
      break
    );
    (void) transition_during_reset; (void) sync_reset; (void) self_reset;
    next_sample += Interpolate824(wav_sine, modulator_phase);
  )
}

void Oscillator::RenderSyncPulse() {
  SET_SYNC_INCREMENT;
  uint32_t pw = 0x80000000;
  RENDER_WITH_PHASE_GAIN(
    SYNC(
      0 - reset_modulator_phase < pw ? 0 : 32767,
      EDGES_PULSE(modulator_phase, modulator_phase_increment)
    );
    next_sample += modulator_phase < pw ? 0 : 32767;
    this_sample = (this_sample - 16384) << 1;
  )
}

void Oscillator::RenderSyncSaw() {
  SET_SYNC_INCREMENT;
  RENDER_WITH_PHASE_GAIN(
    SYNC(
      0 - (reset_modulator_phase >> 17),
      EDGES_SAW(modulator_phase, modulator_phase_increment)
    );
    next_sample += modulator_phase >> 17;
    this_sample = (this_sample - 16384) << 1;
  )
}

void Oscillator::RenderFoldTriangle() {
  RENDER_WITH_PHASE_GAIN_TIMBRE(
    uint16_t phase_16 = phase >> 16;
    this_sample = (phase_16 << 1) ^ (phase_16 & 0x8000 ? 0xffff : 0x0000);
    this_sample += 32768;
    this_sample = this_sample * timbre >> 15;
    this_sample = Interpolate88(ws_tri_fold, this_sample + 32768);
  )
}

void Oscillator::RenderFoldSine() {
  RENDER_WITH_PHASE_GAIN_TIMBRE(
    this_sample = Interpolate824(wav_sine, phase);
    this_sample = this_sample * timbre >> 15;
    this_sample = Interpolate88(ws_sine_fold, this_sample + 32768);
  )
}

void Oscillator::RenderTanhSine() {
  RENDER_WITH_PHASE_GAIN_TIMBRE(
    this_sample = Interpolate824(wav_sine, phase);
    int16_t baseline = this_sample >> 6;
    this_sample = baseline + ((this_sample - baseline) * timbre >> 15);
    this_sample = Interpolate88(ws_violent_overdrive, this_sample + 32768);
  )
}

void Oscillator::RenderExponentialSine() {
  RENDER_WITH_PHASE_GAIN_TIMBRE(
    timbre = (timbre >> 1) + (timbre >> 2) + (timbre >> 3) + 0x0fff;
    this_sample = Interpolate824(wav_sine, phase);
    this_sample = this_sample * timbre >> 15;
    this_sample = Interpolate88(wav_sizzle, this_sample + 32768);
  )
}

void Oscillator::RenderFM() {
  int16_t interval = lut_fm_modulator_intervals[shape_ - OSC_SHAPE_FM];
  modulator_phase_increment_ = ComputePhaseIncrement(pitch_ + interval);
  RENDER_WITH_PHASE_GAIN_TIMBRE(
    modulator_phase += modulator_phase_increment;
    int16_t modulator = Interpolate824(wav_sine, modulator_phase);
    uint32_t phase_mod = modulator * timbre;
    // phase_mod = (phase_mod << 3) + (phase_mod << 2); // FM index 0-3
    phase_mod <<= 3; // FM index 0-2
    if (interval == 0) phase_mod <<= 1; // Double index range for 1:1 FM ratio
    this_sample = Interpolate824(wav_sine, phase + phase_mod);
  )
}

#define SET_PHASE_DISTORTION_INCREMENT \
  SET_TIMBRE; \
  int16_t timbre_offset = timbre - 2048; \
  int32_t shifted_pitch = pitch_ + (timbre_offset >> 2) + (timbre_offset >> 4) + (timbre_offset >> 8); \
  if (shifted_pitch >= kHighestNote) shifted_pitch = kHighestNote - 1; \
  modulator_phase_increment_ = ComputePhaseIncrement(shifted_pitch);

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

void Oscillator::RenderPhaseDistortionPulse() {
  SET_PHASE_DISTORTION_INCREMENT;
  uint8_t filter_type = shape_ - OSC_SHAPE_CZ_PULSE_LP;
  int32_t integrator = pd_square_.integrator;
  RENDER_WITH_PHASE_GAIN(
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

void Oscillator::RenderPhaseDistortionSaw() {
  SET_PHASE_DISTORTION_INCREMENT;
  uint8_t filter_type = shape_ - OSC_SHAPE_CZ_SAW_LP;
  RENDER_WITH_PHASE_GAIN(
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

void Oscillator::RenderDiracComb() {
  RENDER_WITH_PHASE_GAIN_TIMBRE(
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

void Oscillator::RenderFilteredNoise() {
  SET_TIMBRE;
  int32_t cutoff = 0x1000 + (timbre >> 1); // 1/4...1/2
  StateVariableFilter svf = svf_;
  svf.RenderInit(cutoff, pitch_ << 1);
  gain_.ComputeSlope();
  // int32_t scale = Interpolate824(lut_svf_scale, pitch_ << 18);
  // int32_t gain_correction = cutoff > scale ? scale * 32767 / cutoff : 32767;
  RENDER_CORE(
    gain_.Tick();
    uint16_t gain = gain_.value();
    svf.RenderSample(Random::GetSample());
    switch (shape_) {
      case OSC_SHAPE_NOISE_LP: this_sample = svf.lp; break;
      case OSC_SHAPE_NOISE_NOTCH: this_sample = svf.notch; break;
      case OSC_SHAPE_NOISE_BP: this_sample = svf.bp; break;
      case OSC_SHAPE_NOISE_HP: this_sample = svf.hp; break;
      default: break;
    }
    this_sample <<= 1;
    // CLIP(this_sample);
    // result = result * gain_correction >> 15;
    // result = Interpolate88(ws_moderate_overdrive, result + 32768);
  )
  svf_ = svf;
}

}  // namespace yarns
