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

#include <cmath>

#include "stmlib/midi/midi.h"
#include "stmlib/utils/dsp.h"
#include "stmlib/utils/random.h"
#include "stmlib/dsp/dsp.h"

#include "yarns/resources.h"
#include "yarns/multi.h"

namespace yarns {
  
using namespace stmlib;
using namespace stmlib_midi;

const int32_t kOctave = 12 << 7;
const int32_t kMaxNote = 120 << 7;
const int32_t kQuadrature = 0x40000000;

void Voice::Init() {
  audio_output_ = NULL;
  note_ = -1;
  note_source_ = note_target_ = note_portamento_ = 60 << 7;
  gate_ = false;
  
  mod_velocity_ = 0x7f;
  ResetAllControllers();
  
  for (uint8_t i = 0; i < LFO_ROLE_LAST; i++) {
    lfos_[i].SetPhase(0);
    lfos_[i].SetPhaseIncrement(lut_lfo_increments[50]);
  }
  pitch_bend_range_ = 2;
  vibrato_range_ = 0;

  tremolo_mod_current_ = 0;
  timbre_mod_lfo_current_ = 0;
  timbre_init_current_ = 0;

  refresh_counter_ = 0;
  pitch_lfo_interpolator_.Init();
  timbre_lfo_interpolator_.Init();
  amplitude_lfo_interpolator_.Init();
  scaled_vibrato_lfo_interpolator_.Init();
  
  portamento_phase_ = 0;
  portamento_phase_increment_ = 1U << 31;
  portamento_exponential_shape_ = false;

  trigger_duration_ = 2;
}

/* static */
CVOutput::DCFn CVOutput::dc_fn_table_[] = {
  &CVOutput::pitch_dac_code,
  &CVOutput::velocity_dac_code,
  &CVOutput::aux_cv_dac_code,
  &CVOutput::aux_cv_dac_code_2,
  &CVOutput::trigger_dac_code,
};

void CVOutput::Init(bool reset_calibration) {
  if (reset_calibration) {
    for (uint8_t i = 0; i < kNumOctaves; ++i) {
      calibrated_dac_code_[i] = 54586 - 5133 * i;
    }
  }
  dirty_ = false;
  dc_role_ = DC_PITCH;
  envelope_.Init();
  dac_buffer_.Init();
  tremolo_.Init();
}

void CVOutput::Calibrate(uint16_t* calibrated_dac_code) {
  std::copy(
      &calibrated_dac_code[0],
      &calibrated_dac_code[kNumOctaves],
      &calibrated_dac_code_[0]);
}

uint16_t CVOutput::pitch_dac_code() {
  int32_t note = dc_voice_->note();
  if (dirty_ || note_ != note) dac_code_ = NoteToDacCode(note);
  dirty_ = false;
  note_ = note;
  return dac_code_;
}

uint16_t CVOutput::NoteToDacCode(int32_t note) const {
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
  return a + ((b - a) * note / kOctave);
}

void Voice::ResetAllControllers() {
  mod_pitch_bend_ = 8192;
  vibrato_mod_ = 0;
  std::fill(&mod_aux_[0], &mod_aux_[MOD_AUX_LAST - 1], 0);
}

void Voice::garbage(uint8_t x) {
  uint32_t foo = pow(1.123f, (int) x);
  (void) foo;
}

void Voice::Refresh() {
  if (retrigger_delay_) {
    --retrigger_delay_;
  }  
  if (trigger_pulse_) {
    --trigger_pulse_;
  }
  if (!has_cv_output()) return;

  // Slew coarse inputs to avoid clicks
  tremolo_mod_current_ = stmlib::slew(
    tremolo_mod_current_, tremolo_mod_target_);
  timbre_init_current_ = stmlib::slew(
    timbre_init_current_, timbre_init_target_);
  timbre_mod_lfo_current_ = stmlib::slew(
    timbre_mod_lfo_current_, timbre_mod_lfo_target_);

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
  
  // Render modulation sources
  for (uint8_t i = 0; i < LFO_ROLE_LAST; i++) {
    lfos_[i].Refresh();
  }
  int32_t vibrato_lfo = lfo_value(LFO_ROLE_PITCH);

  if (refresh_counter_ == 0) {
    uint16_t tremolo_lfo = 32767 - lfo_value(LFO_ROLE_AMPLITUDE);
    uint16_t scaled_tremolo_lfo = tremolo_lfo * tremolo_mod_current_ >> 16;
    amplitude_lfo_interpolator_.SetTarget(scaled_tremolo_lfo >> 1);
    amplitude_lfo_interpolator_.ComputeSlope();

    int32_t timbre_lfo_15 = lfo_value(LFO_ROLE_TIMBRE) * timbre_mod_lfo_current_ >> (31 - 15);
    timbre_lfo_interpolator_.SetTarget(timbre_lfo_15);
    timbre_lfo_interpolator_.ComputeSlope();

    scaled_vibrato_lfo_interpolator_.SetTarget(vibrato_lfo * vibrato_mod_ >> 8);
    scaled_vibrato_lfo_interpolator_.ComputeSlope();
    int32_t pitch_lfo_15 = scaled_vibrato_lfo_interpolator_.target() * vibrato_range_ >> 8;
    pitch_lfo_interpolator_.SetTarget(pitch_lfo_15);
    pitch_lfo_interpolator_.ComputeSlope();
  }
  refresh_counter_ = (refresh_counter_ + 1) % (1 << kLowFreqRefreshBits);

  pitch_lfo_interpolator_.Tick();
  timbre_lfo_interpolator_.Tick();
  amplitude_lfo_interpolator_.Tick();
  scaled_vibrato_lfo_interpolator_.Tick();

  note += pitch_lfo_interpolator_.value();

  int32_t timbre_15 =
    (timbre_init_current_ >> (16 - 15)) +
    timbre_lfo_interpolator_.value();
  CONSTRAIN(timbre_15, 0, (1 << 15) - 1);

  uint16_t tremolo = amplitude_lfo_interpolator_.value() << 1;

  // Needed for LED display of envelope CV
  if (aux_1_envelope()) {
    mod_aux_[MOD_AUX_ENVELOPE] = dc_output(DC_AUX_1)->RefreshEnvelope(tremolo);
  }
  if (aux_2_envelope()) {
    mod_aux_[MOD_AUX_ENVELOPE] = dc_output(DC_AUX_2)->RefreshEnvelope(tremolo);
  }

  oscillator_.Refresh(note, timbre_15, tremolo);
  // TODO with square tremolo, changes in the envelope could outpace this and cause sound to leak through?

  mod_aux_[MOD_AUX_VELOCITY] = mod_velocity_ << 9;
  mod_aux_[MOD_AUX_MODULATION] = vibrato_mod_ << 9;
  mod_aux_[MOD_AUX_BEND] = static_cast<uint16_t>(mod_pitch_bend_) << 2;
  mod_aux_[MOD_AUX_VIBRATO_LFO] = (scaled_vibrato_lfo_interpolator_.value() << 1) + 32768;
  mod_aux_[MOD_AUX_FULL_LFO] = vibrato_lfo + 32768;
  
  if (trigger_phase_increment_) {
    trigger_phase_ += trigger_phase_increment_;
    if (trigger_phase_ < trigger_phase_increment_) {
      trigger_phase_ = 0;
      trigger_phase_increment_ = 0;
    }
  }

  note_ = note;
}

void CVOutput::Refresh() {
  if (is_audio() || is_envelope()) return;
  dac_code_ = (this->*dc_fn_table_[dc_role_])();
}

void CVOutput::RenderSamples() {
  if (dac_buffer_.writable() < kAudioBlockSize) return;

  if (is_envelope()) {
    size_t size = kAudioBlockSize;
    while (size--) {
      tremolo_.Tick();
      envelope_.Tick();
      int32_t value = (tremolo_.value() + envelope_.value()) << 1;
      value = stmlib::ClipU16(value);
      dac_buffer_.Overwrite(value);
    }
  } else if (is_audio()) {
    std::fill(
        dac_buffer_.write_ptr(),
        dac_buffer_.write_ptr() + kAudioBlockSize,
        zero_dac_code_
    );
    for (uint8_t v = 0; v < num_audio_voices_; ++v) {
      audio_voices_[v]->oscillator()->Render();
      q15_add<kAudioBlockSize, false>(
          audio_voices_[v]->oscillator()->audio_buffer.read_ptr(),
          dac_buffer_.write_ptr(),
          dac_buffer_.write_ptr()
      );
    }
    dac_buffer_.advance_write_ptr(kAudioBlockSize);

    // Lowest max seen on FM 1/1: 1.63E4
    static uint32_t debug_count = 0;
    if (debug_count % (1 << 12) == 0) {
      // Get the biggest sample-to-sample delta in dac_buffer_ and print it
      int32_t largest_delta = 0;
      for (uint8_t i = 0; i < kAudioBlockSize - 1; ++i) {
        int32_t delta = abs(dac_buffer_.read_ptr()[i] - dac_buffer_.read_ptr()[i + 1]);
        if (abs(delta) > abs(largest_delta)) {
          largest_delta = delta;
        }
      }
      multi.PrintInt32E(largest_delta);
    }
    ++debug_count;
  }
}

void Voice::NoteOn(
  int16_t note, uint8_t velocity, uint8_t portamento, bool trigger,
  ADSR& adsr, int16_t timbre_envelope_target
) {
  if (gate_ && trigger) {
    retrigger_delay_ = 3;
  }
  if (trigger) {
    trigger_pulse_ = trigger_duration_ * 2;
    trigger_phase_ = 0;
    trigger_phase_increment_ = lut_portamento_increments[trigger_duration_];
    NoteOff();
  }
  gate_ = true;
  adsr_ = adsr;
  oscillator_.NoteOn(adsr_, oscillator_mode_ == OSCILLATOR_MODE_DRONE, timbre_envelope_target);
  if (aux_1_envelope()) dc_output(DC_AUX_1)->NoteOn(adsr_);
  if (aux_2_envelope()) dc_output(DC_AUX_2)->NoteOn(adsr_);

  if (!has_cv_output()) return;

  note_source_ = note_portamento_;  
  note_target_ = note;
  if (!portamento) {
    note_source_ = note_target_;
  }

  portamento_phase_ = 0;
  uint32_t split_point = LUT_PORTAMENTO_INCREMENTS_SIZE >> 1;
  if (portamento < split_point) {
    portamento_phase_increment_ = lut_portamento_increments[(split_point - portamento) << 1];
    portamento_exponential_shape_ = true;
  } else {
    uint32_t base_increment = lut_portamento_increments[(portamento - split_point) << 1];
    uint32_t delta = abs(note_target_ - note_source_) + 1;
    portamento_phase_increment_ = (1536 * (base_increment >> 11) / delta) << 11;
    CONSTRAIN(portamento_phase_increment_, 1, 0x7FFFFFFF);
    portamento_exponential_shape_ = false;
  }

  mod_velocity_ = velocity;
}

void Voice::NoteOff() {
  gate_ = false;
  oscillator_.NoteOff();
  if (aux_1_envelope()) dc_output(DC_AUX_1)->NoteOff();
  if (aux_2_envelope()) dc_output(DC_AUX_2)->NoteOff();
}

void Voice::ControlChange(uint8_t controller, uint8_t value) {
  switch (controller) {
    case kCCBreathController:
      mod_aux_[MOD_AUX_BREATH] = value << 9;
      break;
      
    case kCCFootPedalMsb:
      mod_aux_[MOD_AUX_PEDAL] = value << 9;
      break;
  }
}

uint16_t Voice::trigger_value() const {
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

}  // namespace yarns