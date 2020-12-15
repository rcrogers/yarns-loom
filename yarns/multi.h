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
// Multi.

#ifndef YARNS_MULTI_H_
#define YARNS_MULTI_H_

#include "stmlib/stmlib.h"

#include "yarns/internal_clock.h"
#include "yarns/layout_configurator.h"
#include "yarns/part.h"
#include "yarns/voice.h"
#include "yarns/storage_manager.h"
#include "yarns/settings.h"

namespace yarns {

const uint8_t kNumParts = 4;
const uint8_t kNumCVOutputs = 4;
// One paraphonic part, one voice per remaining output
const uint8_t kNumSystemVoices = kNumParaphonicVoices + (kNumCVOutputs - 1);
const uint8_t kMaxBarDuration = 32;

struct MultiSettings {
  uint8_t layout;
  uint8_t clock_tempo;
  uint8_t clock_swing;
  uint8_t clock_input_division;
  uint8_t clock_output_division;
  uint8_t clock_bar_duration;
  uint8_t clock_override;
  int8_t custom_pitch_table[12];
  uint8_t remote_control_channel;
  uint8_t nudge_first_tick;
  uint8_t clock_manual_start;
  uint8_t padding[10];
};

enum Tempo {
  TEMPO_EXTERNAL = 39
};

enum MultiSetting {
  MULTI_LAYOUT,
  MULTI_CLOCK_TEMPO,
  MULTI_CLOCK_SWING,
  MULTI_CLOCK_INPUT_DIVISION,
  MULTI_CLOCK_OUTPUT_DIVISION,
  MULTI_CLOCK_BAR_DURATION,
  MULTI_CLOCK_OVERRIDE,
  MULTI_PITCH_1,
  MULTI_PITCH_2,
  MULTI_PITCH_3,
  MULTI_PITCH_4,
  MULTI_PITCH_5,
  MULTI_PITCH_6,
  MULTI_PITCH_7,
  MULTI_PITCH_8,
  MULTI_PITCH_9,
  MULTI_PITCH_10,
  MULTI_PITCH_11,
  MULTI_PITCH_12,
  MULTI_REMOTE_CONTROL_CHANNEL,
  MULTI_CLOCK_NUDGE_FIRST_TICK,
  MULTI_CLOCK_MANUAL_START,
};

enum Layout {
  LAYOUT_MONO,
  LAYOUT_DUAL_MONO,
  LAYOUT_QUAD_MONO,
  LAYOUT_DUAL_POLY,
  LAYOUT_QUAD_POLY,
  LAYOUT_DUAL_POLYCHAINED,
  LAYOUT_QUAD_POLYCHAINED,
  LAYOUT_OCTAL_POLYCHAINED,
  LAYOUT_QUAD_TRIGGERS,
  LAYOUT_QUAD_VOLTAGES,
  LAYOUT_THREE_ONE,
  LAYOUT_TWO_TWO,
  LAYOUT_TWO_ONE,
  LAYOUT_PARAPHONIC_PLUS_TWO,
  LAYOUT_LAST
};

class Multi {
 public:
  Multi() { }
  ~Multi() { }
  
  void Init(bool reset_calibration);
  
  inline uint8_t paques() const {
    return settings_.clock_tempo == 49 && \
        settings_.clock_swing == 49 && \
        settings_.clock_output_division == 6 && \
        settings_.clock_bar_duration == 9;
  }
  
  bool NoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    layout_configurator_.RegisterNote(channel, note);

    bool thru = true;
    bool received = false;
    for (uint8_t i = 0; i < num_active_parts_; ++i) {
      if (part_[i].accepts(channel, note, velocity)) {
        received = true;
        thru = part_[i].NoteOn(channel, part_[i].TransposeInputPitch(note), velocity) && thru;
      }
    }
    
    if (received &&
        !running() &&
        internal_clock() &&
        !settings_.clock_manual_start) {
      // Start the arpeggiators.
      Start(true);
    }
    
    stop_count_down_ = 0;
    
    return thru;
  }
  
