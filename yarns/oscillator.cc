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
  // SVF LP
  &Oscillator::RenderPulse,
  &Oscillator::RenderSaw,
  // Width mod
  &Oscillator::RenderPulse,
  &Oscillator::RenderSaw,
  &Oscillator::RenderSyncSine,
  &Oscillator::RenderSyncPulse,
  &Oscillator::RenderSyncSaw,
  &Oscillator::RenderFoldSine,
  &Oscillator::RenderFoldTriangle,
  &Oscillator::RenderTanhSine,
  &Oscillator::RenderBuzz,
  &Oscillator::RenderFM,
  // &Oscillator::RenderAudioRatePWM,
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
    if (shape_ == OSC_SHAPE_FOLD_SINE || shape_ >= OSC_SHAPE_FM) {
      strength -= 6 * (pitch_ - (92 << 7));
      CONSTRAIN(strength, 0, 32767);
      timbre = timbre * strength >> 15;
    } else {
      switch (shape_) {
        case OSC_SHAPE_VARIABLE_PULSE:
          CONSTRAIN(timbre, 0, 31767);
          break;
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
    int16_t shifts_available = __builtin_clz(phase_increment) - 1;
    num_shifts = -num_shifts;
    num_shifts = std::min(shifts_available, num_shifts);
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

#define RENDER_LOOP_WITHOUT_MOD_PHASE_INCREMENT(body) \
  timbre_.ComputeSlope(); gain_.ComputeSlope(); \
  int32_t next_sample = next_sample_; \
  uint32_t phase = phase_; \
  uint32_t phase_increment = phase_increment_; \
  uint32_t modulator_phase = modulator_phase_; \
  uint32_t modulator_phase_increment = modulator_phase_increment_; \
  size_t size = kAudioBlockSize; \
  while (size--) { \
    int32_t this_sample = next_sample; \
    next_sample = 0; \
    phase += phase_increment; \
    timbre_.Tick(); gain_.Tick(); \
    body \
    audio_buffer_.Overwrite(offset_ - ((gain_.value() * this_sample) >> 15)); \
  } \
  next_sample_ = next_sample; \
  phase_ = phase; \
  phase_increment_ = phase_increment; \
  modulator_phase_ = modulator_phase; \
  modulator_phase_increment_ = modulator_phase_increment;

#define RENDER_LOOP(body) \
  RENDER_LOOP_WITHOUT_MOD_PHASE_INCREMENT( \
    modulator_phase += modulator_phase_increment; \
    body; \
  )

#define EDGES_SAW(ph, ph_incr) \
  if (!high_) { \
    if (ph < pw) break; \
    uint32_t t = (ph - pw) / (ph_incr >> 16); \
    this_sample -= ThisBlepSample(t) >> 1; \
    next_sample -= NextBlepSample(t) >> 1; \
    high_ = true; \
  } \
  if (high_) { \
    if (!self_reset) break; \
    self_reset = false; \
    uint32_t t = ph / (ph_incr >> 16); \
    this_sample -= ThisBlepSample(t) >> 1; \
    next_sample -= NextBlepSample(t) >> 1; \
    high_ = false; \
  }

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
  int32_t cutoff = (pitch_ >> 1) + (timbre_.target() >> 1); \
  CONSTRAIN(cutoff, 0, 0x7fff);

void Oscillator::RenderPulse() {
  SET_TRACKING_FILTER_CUTOFF;
  svf_.RenderInit(cutoff, 0x7fff);
  uint32_t pw = 0x80000000;
  RENDER_LOOP(
    if (shape_ == OSC_SHAPE_VARIABLE_PULSE) {
      pw = static_cast<uint32_t>(32768 - timbre_.value()) << 16;
    }
    bool self_reset = phase < phase_increment;
    while (true) { EDGES_PULSE(phase, phase_increment) }
    next_sample += phase < pw ? 0 : 32767;
    this_sample = (this_sample - 16384) << 1;
    if (shape_ == OSC_SHAPE_LP_PULSE) {
      svf_.RenderSample(this_sample);
      this_sample = svf_.lp << 1;
    }
  )
}

void Oscillator::RenderSaw() {
  SET_TRACKING_FILTER_CUTOFF;
  svf_.RenderInit(cutoff, 0x6000);
  uint32_t pw = 0;
  RENDER_LOOP(
    if (shape_ == OSC_SHAPE_VARIABLE_SAW) {
      pw = static_cast<uint32_t>(timbre_.value()) << 16;
    }
    bool self_reset = phase < phase_increment;
    while (true) { EDGES_SAW(phase, phase_increment) }
    next_sample += phase >> 18;
    next_sample += (phase - pw) >> 18;
    this_sample = (this_sample - 16384) << 1;
    if (shape_ == OSC_SHAPE_LP_SAW) {
      svf_.RenderSample(this_sample);
      this_sample = svf_.lp << 1;
    }
  )
}

#define SET_SYNC_INCREMENT \
  int32_t modulator_pitch = pitch_ + (timbre_.target() >> 3); \
  CONSTRAIN(modulator_pitch, 0, 16383); \
  modulator_phase_increment_ = ComputePhaseIncrement(modulator_pitch);

void Oscillator::RenderSyncSine() {
  SET_SYNC_INCREMENT;
  RENDER_LOOP_WITHOUT_MOD_PHASE_INCREMENT(
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
  RENDER_LOOP_WITHOUT_MOD_PHASE_INCREMENT(
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
  uint32_t pw = 0;
  RENDER_LOOP_WITHOUT_MOD_PHASE_INCREMENT(
    SYNC(
      0 - (
        (reset_modulator_phase >> 18) + ((reset_modulator_phase - pw) >> 18)
      ),
      EDGES_SAW(modulator_phase, modulator_phase_increment)
    );
    next_sample += modulator_phase >> 18;
    next_sample += (modulator_phase - pw) >> 18;
    this_sample = (this_sample - 16384) << 1;
  )
}

void Oscillator::RenderFoldTriangle() {
  RENDER_LOOP(
    uint16_t phase_16 = phase >> 16;
    this_sample = (phase_16 << 1) ^ (phase_16 & 0x8000 ? 0xffff : 0x0000);
    this_sample += 32768;
    this_sample = this_sample * timbre_.value() >> 15;
    this_sample = Interpolate88(ws_tri_fold, this_sample + 32768);
  )
}

void Oscillator::RenderFoldSine() {
  RENDER_LOOP(
    this_sample = Interpolate824(wav_sine, phase);
    this_sample = this_sample * timbre_.value() >> 15;
    this_sample = Interpolate88(ws_sine_fold, this_sample + 32768);
  )
}

void Oscillator::RenderTanhSine() {
  RENDER_LOOP(
    this_sample = Interpolate824(wav_sine, phase);
    int16_t baseline = this_sample >> 6;
    this_sample = baseline + ((this_sample - baseline) * timbre_.value() >> 15);
    this_sample = Interpolate88(ws_violent_overdrive, this_sample + 32768);
  )
}

void Oscillator::RenderFM() {
  int16_t interval = lut_fm_modulator_intervals[shape_ - OSC_SHAPE_FM];
  modulator_phase_increment_ = ComputePhaseIncrement(pitch_ + interval);
  RENDER_LOOP(
    int16_t modulator = Interpolate824(wav_sine, modulator_phase);
    uint32_t phase_mod = modulator * timbre_.value();
    phase_mod = (phase_mod << 3) + (phase_mod << 2); // FM index 0-3
    this_sample = Interpolate824(wav_sine, phase + phase_mod);
  )
}

#define SET_PHASE_DISTORTION_INCREMENT \
  int16_t timbre_offset = timbre_.target() - 2048; \
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
  RENDER_LOOP(
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
  RENDER_LOOP(
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

void Oscillator::RenderBuzz() {
  RENDER_LOOP(
    int32_t zone_14 = (pitch_ + ((32767 - timbre_.value()) >> 1));
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
  int32_t cutoff = 0x1000 + (timbre_.target() >> 1); // 1/4...1/2
  svf_.RenderInit(cutoff, pitch_ << 1);
  // int32_t scale = Interpolate824(lut_svf_scale, pitch_ << 18);
  // int32_t gain_correction = cutoff > scale ? scale * 32767 / cutoff : 32767;
  RENDER_LOOP(
    svf_.RenderSample(Random::GetSample());
    switch (shape_) {
      case OSC_SHAPE_NOISE_LP: this_sample = svf_.lp; break;
      case OSC_SHAPE_NOISE_NOTCH: this_sample = svf_.notch; break;
      case OSC_SHAPE_NOISE_BP: this_sample = svf_.bp; break;
      case OSC_SHAPE_NOISE_HP: this_sample = svf_.hp; break;
      default: break;
    }
    this_sample <<= 1;
    // CLIP(this_sample);
    // result = result * gain_correction >> 15;
    // result = Interpolate88(ws_moderate_overdrive, result + 32768);
  )
}

}  // namespace yarns
