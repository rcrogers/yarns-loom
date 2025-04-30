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
// Oscillator - analog style waveforms.

#ifndef YARNS_ANALOG_OSCILLATOR_H_
#define YARNS_ANALOG_OSCILLATOR_H_

#include "stmlib/stmlib.h"

#include "yarns/envelope.h"
#include "yarns/interpolator.h"
#include "yarns/drivers/dac.h"

#include <cstring>
#include <cstdio>

namespace yarns {

class StateVariableFilter {
 public:
  void Init();
  void RenderInit(int16_t resonance);

  inline void RenderSample(int32_t in, int16_t cutoff) {
    damp.Tick();
    notch = in - (bp * damp.value() >> 14);
    CLIP(notch);
    lp += cutoff * bp >> 14;
    CLIP(lp);
    hp = notch - lp;
    CLIP(hp);
    bp += cutoff * hp >> 14;
    CLIP(bp);
  }

  int32_t bp, lp, notch, hp;
 private:
  Interpolator<kAudioBlockSizeBits> damp;
};

struct PhaseDistortionSquareModulator {
  int32_t integrator;
  bool polarity;
};

enum OscillatorShape {
  OSC_SHAPE_NOISE_NOTCH,
  OSC_SHAPE_NOISE_LP,
  OSC_SHAPE_NOISE_BP,
  OSC_SHAPE_NOISE_HP,
  OSC_SHAPE_CZ_PULSE_LP,
  OSC_SHAPE_CZ_PULSE_PK,
  OSC_SHAPE_CZ_PULSE_BP,
  OSC_SHAPE_CZ_PULSE_HP,
  OSC_SHAPE_CZ_SAW_LP,
  OSC_SHAPE_CZ_SAW_PK,
  OSC_SHAPE_CZ_SAW_BP,
  OSC_SHAPE_CZ_SAW_HP,
  OSC_SHAPE_LP_PULSE,
  OSC_SHAPE_LP_SAW,
  OSC_SHAPE_VARIABLE_PULSE,
  OSC_SHAPE_VARIABLE_SAW,
  OSC_SHAPE_SAW_PULSE_MORPH,
  OSC_SHAPE_SYNC_SINE,
  OSC_SHAPE_SYNC_PULSE,
  OSC_SHAPE_SYNC_SAW,
  OSC_SHAPE_FOLD_SINE,
  OSC_SHAPE_FOLD_TRIANGLE,
  OSC_SHAPE_DIRAC_COMB,
  OSC_SHAPE_TANH_SINE,
  OSC_SHAPE_EXP_SINE,
  OSC_SHAPE_FM,
};

class Oscillator {
 public:
  typedef void (Oscillator::*RenderFn)(int16_t* timbre_samples, int16_t* audio_samples);

  Oscillator() { }
  ~Oscillator() { }

  inline void Init(uint16_t scale) {
    scale_ = scale;
    raw_gain_bias_ = raw_timbre_bias_ = 0;
    gain_envelope_.Init(0);
    timbre_envelope_.Init(0);
    svf_.Init();
    pitch_ = 60 << 7;
    phase_ = 0;
    phase_increment_ = 1;
    high_ = false;
    next_sample_ = 0;
  }

  void Refresh(int16_t pitch, int16_t timbre_bias, uint16_t gain_bias);
  int16_t WarpTimbre(int16_t timbre) const;
  
  inline void set_shape(OscillatorShape shape) {
    shape_ = shape;
  }

  inline void NoteOn(ADSR& adsr, bool drone, int16_t raw_max_timbre) {
    gain_envelope_.NoteOn(adsr, drone ? scale_ >> 1 : 0, scale_ >> 1);
    timbre_envelope_.NoteOn(adsr, 0, WarpTimbre(raw_max_timbre));
  }
  inline void NoteOff() {
    gain_envelope_.NoteOff();
    timbre_envelope_.NoteOff();
  }
  
  void Render(int16_t* audio_mix);
  
 private:
  void RenderFilteredNoise(int16_t* timbre_samples, int16_t* audio_samples);
  void RenderPhaseDistortionPulse(int16_t* timbre_samples, int16_t* audio_samples);
  void RenderPhaseDistortionSaw(int16_t* timbre_samples, int16_t* audio_samples);
  void RenderLPPulse(int16_t* timbre_samples, int16_t* audio_samples);
  void RenderLPSaw(int16_t* timbre_samples, int16_t* audio_samples);
  void RenderVariablePulse(int16_t* timbre_samples, int16_t* audio_samples);
  void RenderVariableSaw(int16_t* timbre_samples, int16_t* audio_samples);
  void RenderSawPulseMorph(int16_t* timbre_samples, int16_t* audio_samples);
  void RenderSyncSine(int16_t* timbre_samples, int16_t* audio_samples);
  void RenderSyncPulse(int16_t* timbre_samples, int16_t* audio_samples);
  void RenderSyncSaw(int16_t* timbre_samples, int16_t* audio_samples);
  void RenderFoldSine(int16_t* timbre_samples, int16_t* audio_samples);
  void RenderFoldTriangle(int16_t* timbre_samples, int16_t* audio_samples);
  void RenderDiracComb(int16_t* timbre_samples, int16_t* audio_samples);
  void RenderTanhSine(int16_t* timbre_samples, int16_t* audio_samples);
  void RenderExponentialSine(int16_t* timbre_samples, int16_t* audio_samples);
  void RenderFM(int16_t* timbre_samples, int16_t* audio_samples);
  
  uint32_t ComputePhaseIncrement(int16_t midi_pitch) const;
  
  inline int32_t ThisBlepSample(uint32_t t) const {
    if (t > 65535) {
      t = 65535;
    }
    return t * t >> 18;
  }
  
  inline int32_t NextBlepSample(uint32_t t) const {
    if (t > 65535) {
      t = 65535;
    }
    t = 65535 - t;
    return -static_cast<int32_t>(t * t >> 18);
  }

  OscillatorShape shape_;
  Envelope gain_envelope_, timbre_envelope_;
  int16_t raw_timbre_bias_;
  uint16_t raw_gain_bias_;
  int16_t pitch_;

  uint32_t phase_;
  uint32_t phase_increment_;
  uint32_t modulator_phase_;
  bool high_;

  StateVariableFilter svf_;
  PhaseDistortionSquareModulator pd_square_;
  
  int32_t next_sample_;
  uint16_t scale_;
  
  static RenderFn fn_table_[];
  
  DISALLOW_COPY_AND_ASSIGN(Oscillator);
};

}  // namespace yarns

#endif // YARNS_ANALOG_OSCILLATOR_H_