  bool NoteOff(uint8_t channel, uint8_t note, uint8_t velocity) {
    bool thru = true;
    bool has_notes = false;
    for (uint8_t i = 0; i < num_active_parts_; ++i) {
      if (part_[i].accepts(channel, note)) {
        thru = part_[i].NoteOff(channel, part_[i].TransposeInputPitch(note)) && thru;
      }
      has_notes = has_notes || part_[i].has_notes();
    }
    
    if (!has_notes && internal_clock() && started_by_keyboard_) {
      stop_count_down_ = 12;
    }
    
    return thru;
  }
  
  bool ControlChange(uint8_t channel, uint8_t controller, uint8_t value);
  void SetFromCC(uint8_t part_index, uint8_t controller, uint8_t value);
  uint8_t GetSetting(const Setting& setting, uint8_t part) const;
  void ApplySetting(const Setting& setting, uint8_t part, int16_t raw_value);

  bool PitchBend(uint8_t channel, uint16_t pitch_bend) {
    bool thru = true;
    for (uint8_t i = 0; i < num_active_parts_; ++i) {
      if (part_[i].accepts(channel)) {
        thru = part_[i].PitchBend(channel, pitch_bend) && thru;
      }
    }
    return thru;
  }

  bool Aftertouch(uint8_t channel, uint8_t note, uint8_t velocity) {
    bool thru = true;
    for (uint8_t i = 0; i < num_active_parts_; ++i) {
      if (part_[i].accepts(channel, note)) {
        thru = part_[i].Aftertouch(channel, note, velocity) && thru;
      }
    }
    return thru;
  }

  bool Aftertouch(uint8_t channel, uint8_t velocity) {
    bool thru = true;
    for (uint8_t i = 0; i < num_active_parts_; ++i) {
      if (part_[i].accepts(channel)) {
        thru = part_[i].Aftertouch(channel, velocity) && thru;
      }
    }
    return thru;
  }
  
  void Reset() {
    for (uint8_t i = 0; i < num_active_parts_; ++i) {
      part_[i].Reset();
    }
  }
  
  void Clock();
  
  // A start initiated by a MIDI 0xfa event or the front panel start button will
  // start the sequencers. A start initiated by the keyboard will not start
  // the sequencers, and give priority to the arpeggiator. This allows the
  // arpeggiator to be played without erasing a sequence.
  void Start(bool started_by_keyboard);
  
  void Stop();
  
  void Continue() {
    Start(false);
  }
  
  void StartRecording(uint8_t part) {
    if (
      part_[part].midi_settings().play_mode == PLAY_MODE_MANUAL ||
      part >= num_active_parts_
    ) {
      return;
    }
    if (recording_) {
      if (recording_part_ == part) {
        return;
      } else {
        StopRecording(recording_part_);
      }
    }
    if (part_[part].looped()) {
      // Looper needs a running clock
      Start(false);
    }
    part_[part].StartRecording();
    uint8_t channel = part_[part].midi_settings().channel;
    bool has_velocity_filtering = part_[part].has_velocity_filtering();
    for (uint8_t i = 0; i < num_active_parts_; ++i) {
      bool same_channel = (
        part_[i].midi_settings().channel == channel ||
        channel == 0x10 ||
        part_[i].midi_settings().channel == 0x10
      );
      bool both_velocity_filtering = (
        has_velocity_filtering &&
        part_[i].has_velocity_filtering()
      );
      if (same_channel && !both_velocity_filtering) {
        part_[i].set_transposable(false);
      }
    }
    recording_ = true;
    recording_part_ = part;
  }
  
  void StopRecording(uint8_t part) {
    if (recording_ && recording_part_ == part) {
      part_[part].StopRecording();
      for (uint8_t i = 0; i < num_active_parts_; ++i) {
        part_[i].set_transposable(true);
      }
      recording_ = false;
    }
  }
  
