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

// Converts BPM to the Refresh phase increment of an LFO that cycles at 24 PPQN
const uint32_t kTempoToTickPhaseIncrement = (UINT32_MAX / 4000) * 24 / 60;

// Represents a controller number that has been routed to either remote control or a part, based on the channel the CC was received on
class CCRouting {
 public:
  static CCRouting Part(uint8_t controller, uint8_t part) {
    return CCRouting(controller, part);
  }
  static CCRouting Remote(uint8_t controller) {
    return CCRouting(controller, kRemote);
  }
  bool is_remote() {
    return part_or_remote_ == kRemote;
  }
  uint8_t part() {
    return is_remote() ? controller_ >> 5 : part_or_remote_;
  }
  uint8_t controller() {
    return controller_;
  }
 private:
  static const uint8_t kRemote = 0xff;
  CCRouting(uint8_t controller, uint8_t part_or_remote) {
    this->controller_ = controller;
    this->part_or_remote_ = part_or_remote;
  }
  uint8_t part_or_remote_;
  uint8_t controller_;
};

struct SettingRange {
  SettingRange(int16_t min, int16_t max) {
    this->min = min;
    this->max = max;
  }
  int16_t min;
  int16_t max;
};

struct PackedMulti {
  PackedPart parts[kNumParts];

  int8_t custom_pitch_table[12];

  unsigned int // 7 bits to spare (plus 8 in flash_padding)
    layout : 4, // values free: 1
    clock_tempo : 8, // values free: 54
    clock_swing : 7, // values free: 28
    clock_input_division : 3, // Breaking: can 0-index for 1 fewer bit
    clock_output_division : 5, // values free: 0
    clock_bar_duration : 6, // values free: 30
    clock_override : 1,
    remote_control_channel : 5, // values free: 15
    nudge_first_tick : 1,
    clock_manual_start : 1;

  uint8_t control_change_mode; // Breaking: move to bitfield when convenient
  int8_t clock_offset;
}__attribute__((packed));

struct MultiSettings {
  uint8_t layout;
  uint8_t clock_tempo;
  int8_t clock_swing;
  uint8_t clock_input_division;
  uint8_t clock_output_division;
  uint8_t clock_bar_duration;
  uint8_t clock_override;
  int8_t custom_pitch_table[12];
  uint8_t remote_control_channel; // first value = off
  uint8_t nudge_first_tick;
  uint8_t clock_manual_start;
  uint8_t control_change_mode;
  int8_t clock_offset;
  uint8_t padding[8];

  void Pack(PackedMulti& packed) {
    for (uint8_t i = 0; i < 12; i++) {
      packed.custom_pitch_table[i] = custom_pitch_table[i];
    }
    packed.layout = layout;
    packed.clock_tempo = clock_tempo;
    packed.clock_swing = clock_swing;
    packed.clock_input_division = clock_input_division;
    packed.clock_output_division = clock_output_division;
    packed.clock_bar_duration = clock_bar_duration;
    packed.clock_override = clock_override;
    packed.remote_control_channel = remote_control_channel;
    packed.nudge_first_tick = nudge_first_tick;
    packed.clock_manual_start = clock_manual_start;
    packed.control_change_mode = control_change_mode;
    packed.clock_offset = clock_offset;
  }

  void Unpack(PackedMulti& packed) {
    for (uint8_t i = 0; i < 12; i++) {
      custom_pitch_table[i] = packed.custom_pitch_table[i];
    }
    layout = packed.layout;
    clock_tempo = packed.clock_tempo;
    clock_swing = packed.clock_swing;
    clock_input_division = packed.clock_input_division;
    clock_output_division = packed.clock_output_division;
    clock_bar_duration = packed.clock_bar_duration;
    clock_override = packed.clock_override;
    remote_control_channel = packed.remote_control_channel;
    nudge_first_tick = packed.nudge_first_tick;
    clock_manual_start = packed.clock_manual_start;
    control_change_mode = packed.control_change_mode;
    clock_offset = packed.clock_offset;
  }
};

