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

#ifndef YARNS_VOICE_H_
#define YARNS_VOICE_H_

#include "stmlib/stmlib.h"
#include "stmlib/utils/ring_buffer.h"

#include "yarns/envelope.h"
#include "yarns/analog_oscillator.h"
#include "yarns/synced_lfo.h"
#include "yarns/part.h"
#include "yarns/clock_division.h"

namespace yarns {

const uint16_t kNumOctaves = 11;

enum TriggerShape {
  TRIGGER_SHAPE_SQUARE,
  TRIGGER_SHAPE_LINEAR,
  TRIGGER_SHAPE_EXPONENTIAL,
  TRIGGER_SHAPE_RING,
  TRIGGER_SHAPE_STEPS,
  TRIGGER_SHAPE_NOISE_BURST
};

enum OscillatorMode {
  OSCILLATOR_MODE_OFF,
  OSCILLATOR_MODE_DRONE,
  OSCILLATOR_MODE_ENVELOPED,

  OSCILLATOR_MODE_LAST
};

enum ModAux {
  MOD_AUX_VELOCITY,
  MOD_AUX_MODULATION,
  MOD_AUX_AFTERTOUCH,
  MOD_AUX_BREATH,
  MOD_AUX_PEDAL,
  MOD_AUX_BEND,
  MOD_AUX_VIBRATO_LFO,
  MOD_AUX_FULL_LFO,
  MOD_AUX_ENVELOPE,

  MOD_AUX_LAST
};

class Voice {
 public:
  Voice() { }
  ~Voice() { }

  void Init();
  void ResetAllControllers();

  bool Refresh(uint8_t voice_index);
  void NoteOn(int16_t note, uint8_t velocity, uint8_t portamento, bool trigger);
  void NoteOff();
  void ControlChange(uint8_t controller, uint8_t value);
  void PitchBend(uint16_t pitch_bend) {
    mod_pitch_bend_ = pitch_bend;
  }
  void Aftertouch(uint8_t velocity) {
    mod_aux_[MOD_AUX_AFTERTOUCH] = velocity << 9;
  }

  inline void Clock() {
    if (modulation_increment_) { return; }
    synced_lfo_.Tap();
  }
  void set_modulation_rate(uint8_t modulation_rate, uint8_t index);
  inline void set_pitch_bend_range(uint8_t pitch_bend_range) {
    pitch_bend_range_ = pitch_bend_range;
  }
  inline void set_vibrato_range(uint8_t vibrato_range) {
    vibrato_range_ = vibrato_range;
  }
  inline void set_vibrato_initial(uint8_t n) {
    vibrato_initial_ = n;
  }
  inline void set_trigger_duration(uint8_t trigger_duration) {
    trigger_duration_ = trigger_duration;
  }
  inline void set_trigger_scale(uint8_t trigger_scale) {
    trigger_scale_ = trigger_scale;
  }
  inline void set_trigger_shape(uint8_t trigger_shape) {
    trigger_shape_ = trigger_shape;
  }
  inline void set_aux_cv(uint8_t aux_cv_source) {
    aux_cv_source_ = aux_cv_source;
  }
  inline void set_aux_cv_2(uint8_t aux_cv_source_2) {
    aux_cv_source_2_ = aux_cv_source_2;
  }
  
  inline int32_t note() const { return note_; }
  inline uint8_t velocity() const { return mod_velocity_; }
  inline uint8_t modulation() const { return mod_wheel_; }
  inline uint16_t aux_cv_16bit() const { return mod_aux_[aux_cv_source_]; }
  inline uint16_t aux_cv_2_16bit() const { return mod_aux_[aux_cv_source_2_]; }
  inline uint8_t aux_cv() const { return aux_cv_16bit() >> 8; }
  inline uint8_t aux_cv_2() const { return aux_cv_2_16bit() >> 8; }
  
  inline bool gate_on() const { return gate_; }

  inline bool gate() const { return gate_ && !retrigger_delay_; }
  inline bool trigger() const  {
    return gate_ && trigger_pulse_;
  }
  
  int32_t trigger_value() const;
  
  inline void set_oscillator_mode(uint8_t m) {
    oscillator_mode_ = m;
  }
  inline void set_oscillator_shape(uint8_t s) {
    oscillator_.set_shape(static_cast<AnalogOscillatorShape>(s));
  }
  inline void set_oscillator_pw_initial(uint8_t pw) {
    oscillator_pw_initial_ = pw;
  }
  inline void set_oscillator_pw_mod(int8_t pwm) {
    oscillator_pw_mod_ = pwm;
  }
  
  inline void set_tuning(int8_t coarse, int8_t fine) {
    tuning_ = (static_cast<int32_t>(coarse) << 7) + fine;
  }
  
  inline uint8_t has_audio() {
    return oscillator_mode_ != OSCILLATOR_MODE_OFF;
  }

  inline AnalogOscillator* oscillator() {
    return &oscillator_;
  }

  inline Envelope* envelope() {
    return &envelope_;
  }

  inline void set_envelope_amplitude(uint16_t a) {
    envelope_amplitude_ = a;
  }

  inline uint16_t scaled_envelope() {
    uint32_t value = envelope_.value();
    value = (value * envelope_amplitude_) >> 16;
    return value;
  }