  void PushItNoteOn(uint8_t note) {
    uint8_t mask = recording_ ? 0x80 : 0;
    for (uint8_t i = 0; i < num_active_parts_; ++i) {
      if (settings_.layout == LAYOUT_QUAD_TRIGGERS) {
        note = part_[i].midi_settings().min_note;
      }
      if (!recording_ || part_[i].recording()) {
        part_[i].NoteOn(part_[i].tx_channel() | mask, note, 127);
      }
    }
    if (!running() && internal_clock()) {
      // Start the arpeggiators.
      Start(true);
    }
  }
  
  void PushItNoteOff(uint8_t note) {
    uint8_t mask = recording_ ? 0x80 : 0;
    bool has_notes = false;
    for (uint8_t i = 0; i < num_active_parts_; ++i) {
      if (settings_.layout == LAYOUT_QUAD_TRIGGERS) {
        note = part_[i].midi_settings().min_note;
      }
      if (!recording_ || part_[i].recording()) {
        part_[i].NoteOff(part_[i].tx_channel() | mask, note);
      }
      has_notes = has_notes || part_[i].has_notes();
    }
    if (!has_notes && internal_clock() && started_by_keyboard_) {
      Stop();
    }
  }
  
  void Touch();
  void Refresh();
  void RefreshInternalClock() {
    if (running() && internal_clock() && internal_clock_.Process()) {
      ++internal_clock_ticks_;
    }
  }
  void ProcessInternalClockEvents() {
    while (internal_clock_ticks_) {
      Clock();
      --internal_clock_ticks_;
    }
  }

  void AdvanceLoopers() {
    if (!running()) {
      return;
    }
    for (uint8_t j = 0; j < num_active_parts_; ++j) {
      part_[j].LooperAdvance();
    }
  }

  inline void RenderAudio() {
    for (uint8_t i = 0; i < kNumCVOutputs; ++i) {
      cv_outputs_[i].RenderAudio(layout() == LAYOUT_PARAPHONIC_PLUS_TWO);
    }
  }
  
  bool Set(uint8_t address, uint8_t value);
  inline uint8_t Get(uint8_t address) const {
    const uint8_t* bytes;
    bytes = static_cast<const uint8_t*>(static_cast<const void*>(&settings_));
    return bytes[address];
  }
  
  inline Layout layout() const { return static_cast<Layout>(settings_.layout); }
  inline bool internal_clock() const { return settings_.clock_tempo > TEMPO_EXTERNAL; }
  inline uint8_t tempo() const { return settings_.clock_tempo; }
  inline bool running() const { return running_; }
  inline bool recording() const { return recording_; }
  inline uint8_t recording_part() const { return recording_part_; }
  inline bool clock() const {
    return clock_pulse_counter_ > 0 && \
        (!settings_.nudge_first_tick || \
          settings_.clock_bar_duration == 0 || \
          !reset());
  }
  inline bool reset() const {
    return reset_pulse_counter_ > 0;
  }
  inline bool reset_or_playing_flag() const {
    return reset() || ((settings_.clock_bar_duration == 0) && running_);
  }
  
  inline const CVOutput& cv_output(uint8_t index) const { return cv_outputs_[index]; }
  inline const Part& part(uint8_t index) const { return part_[index]; }
  inline const Voice& voice(uint8_t index) const { return voice_[index]; }
  inline const MultiSettings& settings() const { return settings_; }
  inline uint8_t num_active_parts() const { return num_active_parts_; }
  
  inline CVOutput* mutable_cv_output(uint8_t index) { return &cv_outputs_[index]; }
  inline Voice* mutable_voice(uint8_t index) { return &voice_[index]; }
  inline Part* mutable_part(uint8_t index) { return &part_[index]; }
  inline MultiSettings* mutable_settings() { return &settings_; }
  
  void set_custom_pitch(uint8_t pitch_class, int8_t correction) {
    settings_.custom_pitch_table[pitch_class] = correction;
  }
  
  // Returns true when no part does anything fancy with the MIDI stream (such
  // as producing arpeggiated notes, or suppressing messages). This means that
  // the MIDI dispatcher can just copy to the MIDI out a MIDI data byte as soon
  // as it is received. Otherwise, merging and message reformatting will be
  // necessary and the output stream will be delayed :(
  inline bool direct_thru() const {
    for (uint8_t i = 0; i < num_active_parts_; ++i) {
      if (!part_[i].direct_thru()) {
        return false;
      }
    }
    return true;
  }
  