enum Tempo {
  TEMPO_EXTERNAL = 39
};

enum ControlChangeMode {
  CONTROL_CHANGE_MODE_OFF,
  CONTROL_CHANGE_MODE_ABSOLUTE,
  CONTROL_CHANGE_MODE_RELATIVE_DIRECT,
  CONTROL_CHANGE_MODE_RELATIVE_SCALED,
  CONTROL_CHANGE_MODE_LAST,
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
  MULTI_CONTROL_CHANGE_MODE,
  MULTI_CLOCK_OFFSET,
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
  LAYOUT_PARAPHONIC_PLUS_TWO, // Now a misnomer: has a 4th part
  LAYOUT_TRI_MONO,
  LAYOUT_PARAPHONIC_PLUS_ONE,
  LAYOUT_LAST
};

class Multi {
 public:
  Multi() { }
  ~Multi() { }

  void PrintDebugByte(uint8_t byte);
  
  void Init(bool reset_calibration);

  inline bool is_remote_control_channel(uint8_t channel) const {
    return channel + 1 == settings_.remote_control_channel;
  }

  inline const MidiSettings& midi(uint8_t part) const {
    return part_[part].midi_settings();
  }

  inline bool part_accepts_channel(uint8_t part, uint8_t channel) const {
    return is_remote_control_channel(channel) ||
      midi(part).channel == kMidiChannelOmni ||
      midi(part).channel == channel;
  }

  inline bool part_accepts_note(
    uint8_t part, uint8_t channel, uint8_t note
  ) const {
    if (!part_accepts_channel(part, channel)) {
      return false;
    }
    if (midi(part).min_note <= midi(part).max_note) {
      return note >= midi(part).min_note && note <= midi(part).max_note;
    } else {
      return note <= midi(part).max_note || note >= midi(part).min_note;
    }
  }

  inline bool part_accepts_note_on(
    uint8_t part, uint8_t channel, uint8_t note, uint8_t velocity
  ) const {
    // Block NoteOn, but allow NoteOff so the key can transition from
    // sustainable to sustained
    if (
      midi(part).sustain_mode == SUSTAIN_MODE_FILTER &&
      part_[part].HeldKeysForUI().universally_sustainable
    ) {
      return false;
    }
    return part_accepts_note(part, channel, note) && \
        velocity >= midi(part).min_velocity && \
        velocity <= midi(part).max_velocity;
  }

  bool NoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    layout_configurator_.RegisterNote(channel, note);

    bool thru = true;
    bool received = false;
    if (recording_ && part_accepts_note_on(recording_part_, channel, note, velocity)) {
      received = true;
      thru = thru && part_[recording_part_].notes_thru();
      part_[recording_part_].NoteOn(channel, part_[recording_part_].TransposeInputPitch(note), velocity);
    } else {
      for (uint8_t i = 0; i < num_active_parts_; ++i) {
        if (!part_accepts_note_on(i, channel, note, velocity)) { continue; }
        received = true;
        thru = thru && part_[recording_part_].notes_thru();
        part_[i].NoteOn(channel, part_[i].TransposeInputPitch(note), velocity);
      }
    }
    
    if (received &&
        !running() &&
        internal_clock() &&
        !settings_.clock_manual_start) {
      set_next_clock_input_tick(0);
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
      has_notes = has_notes || part_[i].has_notes();
      if (!part_accepts_note(i, channel, note)) continue;
      thru = thru && part_[i].notes_thru();
      part_[i].NoteOff(channel, part_[i].TransposeInputPitch(note));
    }
    
    if (!has_notes && CanAutoStop()) {
      stop_count_down_ = 12;
    }
    
