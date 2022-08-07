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
#include "stmlib/utils/ring_buffer.h"

#include "yarns/interpolator.h"

#include <cstring>
#include <cstdio>

namespace yarns {

const size_t kAudioBlockSize = 64;

class StateVariableFilter {
 public:
  void Init(uint8_t interpolation_slope);
  void RenderInit(int16_t frequency, int16_t resonance);
  void RenderSample(int16_t in);
  int32_t bp, lp, notch, hp;
 private:
  Interpolator cutoff, damp;
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
  typedef void (Oscillator::*RenderFn)();

  Oscillator() { }
  ~Oscillator() { }

  inline void Init(uint16_t scale) {
    audio_buffer_.Init();
    scale_ = scale;
    timbre_.Init(64);
    gain_.Init(64);
    svf_.Init(64);
    pitch_ = 60 << 7;
    phase_ = 0;
    phase_increment_ = 1;
    high_ = false;
    next_sample_ = 0;
  }

  inline uint16_t ReadSample() {
    return audio_buffer_.ImmediateRead();
  }

  void Refresh(int16_t pitch, int16_t timbre, uint16_t gain);
  
  inline void set_shape(OscillatorShape shape) {
    shape_ = shape;
  }
  
  void Render();
  
 private:
  void RenderFilteredNoise();
  void RenderPhaseDistortionPulse();
  void RenderPhaseDistortionSaw();
  void RenderLPPulse();
  void RenderLPSaw();
  void RenderVariablePulse();
  void RenderVariableSaw();
  void RenderSawPulseMorph();
  void RenderSyncSine();
  void RenderSyncPulse();
  void RenderSyncSaw();
  void RenderFoldSine();
  void RenderFoldTriangle();
  void RenderDiracComb();
  void RenderTanhSine();
  void RenderExponentialSine();
  void RenderFM();
  
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
  Interpolator timbre_, gain_;
  int16_t pitch_;

  uint32_t phase_;
  uint32_t phase_increment_;
  uint32_t modulator_phase_;
  uint32_t modulator_phase_increment_;
  bool high_;

  StateVariableFilter svf_;
  PhaseDistortionSquareModulator pd_square_;
  
  int32_t next_sample_;
  uint16_t scale_;
  stmlib::RingBuffer<uint16_t, kAudioBlockSize * 2> audio_buffer_;
  
  static RenderFn fn_table_[];
  
  DISALLOW_COPY_AND_ASSIGN(Oscillator);
};

}  // namespace yarns

#endif // YARNS_ANALOG_OSCILLATOR_H_