  inline void RenderAudio() {
    if (oscillator_mode_ == OSCILLATOR_MODE_OFF) return;
    oscillator_.Render();
  }
  inline uint16_t ReadSample() {
    return oscillator_.ReadSample();
  }
  
 private:
  // Clock-synced LFO.
  SyncedLFO synced_lfo_;

  int32_t note_source_;
  int32_t note_target_;
  int32_t note_portamento_;
  int32_t note_;
  int32_t tuning_;
  bool gate_;
  
  int16_t mod_pitch_bend_;
  uint8_t mod_wheel_;
  uint16_t mod_aux_[MOD_AUX_LAST];
  uint8_t mod_velocity_;
  
  uint8_t pitch_bend_range_;
  uint32_t modulation_increment_;
  uint8_t vibrato_range_;
  uint8_t vibrato_initial_;
  
  uint8_t trigger_duration_;
  uint8_t trigger_shape_;
  bool trigger_scale_;
  uint8_t aux_cv_source_;
  uint8_t aux_cv_source_2_;
  
  uint32_t portamento_phase_;
  uint32_t portamento_phase_increment_;
  bool portamento_exponential_shape_;
  
  // This counter is used to artificially create a 750µs (3-systick) dip at LOW
  // level when the gate is currently HIGH and a new note arrive with a
  // retrigger command. This happens with note-stealing; or when sending a MIDI
  // sequence with overlapping notes.
  uint16_t retrigger_delay_;
  
  uint16_t trigger_pulse_;
  uint32_t trigger_phase_increment_;
  uint32_t trigger_phase_;
  
  uint8_t oscillator_mode_;
  uint8_t oscillator_pw_initial_;
  int8_t oscillator_pw_mod_;
  AnalogOscillator oscillator_;
  Envelope envelope_;
  uint16_t envelope_amplitude_;

  DISALLOW_COPY_AND_ASSIGN(Voice);
};

class CVOutput {
 public:
  CVOutput() { }
  ~CVOutput() { }

  void Init(bool reset_calibration);

  void Calibrate(uint16_t* calibrated_dac_code);

  inline void assign_voices(Voice* list, uint8_t num = 1) {
    num_voices_ = num;
    for (uint8_t i = 0; i < num_voices_; ++i) {
      voices_[i] = list + i;
      voices_[i]->oscillator()->Init(scale() / num_voices_, offset());
    }
  }

  inline bool gate() const {
    for (uint8_t i = 0; i < num_voices_; ++i) {
      if (voices_[i]->gate()) { return true; }
    }
    return false;
  }

  inline int32_t scale() const {
    return offset() - volts_dac_code(4);
  }

  inline int32_t offset() const {
    return volts_dac_code(0);
  }

  inline Voice* main_voice() const {
    return voices_[0];
  }

  inline bool has_audio() const {
    return main_voice()->has_audio();
  }

  inline uint16_t ReadSample() {
    uint16_t mix = 0;
    for (uint8_t i = 0; i < num_voices_; ++i) {
      mix += voices_[i]->ReadSample();
    }
    return mix;
  }

  void Refresh();

  inline uint16_t DacCodeFrom16BitValue(uint16_t value) const {
    uint32_t v = static_cast<uint32_t>(value);
    uint32_t scale = volts_dac_code(0) - volts_dac_code(7);
    return static_cast<uint16_t>(volts_dac_code(0) - (scale * v >> 16));
  }

  inline uint16_t note_dac_code() const {
    return note_dac_code_;
  }

  inline uint16_t velocity_dac_code() const {
    return DacCodeFrom16BitValue(main_voice()->velocity() << 9);
  }
  inline uint16_t modulation_dac_code() const {
    return DacCodeFrom16BitValue(main_voice()->modulation() << 9);
  }
  inline uint16_t aux_cv_dac_code() const {
    return DacCodeFrom16BitValue(main_voice()->aux_cv_16bit());
  }
  inline uint16_t aux_cv_dac_code_2() const {
    return DacCodeFrom16BitValue(main_voice()->aux_cv_2_16bit());
  }
  inline uint16_t trigger_dac_code() const {
    int32_t max = volts_dac_code(5);
    int32_t min = volts_dac_code(0);
    return min + ((max - min) * main_voice()->trigger_value() >> 15);
  }

  inline uint16_t calibration_dac_code(uint8_t note) const {
    return calibrated_dac_code_[note];
  }

  inline void set_calibration_dac_code(uint8_t note, uint16_t dac_code) {
    calibrated_dac_code_[note] = dac_code;
    dirty_ = true;
  }

  inline uint16_t volts_dac_code(uint8_t volts) const {
    return calibration_dac_code(volts + 3);
  }

  inline void RenderAudio() {
    for (uint8_t i = 0; i < num_voices_; ++i) {
      voices_[i]->RenderAudio();
    }
  }

 private:
  void NoteToDacCode();

  Voice* voices_[kNumMaxVoicesPerPart];
  uint8_t num_voices_;
  uint16_t note_dac_code_;
  bool dirty_;  // Set to true when the calibration settings have changed.
  uint16_t calibrated_dac_code_[kNumOctaves];

  DISALLOW_COPY_AND_ASSIGN(CVOutput);
};

}  // namespace yarns

#endif // YARNS_VOICE_H_