    return thru;
  }
  
  bool ControlChange(uint8_t channel, uint8_t controller, uint8_t value_7bits);
  int16_t UpdateController(CCRouting cc, uint8_t value_7bits);
  void SetFromCC(CCRouting cc, int16_t scaled_value);
  void InferControllerValue(CCRouting cc);

  uint8_t ScaleSettingToController(SettingRange range, int16_t value) const;
  const Setting* GetSettingForController(CCRouting cc) const;

  int16_t GetControllableValue(CCRouting cc) const;
  int16_t GetSettingValue(const Setting& setting, uint8_t part) const;

  SettingRange GetControllableRange(CCRouting cc) const;
  SettingRange GetSettingRange(const Setting& setting, uint8_t part) const;

  void ApplySetting(SettingIndex setting, uint8_t part, int16_t raw_value) {
    ApplySetting(setting_defs.get(setting), part, raw_value);
  };
  void ApplySetting(const Setting& setting, uint8_t part, int16_t raw_value);
  void ApplySettingAndSplash(const Setting& setting, uint8_t part, int16_t raw_value);

  bool PitchBend(uint8_t channel, uint16_t pitch_bend) {
    bool thru = true;
    for (uint8_t i = 0; i < num_active_parts_; ++i) {
      if (part_accepts_channel(i, channel)) {
        thru = thru && part_[i].cc_thru();
        part_[i].PitchBend(channel, pitch_bend);
      }
    }
    return thru;
  }

  bool Aftertouch(uint8_t channel, uint8_t note, uint8_t velocity) {
    bool thru = true;
    for (uint8_t i = 0; i < num_active_parts_; ++i) {
      if (part_accepts_note(i, channel, note)) {
        thru = thru && part_[i].cc_thru();
        part_[i].Aftertouch(channel, note, velocity);
      }
    }
    return thru;
  }

  bool Aftertouch(uint8_t channel, uint8_t velocity) {
    bool thru = true;
    for (uint8_t i = 0; i < num_active_parts_; ++i) {
      if (part_accepts_channel(i, channel)) {
        thru = thru && part_[i].cc_thru();
        part_[i].Aftertouch(channel, velocity);
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
  inline void set_next_clock_input_tick(uint16_t n) {
    // We haven't actually received the target tick yet -- Clock() will
    // pre-increment -- so the last Clock we received is one prior
    clock_input_ticks_ = n - 1;
  }
  
  // A start initiated by a MIDI 0xfa event or the front panel start button will
  // start the sequencers. A start initiated by the keyboard will not start
  // the sequencers, and give priority to the arpeggiator. This allows the
  // arpeggiator to be played without erasing a sequence.
  //
  // If already running, will not reset clock state
  void Start(bool started_by_keyboard);
  
  void Stop();

  inline bool CanAutoStop() const {
    return started_by_keyboard_ && internal_clock();
  }
  
  void StartRecording(uint8_t part);
  void StopRecording(uint8_t part);
  
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
      set_next_clock_input_tick(0);
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
    if (!has_notes && CanAutoStop()) {
      Stop();
    }
  }
  
  void AfterDeserialize();
  inline void UpdateResetPulse() { if (reset_pulse_counter_) --reset_pulse_counter_; }
  void Refresh();
  void ClockLFOs(int32_t, bool);
  void RefreshInternalClock() {
    if (running() && internal_clock() && internal_clock_.Process()) {
      ++internal_clock_ticks_;
    }
  }

  void LowPriority() {
    while (internal_clock_ticks_) {
      Clock();
      --internal_clock_ticks_;
    }

    bool can_play = tick_counter() >= 0;
    for (uint8_t p = 0; p < num_active_parts_; ++p) {
      if (running()) {
        bool play = can_play && part_[p].looper_in_use();
        part_[p].mutable_looper().ProcessNotesUntilLFOPhase(
          play ? &Part::LooperPlayNoteOn : NULL,
          play ? &Part::LooperPlayNoteOff : NULL
        );
      }
      for (uint8_t v = 0; v < part_[p].num_voices(); ++v) {
        part_[p].voice(v)->RenderSamples();
      }
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
  // After applying clock input division, then clock offset
  inline int32_t tick_counter(int8_t input_bias = 0) const {
    return DIV_FLOOR(clock_input_ticks_ + input_bias, settings_.clock_input_division) + settings_.clock_offset;
  }
  inline uint8_t tempo() const { return settings_.clock_tempo; }
  inline bool running() const { return running_; }
  inline bool recording() const { return recording_; }
  inline uint8_t recording_part() const { return recording_part_; }
  bool clock() const;
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
      if (!part_[i].notes_thru()) {
        return false;
      }
    }
    return true;
  }
  
  void AssignOutputVoice(
    uint8_t cv_i, uint8_t voice_i, DCRole r, uint8_t num_audio_voices
  ) {
    cv_outputs_[cv_i].assign(&voice_[voice_i], r, num_audio_voices);
  }
  void AssignVoicesToCVOutputs();
  void GetCvGate(uint16_t* cv, bool* gate);
  void GetLedsBrightness(uint8_t* brightness);

  template<typename T>
  void Serialize(T* stream_buffer) {
    PackedMulti packed;
    for (uint8_t i = 0; i < kNumParts; i++) {
      part_[i].Pack(packed.parts[i]);
    }
    settings_.Pack(packed);
    const uint16_t size = sizeof(packed);
    // char (*__debug)[size] = 1;
    STATIC_ASSERT(size == 1020, expected);
    STATIC_ASSERT(size % 4 == 0, flash_word);
    STATIC_ASSERT(size <= kMaxSize, capacity);
    stream_buffer->Write(packed);
  };
  
  template<typename T>
  void Deserialize(T* stream_buffer) {
    StopRecording(recording_part_);
    Stop();
    PackedMulti packed;
    stream_buffer->Read(&packed);
    for (uint8_t i = 0; i < kNumParts; i++) {
      part_[i].Unpack(packed.parts[i]);
    }
    settings_.Unpack(packed);
    AfterDeserialize();
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
  
 private:
  void ChangeLayout(Layout old_layout, Layout new_layout);
  void UpdateTempo();
  void AllocateParts();
  void SpreadLFOs(int8_t spread, FastSyncedLFO** base_lfo, uint8_t num_lfos, bool force_phase);
  
  MultiSettings settings_;
  
  bool running_;
  bool started_by_keyboard_;
  bool recording_;
  uint8_t recording_part_;
  
  InternalClock internal_clock_;
  uint8_t internal_clock_ticks_;
  
  // The 0-based index of the last received Clock event, ignoring
  // division/offset. Negative if we have not yet received a Clock. At 240 BPM *
  // 24 PPQN = 96 Hz, this overflows after 259 days
  int32_t clock_input_ticks_;

  bool can_advance_lfos_;

  // While the clock is running, the backup LFO syncs to the clock's phase/freq,
  // and while the clock is stopped, the backup LFO continues free-running based
  // on its last sync, to provide a tick counter for looper/modulation LFOs
  FastSyncedLFO backup_clock_lfo_;
  // 1:1 with divided ticks, but can free-run without the clock
  int32_t backup_clock_lfo_ticks_;

  uint8_t stop_count_down_;
  
  uint16_t reset_pulse_counter_;
  
  uint8_t num_active_parts_;

  // "Virtual knobs" to track the accumulated result of CCs in relative mode.
  //
  // There is some wasted space here, because 1) not all controller numbers are mapped to a setting or macro, and 2) most remote controls map to part settings, which are also tracked in part_controller_value_
  uint8_t remote_control_controller_value_[128];
  uint8_t part_controller_value_[kNumParts][128];

  Part part_[kNumParts];
  Voice voice_[kNumSystemVoices];
  CVOutput cv_outputs_[kNumCVOutputs];

  LayoutConfigurator layout_configurator_;

  DISALLOW_COPY_AND_ASSIGN(Multi);
};

extern Multi multi;

}  // namespace yarns

#endif // YARNS_MULTI_H_
