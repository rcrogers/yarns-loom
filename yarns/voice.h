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
#include "yarns/synced_lfo.h"
#include "yarns/part.h"
#include "yarns/clock_division.h"

namespace yarns {

const uint16_t kNumOctaves = 11;
const size_t kAudioBlockSize = 64;

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

enum OscillatorShape {
  OSCILLATOR_SHAPE_SAW,
  OSCILLATOR_SHAPE_PULSE_VARIABLE,
  OSCILLATOR_SHAPE_PULSE_50,
  OSCILLATOR_SHAPE_TRIANGLE,
  OSCILLATOR_SHAPE_SINE,
  OSCILLATOR_SHAPE_NOISE,

  OSCILLATOR_SHAPE_LAST
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

class Oscillator {
 public:
  Oscillator() { }
  ~Oscillator() { }
  void Init(int32_t scale, int32_t offset);
  void Render(uint8_t shape, int16_t note, bool gate, uint16_t gain);
  inline uint16_t ReadSample() {
    return audio_buffer_.ImmediateRead();
  }
  inline void SetPulseWidth(uint32_t pw) { pulse_width_ = pw; }

 private:
  uint32_t ComputePhaseIncrement(int16_t pitch);
  
  void RenderSilence();
  void RenderNoise(uint16_t gain);
  void RenderSine(uint16_t gain, uint32_t phase_increment);
  void RenderSaw(uint16_t gain, uint32_t phase_increment);
  void RenderSquare(uint16_t gain, uint32_t phase_increment, uint32_t pw, bool integrate);

  inline void WriteSample(uint16_t gain, int16_t sample) {
    int32_t amplitude = (scale_ * gain) >> 16;
    audio_buffer_.Overwrite(offset_ - ((amplitude * sample) >> 16));
  }

  inline int32_t ThisBlepSample(uint32_t t) {
    if (t > 65535) {
      t = 65535;
    }
    return t * t >> 18;
  }
  
  inline int32_t NextBlepSample(uint32_t t) {
    if (t > 65535) {
      t = 65535;
    }
    t = 65535 - t;
    return -static_cast<int32_t>(t * t >> 18);
  }
  
  int32_t scale_;
  int32_t offset_;
  uint32_t phase_;
  int32_t next_sample_;
  int32_t integrator_state_;
  uint32_t pulse_width_;
  bool high_;
  stmlib::RingBuffer<uint16_t, kAudioBlockSize * 2> audio_buffer_;
  
  DISALLOW_COPY_AND_ASSIGN(Oscillator);
};

class Voice {
 public:
  Voice() { }
  ~Voice() { }

  void Init();
  void ResetAllControllers();

  void Refresh(uint8_t voice_index);
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
    if (!modulation_sync_ticks_) { return; }
    synced_lfo_.Tap(modulation_sync_ticks_);
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
  inline void set_oscillator_shape(uint8_t oscillator_shape) {
    oscillator_shape_ = oscillator_shape;
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
  
  inline bool has_audio() {
    return oscillator_mode_ != OSCILLATOR_MODE_OFF;
  }

  inline Oscillator* oscillator() {
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
    uint16_t gain;
    switch (oscillator_mode_) {
      case OSCILLATOR_MODE_DRONE:
        gain = UINT16_MAX;
        break;
      case OSCILLATOR_MODE_ENVELOPED:
        gain = scaled_envelope();
        break;
      case OSCILLATOR_MODE_OFF:
      default:
        return;
    }
    oscillator_.Render(oscillator_shape_, note_, gate_, gain);
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
  uint16_t modulation_sync_ticks_;
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
  uint8_t oscillator_shape_;
  uint8_t oscillator_pw_initial_;
  int8_t oscillator_pw_mod_;
  Oscillator oscillator_;
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

  inline void assign(Voice* dc, uint8_t num_audio) {
    dc_voice_ = dc;
    num_audio_voices_ = num_audio;
    for (uint8_t i = 0; i < num_audio_voices_; ++i) {
      Voice* audio_voice = audio_voices_[i] = dc_voice_ + i;
      audio_voice->oscillator()->Init(scale() / num_audio_voices_, offset());
    }
  }

  inline bool gate() const {
    if (has_audio()) {
      for (uint8_t i = 0; i < num_audio_voices_; ++i) {
        if (audio_voices_[i]->gate()) { return true; }
      }
      return false;
    } else {
      return dc_voice_->gate();
    }
  }

  inline int32_t scale() const {
    return offset() - volts_dac_code(4);
  }

  inline int32_t offset() const {
    return volts_dac_code(0);
  }

  inline bool has_audio() const {
    return num_audio_voices_ > 0 && audio_voices_[0]->has_audio();
  }

  inline void RenderAudio() {
    for (uint8_t i = 0; i < num_audio_voices_; ++i) {
      audio_voices_[i]->RenderAudio();
    }
  }

  inline uint16_t ReadSample() {
    uint16_t mix = 0;
    for (uint8_t i = 0; i < num_audio_voices_; ++i) {
      mix += audio_voices_[i]->ReadSample();
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
    return DacCodeFrom16BitValue(dc_voice_->velocity() << 9);
  }
  inline uint16_t modulation_dac_code() const {
    return DacCodeFrom16BitValue(dc_voice_->modulation() << 9);
  }
  inline uint16_t aux_cv_dac_code() const {
    return DacCodeFrom16BitValue(dc_voice_->aux_cv_16bit());
  }
  inline uint16_t aux_cv_dac_code_2() const {
    return DacCodeFrom16BitValue(dc_voice_->aux_cv_2_16bit());
  }
  inline uint16_t trigger_dac_code() const {
    int32_t max = volts_dac_code(5);
    int32_t min = volts_dac_code(0);
    return min + ((max - min) * dc_voice_->trigger_value() >> 15);
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

 private:
  void NoteToDacCode();

  Voice* dc_voice_;
  Voice* audio_voices_[kNumMaxVoicesPerPart];
  uint8_t num_audio_voices_;

  int32_t note_;
  uint16_t note_dac_code_;
  bool dirty_;  // Set to true when the calibration settings have changed.
  uint16_t calibrated_dac_code_[kNumOctaves];

  DISALLOW_COPY_AND_ASSIGN(CVOutput);
};

}  // namespace yarns

#endif // YARNS_VOICE_H_
