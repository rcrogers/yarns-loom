// Copyright 2013 Emilie Gillet.
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
//
// -----------------------------------------------------------------------------
//
// Voice.

#include "yarns/voice.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>

#include "stmlib/midi/midi.h"
#include "stmlib/utils/dsp.h"
#include "stmlib/utils/random.h"

#include "yarns/resources.h"

namespace yarns {
  
using namespace stmlib;
using namespace stmlib_midi;

const int32_t kOctave = 12 << 7;
const int32_t kMaxNote = 120 << 7;
const int32_t kQuadrature = 0x40000000;
const uint8_t kOscillatorPWMRatioBits = 7;

void Voice::Init() {
  note_ = -1;
  note_source_ = note_target_ = note_portamento_ = 60 << 7;
  gate_ = false;
  
  mod_velocity_ = 0x7f;
  ResetAllControllers();
  
  modulation_increment_ = lut_lfo_increments[50];
  modulation_sync_ticks_ = 0;
  pitch_bend_range_ = 2;
  vibrato_range_ = 0;
  
  synced_lfo_.Init();
  envelope_.Init();
  portamento_phase_ = 0;
  portamento_phase_increment_ = 1U << 31;
  portamento_exponential_shape_ = false;
  
  trigger_duration_ = 2;
}

void CVOutput::Init(bool reset_calibration) {
  if (reset_calibration) {
    for (uint8_t i = 0; i < kNumOctaves; ++i) {
      calibrated_dac_code_[i] = 54586 - 5133 * i;
    }
  }
  dirty_ = false;
}

void CVOutput::Calibrate(uint16_t* calibrated_dac_code) {
  std::copy(
      &calibrated_dac_code[0],
      &calibrated_dac_code[kNumOctaves],
      &calibrated_dac_code_[0]);
}

inline void CVOutput::NoteToDacCode() {
  int32_t note = dc_voice_->note();
  if (note <= 0) {
    note = 0;
  }
  if (note >= kMaxNote) {
    note = kMaxNote - 1;
  }
  uint8_t octave = 0;
  while (note >= kOctave) {
    note -= kOctave;
    ++octave;
  }
  
  // Note is now between 0 and kOctave
  // Octave indicates the octave. Look up in the DAC code table.
  int32_t a = calibrated_dac_code_[octave];
  int32_t b = calibrated_dac_code_[octave + 1];
  note_dac_code_ = a + ((b - a) * note / kOctave);
}

void Voice::ResetAllControllers() {
  mod_pitch_bend_ = 8192;
  mod_wheel_ = 0;
  std::fill(&mod_aux_[0], &mod_aux_[MOD_AUX_LAST - 1], 0);
}

void Voice::set_modulation_rate(uint8_t modulation_rate, uint8_t index) {
  if (modulation_rate < LUT_LFO_INCREMENTS_SIZE) {
    modulation_increment_ = lut_lfo_increments[modulation_rate];
    modulation_increment_ *= pow(1.123f, (int) index);
    modulation_sync_ticks_ = 0;
  } else {
    modulation_increment_ = 0;
    modulation_sync_ticks_ = clock_division::list[modulation_rate - LUT_LFO_INCREMENTS_SIZE].num_ticks;
  }
}

void Voice::Refresh(uint8_t voice_index) {
  // Compute base pitch with portamento.
  portamento_phase_ += portamento_phase_increment_;
  if (portamento_phase_ < portamento_phase_increment_) {
    portamento_phase_ = 0;
    portamento_phase_increment_ = 0;
    note_source_ = note_target_;
  }
  uint16_t portamento_level = portamento_exponential_shape_
      ? Interpolate824(lut_env_expo, portamento_phase_)
      : portamento_phase_ >> 16;
  int32_t note = note_source_ + \
      ((note_target_ - note_source_) * portamento_level >> 16);

  note_portamento_ = note;
  
  // Add pitch-bend.
  note += static_cast<int32_t>(mod_pitch_bend_ - 8192) * pitch_bend_range_ >> 6;
  
  // Add transposition/fine tuning.
  note += tuning_;
  
  // Add vibrato.
  if (modulation_increment_) {
    synced_lfo_.Increment(modulation_increment_);
  } else {
    synced_lfo_.Refresh();
  }
  uint32_t lfo_phase = synced_lfo_.GetPhase() + voice_index * kQuadrature;
  int32_t lfo = synced_lfo_.Triangle(lfo_phase);
  uint16_t vibrato_level = mod_wheel_ + (vibrato_initial_ << 1);
  CONSTRAIN(vibrato_level, 0, 127);
  note += lfo * vibrato_level * vibrato_range_ >> 15;
  mod_aux_[MOD_AUX_VELOCITY] = mod_velocity_ << 9;
  mod_aux_[MOD_AUX_MODULATION] = mod_wheel_ << 9;
  mod_aux_[MOD_AUX_BEND] = static_cast<uint16_t>(mod_pitch_bend_) << 2;
  mod_aux_[MOD_AUX_VIBRATO_LFO] = (lfo * vibrato_level >> 7) + 32768;
  mod_aux_[MOD_AUX_FULL_LFO] = lfo + 32768;
  
  // Use quadrature phase for PWM LFO
  lfo = synced_lfo_.Triangle(lfo_phase + kQuadrature);
  int32_t pw_30bit = \
    // Initial range 0..1
    (oscillator_pw_initial_ << (30 - 6)) +
    // Mod range -1..1 with cubic scaling
    lfo * oscillator_pw_mod_ * oscillator_pw_mod_ * oscillator_pw_mod_;
  int32_t min_pw = 1 << (30 - kOscillatorPWMRatioBits);
  CONSTRAIN(pw_30bit, min_pw, (1 << 30) - min_pw)
  oscillator_.SetPulseWidth(pw_30bit << (32 - 30));

  if (retrigger_delay_) {
    --retrigger_delay_;
  }
  
  if (trigger_pulse_) {
    --trigger_pulse_;
  }
  
  if (trigger_phase_increment_) {
    trigger_phase_ += trigger_phase_increment_;
    if (trigger_phase_ < trigger_phase_increment_) {
      trigger_phase_ = 0;
      trigger_phase_increment_ = 0;
    }
  }

  envelope_.Render();
  mod_aux_[MOD_AUX_ENVELOPE] = scaled_envelope();

  note_ = note;
}

void CVOutput::Refresh() {
  if (has_audio()) { return; }
  int32_t note = dc_voice_->note();
  bool changed = note_ != note;
  note_ = note;
  if (changed || dirty_) {
    NoteToDacCode();
    dirty_ = false;
  }
}

void Voice::NoteOn(
    int16_t note,
    uint8_t velocity,
    uint8_t portamento,
    bool trigger) {
  note_source_ = note_portamento_;  
  note_target_ = note;
  if (!portamento) {
    note_source_ = note_target_;
  }
  portamento_phase_ = 0;
  uint32_t num_portamento_increments_per_shape = LUT_PORTAMENTO_INCREMENTS_SIZE >> 1;
  if (portamento < num_portamento_increments_per_shape) {
    portamento_phase_increment_ = lut_portamento_increments[portamento << 1];
    portamento_exponential_shape_ = true;
  } else {
    uint32_t base_increment = lut_portamento_increments[(portamento - num_portamento_increments_per_shape) << 1];
    uint32_t delta = abs(note_target_ - note_source_) + 1;
    portamento_phase_increment_ = (1536 * (base_increment >> 11) / delta) << 11;
    CONSTRAIN(portamento_phase_increment_, 1, 0x7FFFFFFF);
    portamento_exponential_shape_ = false;
  }

  mod_velocity_ = velocity;

  if (gate_ && trigger) {
    retrigger_delay_ = 3;
  }
  if (trigger) {
    trigger_pulse_ = trigger_duration_ * 8;
    trigger_phase_ = 0;
    trigger_phase_increment_ = lut_portamento_increments[trigger_duration_];
    envelope_.GateOff();
  }
  gate_ = true;
  envelope_.GateOn();
}

void Voice::NoteOff() {
  gate_ = false;
  envelope_.GateOff();
}

void Voice::ControlChange(uint8_t controller, uint8_t value) {
  switch (controller) {
    case kCCModulationWheelMsb:
      mod_wheel_ = value;
      break;
    
    case kCCBreathController:
      mod_aux_[MOD_AUX_BREATH] = value << 9;
      break;
      
    case kCCFootPedalMsb:
      mod_aux_[MOD_AUX_PEDAL] = value << 9;
      break;
  }
}

int32_t Voice::trigger_value() const {
  if (trigger_phase_ <= trigger_phase_increment_) {
    return 0;
  } else {
    int32_t velocity_coefficient = trigger_scale_ ? mod_velocity_ << 8 : 32768;
    int32_t value = 0;
    switch(trigger_shape_) {
      case TRIGGER_SHAPE_SQUARE:
        value = 32767;
        break;
      case TRIGGER_SHAPE_LINEAR:
        value = 32767 - (trigger_phase_ >> 17);
        break;
      default:
        {
          const int16_t* table = waveform_table[
              trigger_shape_ - TRIGGER_SHAPE_EXPONENTIAL];
          value = Interpolate824(table, trigger_phase_);
        }
        break;
    }
    value = value * velocity_coefficient >> 15;
    return value;
  }
}

static const uint16_t kHighestNote = 128 * 128;
static const uint16_t kPitchTableStart = 116 * 128;


void Oscillator::Init(int32_t scale, int32_t offset) {
  audio_buffer_.Init();
  phase_ = 0;
  next_sample_ = 0;
  high_ = false;
  scale_ = scale;
  offset_ = offset;
  integrator_state_ = 0;
  pulse_width_ = 0x80000000;
}

uint32_t Oscillator::ComputePhaseIncrement(int16_t midi_pitch) {
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

void Oscillator::RenderSilence() {
  size_t size = kAudioBlockSize;
  while (size--) {
    WriteSample(0, 0);
  }
}

void Oscillator::RenderSine(uint16_t gain, uint32_t phase_increment) {
  size_t size = kAudioBlockSize;
  while (size--) {
    phase_ += phase_increment;
    WriteSample(gain, Interpolate1022(wav_sine, phase_));
  }
}

void Oscillator::RenderNoise(uint16_t gain) {
  size_t size = kAudioBlockSize;
  while (size--) {
    WriteSample(gain, Random::GetSample());
  }
}

void Oscillator::RenderSaw(uint16_t gain, uint32_t phase_increment) {
  uint32_t phase = phase_;
  int32_t next_sample = next_sample_;
  size_t size = kAudioBlockSize;

  while (size--) {
    int32_t this_sample = next_sample;
    next_sample = 0;
    phase += phase_increment;
    if (phase < phase_increment) {
      uint32_t t = phase / (phase_increment >> 16);
      this_sample -= ThisBlepSample(t);
      next_sample -= NextBlepSample(t);
    }
    next_sample += phase >> 17;
    this_sample = (this_sample - 16384) << 1;
    WriteSample(gain, this_sample);
  }
  next_sample_ = next_sample;
  phase_ = phase;
}

void Oscillator::RenderSquare(
    uint16_t gain,
    uint32_t phase_increment,
    uint32_t pw,
    bool integrate) {
  uint32_t phase = phase_;
  int32_t next_sample = next_sample_;
  int32_t integrator_state = integrator_state_;
  int16_t integrator_coefficient = phase_increment >> 18;
  size_t size = kAudioBlockSize;

  while (size--) {
    int32_t this_sample = next_sample;
    next_sample = 0;
    phase += phase_increment;

    if (!high_) {
      if (phase >= pw) {
        uint32_t t = (phase - pw) / (phase_increment >> 16);
        this_sample += ThisBlepSample(t);
        next_sample += NextBlepSample(t);
        high_ = true;
      }
    }
    if (high_ && (phase < phase_increment)) {
      uint32_t t = phase / (phase_increment >> 16);
      this_sample -= ThisBlepSample(t);
      next_sample -= NextBlepSample(t);
      high_ = false;
    }
    next_sample += phase < pw ? 0 : 32767;
    this_sample = (this_sample - 16384) << 1;
    if (integrate) {
      integrator_state += integrator_coefficient * (this_sample - integrator_state) >> 15;
      this_sample = integrator_state << 3;
    }
    WriteSample(gain, this_sample);
  }
  integrator_state_ = integrator_state;
  next_sample_ = next_sample;
  phase_ = phase;
}

void Oscillator::Render(uint8_t shape, int16_t note, bool gate, uint16_t gain) {
  if (audio_buffer_.writable() < kAudioBlockSize) {
    return;
  }
  
  uint32_t phase_increment = ComputePhaseIncrement(note);
  switch (shape) {
    case OSCILLATOR_SHAPE_SAW:
      RenderSaw(gain, phase_increment);
      break;
    case OSCILLATOR_SHAPE_PULSE_VARIABLE:
      RenderSquare(gain, phase_increment, pulse_width_, false);
      break;
    case OSCILLATOR_SHAPE_PULSE_50:
      RenderSquare(gain, phase_increment, 0x80000000, false);
      break;
    case OSCILLATOR_SHAPE_TRIANGLE:
      RenderSquare(gain, phase_increment, 0x80000000, true);
      break;
    case OSCILLATOR_SHAPE_SINE:
      RenderSine(gain, phase_increment);
      break;
    case OSCILLATOR_SHAPE_NOISE:
      RenderNoise(gain);
      break;
  }
}

}  // namespace yarns