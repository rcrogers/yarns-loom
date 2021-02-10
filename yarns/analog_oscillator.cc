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

#include "yarns/analog_oscillator.h"

#include "stmlib/utils/dsp.h"
#include "stmlib/utils/random.h"

#include "yarns/resources.h"

namespace yarns {

using namespace stmlib;

static const size_t kNumZones = 15;

static const uint16_t kHighestNote = 128 * 128;
static const uint16_t kPitchTableStart = 116 * 128;
static const uint16_t kOctave = 12 * 128;

uint32_t AnalogOscillator::ComputePhaseIncrement(int16_t midi_pitch) const {
  if (midi_pitch >= kHighestNote) {
    midi_pitch = kHighestNote - 1;
  }
  
  int32_t ref_pitch = midi_pitch;
  ref_pitch -= kPitchTableStart;
  
  size_t num_shifts = 0;
  while (ref_pitch < 0) {
    ref_pitch += kOctave;
    ++num_shifts;
  }
  
  uint32_t a = lut_oscillator_increments[ref_pitch >> 4];
  uint32_t b = lut_oscillator_increments[(ref_pitch >> 4) + 1];
  uint32_t phase_increment = a + \
      (static_cast<int32_t>(b - a) * (ref_pitch & 0xf) >> 4);
  phase_increment >>= num_shifts;
  return phase_increment;
}

void AnalogOscillator::Render() {
  RenderFn fn = fn_table_[shape_];
  
  if (shape_ != previous_shape_) {
    OnShapeChange();
    previous_shape_ = shape_;
  }

  if (audio_buffer_.writable() < kAudioBlockSize) return;
  phase_increment_ = ComputePhaseIncrement(pitch_);
  
  if (pitch_ > kHighestNote) {
    pitch_ = kHighestNote;
  } else if (pitch_ < 0) {
    pitch_ = 0;
  }
  
  (this->*fn)();
}

void AnalogOscillator::RenderCSaw() {
  size_t size = kAudioBlockSize;
  int32_t next_sample = next_sample_;
  while (size--) {
    bool self_reset = false;
    uint32_t pw = static_cast<uint32_t>(parameter_) * 49152;
    if (pw < (phase_increment_ << 3)) {
      pw = phase_increment_ << 3;
    }
    
    int32_t this_sample = next_sample;
    next_sample = 0;

    phase_ += phase_increment_;
    if (phase_ < phase_increment_) {
      self_reset = true;
    }
    
    int16_t shift = -(aux_parameter_ - 32767) >> 4;
    while (true) {
      if (!high_) {
        if (phase_ < pw) {
          break;
        }
        uint32_t t = (phase_ - pw) / (phase_increment_ >> 16);
        int16_t before = discontinuity_depth_;
        int16_t after = phase_ >> 18;
        int16_t discontinuity = after - before;
        this_sample += discontinuity * ThisBlepSample(t) >> 15;
        next_sample += discontinuity * NextBlepSample(t) >> 15;
        high_ = true;
      }
      if (high_) {
        if (!self_reset) {
          break;
        }
        self_reset = false;
        discontinuity_depth_ = -2048 + (aux_parameter_ >> 2);
        uint32_t t = phase_ / (phase_increment_ >> 16);
        int16_t before = 16383;
        int16_t after = discontinuity_depth_;
        int16_t discontinuity = after - before;
        this_sample += discontinuity * ThisBlepSample(t) >> 15;
        next_sample += discontinuity * NextBlepSample(t) >> 15;
        high_ = false;
      }
    }

    next_sample += phase_ < pw
        ? discontinuity_depth_
        : phase_ >> 18;
    WriteSample(((((this_sample + shift) * 13) >> 3) - 8192) << 1);
  }
  next_sample_ = next_sample;
}

void AnalogOscillator::RenderSquare() {
  size_t size = kAudioBlockSize;
  if (parameter_ > 30000) {
    parameter_ = 30000;
  }
  
  int32_t next_sample = next_sample_;
  while (size--) {
    bool self_reset = false;

    uint32_t pw = static_cast<uint32_t>(32768 - parameter_) << 16;
    
    int32_t this_sample = next_sample;
    next_sample = 0;
    
    phase_ += phase_increment_;
    if (phase_ < phase_increment_) {
      self_reset = true;
    }
    
    while (true) {
      if (!high_) {
        if (phase_ < pw) {
          break;
        }
        uint32_t t = (phase_ - pw) / (phase_increment_ >> 16);
        this_sample += ThisBlepSample(t);
        next_sample += NextBlepSample(t);
        high_ = true;
      }
      if (high_) {
        if (!self_reset) {
          break;
        }
        self_reset = false;
        uint32_t t = phase_ / (phase_increment_ >> 16);
        this_sample -= ThisBlepSample(t);
        next_sample -= NextBlepSample(t);
        high_ = false;
      }
    }
    
    next_sample += phase_ < pw ? 0 : 32767;
    WriteSample((this_sample - 16384) << 1);
  }
  next_sample_ = next_sample;
}

void AnalogOscillator::RenderSawSquareMorph() {
  uint32_t pw = 0x80000000;
  int32_t next_sample = next_sample_;
  size_t size = kAudioBlockSize;
  while (size--) {
    bool self_reset = false;
    int32_t this_sample = next_sample;
    next_sample = 0;
    phase_ += phase_increment_;
    if (phase_ < phase_increment_) {
      self_reset = true;
    }
    while (true) {
      if (!high_) {
        if (phase_ < pw) {
          break;
        }
        uint32_t t = (phase_ - pw) / (phase_increment_ >> 16);
        this_sample -= ThisBlepSample(t) >> 2; // TODO halving for the square BLEP is naive, this should be weighted in some fashion?
        next_sample -= NextBlepSample(t) >> 2;
        high_ = true;
      }
      if (high_) {
        if (!self_reset) {
          break;
        }
        self_reset = false;
        uint32_t t = phase_ / (phase_increment_ >> 16);
        this_sample -= ThisBlepSample(t) >> 1;
        next_sample -= NextBlepSample(t) >> 1;
        high_ = false;
      }
    }
    int16_t saw = phase_ >> 17;
    int16_t square = phase_ < pw ? 0 : 32767;
    next_sample += Mix(saw, square, parameter_ << 1);
    WriteSample((this_sample - 16384) << 1);
  }
  next_sample_ = next_sample;
}

void AnalogOscillator::RenderVariableSaw() {
  size_t size = kAudioBlockSize;
  int32_t next_sample = next_sample_;
  if (parameter_ < 1024) {
    parameter_ = 1024;
  }
  while (size--) {
    bool self_reset = false;

    uint32_t pw = static_cast<uint32_t>(parameter_) << 16;

    int32_t this_sample = next_sample;
    next_sample = 0;

    phase_ += phase_increment_;
    if (phase_ < phase_increment_) {
      self_reset = true;
    }

    while (true) {
      if (!high_) {
        if (phase_ < pw) {
          break;
        }
        uint32_t t = (phase_ - pw) / (phase_increment_ >> 16);
        this_sample -= ThisBlepSample(t) >> 1;
        next_sample -= NextBlepSample(t) >> 1;
        high_ = true;
      }
      if (high_) {
        if (!self_reset) {
          break;
        }
        self_reset = false;
        uint32_t t = phase_ / (phase_increment_ >> 16);
        this_sample -= ThisBlepSample(t) >> 1;
        next_sample -= NextBlepSample(t) >> 1;
        high_ = false;
      }
    }
    
    next_sample += phase_ >> 18;
    next_sample += (phase_ - pw) >> 18;
    WriteSample((this_sample - 16384) << 1);
  }
  next_sample_ = next_sample;
}

void AnalogOscillator::RenderTriangleFold() {
  uint32_t phase = phase_;
  int16_t fold_gain = 2048 + (parameter_ * 30720 >> 15);

  size_t size = kAudioBlockSize;
  while (size--) {
    uint16_t phase_16;
    int16_t triangle;
    int16_t sample;
    
    // 2x oversampled WF.
    phase += phase_increment_; // >> 1;
    phase_16 = phase >> 16;
    triangle = (phase_16 << 1) ^ (phase_16 & 0x8000 ? 0xffff : 0x0000);
    triangle += 32768;
    triangle = triangle * fold_gain >> 15;
    triangle = Interpolate88(ws_tri_fold, triangle + 32768);
    sample = triangle;// >> 1;
    
    /*
    phase += phase_increment_ >> 1;
    phase_16 = phase >> 16;
    triangle = (phase_16 << 1) ^ (phase_16 & 0x8000 ? 0xffff : 0x0000);
    triangle += 32768;
    triangle = triangle * fold_gain >> 15;
    triangle = Interpolate88(ws_tri_fold, triangle + 32768);
    sample += triangle >> 1;
    */

    WriteSample(sample);
  }
    
  phase_ = phase;
}

void AnalogOscillator::RenderSineFold() {
  uint32_t phase = phase_;
  int16_t fold_gain = 2048 + (parameter_ * 30720 >> 15);
  
  size_t size = kAudioBlockSize;
  while (size--) {
    int16_t sine;
    int16_t sample;
    
    // 2x oversampled WF.
    phase += phase_increment_; // >> 1;
    sine = Interpolate824(wav_sine, phase);
    sine = sine * fold_gain >> 15;
    sine = Interpolate88(ws_sine_fold, sine + 32768);
    sample = sine; // >> 1;
    
    /*
    phase += phase_increment_ >> 1;
    sine = Interpolate824(wav_sine, phase);
    sine = sine * fold_gain >> 15;
    sine = Interpolate88(ws_sine_fold, sine + 32768);
    sample += sine >> 1;
    */

    WriteSample(sample);
  }

  phase_ = phase;
}

void AnalogOscillator::RenderBuzz() {
  int32_t shifted_pitch = pitch_ + ((32767 - parameter_) >> 1);
  uint16_t crossfade = shifted_pitch << 6;
  size_t index = (shifted_pitch >> 10);
  if (index >= kNumZones) {
    index = kNumZones - 1;
  }
  const int16_t* wave_1 = waveform_table[WAV_BANDLIMITED_COMB_0 + index];
  index += 1;
  if (index >= kNumZones) {
    index = kNumZones - 1;
  }
  const int16_t* wave_2 = waveform_table[WAV_BANDLIMITED_COMB_0 + index];
  size_t size = kAudioBlockSize;
  while (size--) {
    phase_ += phase_increment_;
    WriteSample(Crossfade(wave_1, wave_2, phase_, crossfade));
  }
}

void AnalogOscillator::RenderNoise() {
  size_t size = kAudioBlockSize;
  while (size--) {
    WriteSample(Random::GetSample());
  }
}

/* static */
AnalogOscillator::RenderFn AnalogOscillator::fn_table_[] = {
  &AnalogOscillator::RenderVariableSaw,
  &AnalogOscillator::RenderCSaw,
  &AnalogOscillator::RenderSawSquareMorph,
  &AnalogOscillator::RenderSquare,
  &AnalogOscillator::RenderTriangleFold,
  &AnalogOscillator::RenderSineFold,
  &AnalogOscillator::RenderBuzz,
  &AnalogOscillator::RenderNoise,
};

}  // namespace yarns