  void AssignVoicesToCVOutputs();
  void GetCvGate(uint16_t* cv, bool* gate);
  void GetAudioSource(bool* audio_source);
  void GetLedsBrightness(uint8_t* brightness);

  template<typename T>
  void Serialize(T* stream_buffer) {
    stream_buffer->Write(settings());
    for (uint8_t i = 0; i < kNumParts; ++i) {
      STATIC_ASSERT(kStreamBufferSize >= (
        sizeof(settings()) + // 32 bytes
        kNumParts * ( // Max 248 bytes
          sizeof(part_[i].midi_settings()) +
          sizeof(part_[i].voicing_settings()) +
          sizeof(part_[i].sequencer_settings())
        )
      ), buffer_size_exceeded);
      stream_buffer->Write(part_[i].midi_settings()); // 16 bytes
      stream_buffer->Write(part_[i].voicing_settings()); // 32 bytes
      stream_buffer->Write(part_[i].sequencer_settings()); // 176 bytes
    }
  };
  
  template<typename T>
  void Deserialize(T* stream_buffer) {
    Stop();
    stream_buffer->Read(mutable_settings());
    for (uint8_t i = 0; i < kNumParts; ++i) {
      stream_buffer->Read(part_[i].mutable_midi_settings());
      stream_buffer->Read(part_[i].mutable_voicing_settings());
      stream_buffer->Read(part_[i].mutable_sequencer_settings());
    }
    Touch();
  };
  
  template<typename T>
  void SerializeCalibration(T* stream_buffer) {
    // 4 voices x 11 octaves x 2 bytes = 88 bytes
    for (uint8_t i = 0; i < kNumCVOutputs; ++i) {
      for (uint8_t j = 0; j < kNumOctaves; ++j) {
        stream_buffer->Write(cv_outputs_[i].calibration_dac_code(j)); // 2 bytes
      }
    }
  };
  
  template<typename T>
  void DeserializeCalibration(T* stream_buffer) {
    for (uint8_t i = 0; i < kNumCVOutputs; ++i) {
      for (uint8_t j = 0; j < kNumOctaves; ++j) {
        uint16_t v;
        stream_buffer->Read(&v);
        cv_outputs_[i].set_calibration_dac_code(j, v);
      }
    }
  };

  void StartLearning() {
    layout_configurator_.StartLearning();
  }

  void StopLearning() {
    layout_configurator_.StopLearning(this);
  }

  inline bool learning() const {
    return layout_configurator_.learning();
  }
  
  void StartSong();

 private:
  void ChangeLayout(Layout old_layout, Layout new_layout);
  void AllocateParts();
  void ClockSong();
  
  MultiSettings settings_;
  
  bool running_;
  bool started_by_keyboard_;
  bool recording_;
  uint8_t recording_part_;
  
  InternalClock internal_clock_;
  uint8_t internal_clock_ticks_;
  uint16_t midi_clock_tick_duration_;

  int16_t swing_predelay_[12];
  uint8_t swing_counter_;
  
  uint8_t clock_input_prescaler_;
  uint16_t clock_output_prescaler_;
  uint16_t bar_position_;
  uint8_t stop_count_down_;
  
  uint16_t clock_pulse_counter_;
  uint16_t reset_pulse_counter_;
  
  uint16_t previous_output_division_;
  bool needs_resync_;
  
  // Indicates that a setting has been changed and that the multi should
  // be saved in memory.
  bool dirty_;
  
  uint8_t num_active_parts_;
  
  Part part_[kNumParts];
  Voice voice_[kNumSystemVoices];
  CVOutput cv_outputs_[kNumCVOutputs];

  LayoutConfigurator layout_configurator_;
  
  const uint8_t* song_pointer_;
  uint32_t song_clock_;
  uint8_t song_delta_;

  DISALLOW_COPY_AND_ASSIGN(Multi);
};

extern Multi multi;

}  // namespace yarns

#endif // YARNS_MULTI_H_
