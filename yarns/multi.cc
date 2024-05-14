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

#include "yarns/multi.h"

#include <algorithm>

#include "stmlib/algorithms/voice_allocator.h"

#include "yarns/just_intonation_processor.h"
#include "yarns/midi_handler.h"
#include "yarns/settings.h"
#include "yarns/ui.h"

namespace yarns {
  
using namespace std;
using namespace stmlib;

const uint8_t kCCLooperPhaseOffset = 115;

const uint8_t kCCMacroRecord = 116;
enum MacroRecord {
  MACRO_RECORD_OFF,
  MACRO_RECORD_ON,
  MACRO_RECORD_OVERWRITE,
  MACRO_RECORD_DELETE,
};

const uint8_t kCCMacroPlayMode = 117;
enum MacroPlayMode {
  MACRO_PLAY_MODE_STEP_SEQ = -2,
  MACRO_PLAY_MODE_STEP_ARP,
  MACRO_PLAY_MODE_MANUAL,
  MACRO_PLAY_MODE_LOOP_ARP,
  MACRO_PLAY_MODE_LOOP_SEQ
};

const uint8_t kMasterLFOPeriodTicksBits = 4;

void Multi::PrintDebugByte(uint8_t byte) {
  ui.PrintDebugByte(byte);
}

void Multi::Init(bool reset_calibration) {
  just_intonation_processor.Init();
  master_lfo_.Init();
  
  fill(
      &settings_.custom_pitch_table[0],
      &settings_.custom_pitch_table[12],
      0);
  
  for (uint8_t i = 0; i < kNumParts; ++i) {
    part_[i].Init();
    part_[i].set_custom_pitch_table(settings_.custom_pitch_table);
  }
  fill(&swing_predelay_[0], &swing_predelay_[12], -1);
  for (uint8_t i = 0; i < kNumSystemVoices; ++i) {
    voice_[i].Init();
  }
  for (uint8_t i = 0; i < kNumCVOutputs; ++i) {
    cv_outputs_[i].Init(reset_calibration);
  }
  running_ = false;
  recording_ = false;
  recording_part_ = 0;
  started_by_keyboard_ = true;
  
  // Put the multi in a usable state. Even if these settings will later be
  // overridden with some data retrieved from Flash (presets).
  settings_.clock_tempo = 120;
  settings_.clock_swing = 0;
  settings_.clock_input_division = 1;
  settings_.clock_output_division = 20;
  settings_.clock_bar_duration = 4;
  settings_.clock_override = 0;
  settings_.nudge_first_tick = 0;
  settings_.clock_manual_start = 0;
  settings_.control_change_mode = CONTROL_CHANGE_MODE_ABSOLUTE;

  // A test sequence...
  // seq->num_steps = 4;
  // seq->step[0].data[0] = 48;
  // seq->step[0].data[1] = 0x7f;
  // seq->step[1].data[0] = 72;
  // seq->step[1].data[1] = 0x7f;
  // seq->step[2].data[0] = 60;
  // seq->step[2].data[1] = 0x7f;
  // seq->step[3].data[0] = 72;
  // seq->step[3].data[1] = 0x7f;
  // voicing->oscillator_shape = 1;
  // settings_.clock_tempo = 100;
  // settings_.clock_swing = 99;

  settings_.layout = LAYOUT_MONO;
  AfterDeserialize();
}

void Multi::Clock() {
  if (!running_) {
    return;
  }
  
  uint16_t output_division = lut_clock_ratio_ticks[settings_.clock_output_division];
  uint16_t input_division = settings_.clock_input_division;
  
  if (previous_output_division_ &&
      output_division != previous_output_division_) {
    needs_resync_ = true;
  }
  previous_output_division_ = output_division;
  
  // Logic equation for computing a clock output with a 50% duty cycle.
  if (output_division > 1) {
    if (clock_output_prescaler_ == 0 && clock_input_prescaler_ == 0) {
      clock_pulse_counter_ = 0xffff;
    }
    if (clock_output_prescaler_ >= (output_division >> 1) &&
        clock_input_prescaler_ >= (input_division >> 1)) {
      clock_pulse_counter_ = 0;
    }
  } else {
    if (input_division > 1) {
      clock_pulse_counter_ = \
          clock_input_prescaler_ <= (input_division - 1) >> 1 ? 0xffff : 0;
    } else {
      // Because no division is used, neither on the output nor on the input,
      // we don't have a sufficient fast time base to derive a 50% duty cycle
      // output. Instead, we output 5ms pulses.
      clock_pulse_counter_ = 40;
    }
  }
  
  if (!clock_input_prescaler_) {
    midi_handler.OnClock();

    // Sync LFOs
    ++tick_counter_;
    // The master LFO runs at a fraction of the clock frequency, which makes for
    // less jitter than 1-cycle-per-tick
    master_lfo_.Tap(tick_counter_, 1 << kMasterLFOPeriodTicksBits);
    for (uint8_t p = 0; p < num_active_parts_; ++p) {
      part_[p].mutable_looper().Clock();
    }
    
    ++swing_counter_;
    if (swing_counter_ >= 12) {
      swing_counter_ = 0;
    }
    
    if (internal_clock()) {
      swing_predelay_[swing_counter_] = 0;
    } else {
      uint32_t interval = midi_clock_tick_duration_;
      midi_clock_tick_duration_ = 0;

      uint32_t modulation = swing_counter_ < 6
          ? swing_counter_ : 12 - swing_counter_;
      swing_predelay_[swing_counter_] = \
          27 * modulation * interval * uint32_t(settings_.clock_swing) >> 13;
    }
    
    ++bar_position_;
    if (bar_position_ >= settings_.clock_bar_duration * 24) {
      bar_position_ = 0;
    }
    if (bar_position_ == 0) {
      reset_pulse_counter_ = settings_.nudge_first_tick ? 9 : 81;
      if (needs_resync_) {
        clock_output_prescaler_ = 0;
        needs_resync_ = false;
      }
    }
    if (settings_.clock_bar_duration > kMaxBarDuration) {
      bar_position_ = 1;
    }
    
    ++clock_output_prescaler_;
    if (clock_output_prescaler_ >= output_division) {
      clock_output_prescaler_ = 0;
    }
  }

  ++clock_input_prescaler_;
  if (clock_input_prescaler_ >= settings_.clock_input_division) {
    clock_input_prescaler_ = 0;
  }
  
  if (stop_count_down_) {
    --stop_count_down_;
    if (!stop_count_down_ && CanAutoStop()) {
      Stop();
    }
  }
}

void Multi::Start(bool started_by_keyboard) {
  // Non-keyboard start can override a keyboard start
  started_by_keyboard_ = started_by_keyboard_ && started_by_keyboard;
  if (running_) {
    return;
  }
  if (internal_clock()) {
    internal_clock_ticks_ = 0;
    internal_clock_.Start(settings_.clock_tempo, settings_.clock_swing);
  }
  midi_handler.OnStart();

  running_ = true;
  clock_input_prescaler_ = 0;
  clock_output_prescaler_ = 0;
  stop_count_down_ = 0;
  tick_counter_ = master_lfo_tick_counter_ = -1;
  master_lfo_.Init(-1); // Will output a tick on next Refresh
  bar_position_ = -1;
  swing_counter_ = -1;
  previous_output_division_ = 0;
  needs_resync_ = false;
  
  fill(&swing_predelay_[0], &swing_predelay_[12], -1);
  
  for (uint8_t i = 0; i < num_active_parts_; ++i) {
    part_[i].Start();
  }
  midi_clock_tick_duration_ = 0;
}

void Multi::Stop() {
  if (!running()) {
    return;
  }
  for (uint8_t i = 0; i < num_active_parts_; ++i) {
    part_[i].StopSequencerArpeggiatorNotes();
  }
  midi_handler.OnStop();
  clock_pulse_counter_ = 0;
  reset_pulse_counter_ = 0;
  stop_count_down_ = 0;
  running_ = false;
  started_by_keyboard_ = true;
}

void Multi::ClockFast() {
  if (clock_pulse_counter_) {
    --clock_pulse_counter_;
  }
  if (reset_pulse_counter_) {
    --reset_pulse_counter_;
  }

  ++midi_clock_tick_duration_;
  for (int i = 0; i < 12; ++i) {
    if (swing_predelay_[i] == 0) {
      for (uint8_t j = 0; j < num_active_parts_; ++j) {
        part_[j].Clock();
      }
    }
    if (swing_predelay_[i] >= 0) {
      --swing_predelay_[i];
    }
  }
}

void Multi::SpreadLFOs(int8_t spread, FastSyncedLFO** base_lfo, uint8_t num_lfos) {
  if (spread >= 0) { // Detune
    uint8_t spread_8 = spread << 1;
    uint16_t spread_expo_16 = UINT16_MAX - lut_env_expo[((127 - spread_8) << 1)];
    uint32_t phase_increment = (*base_lfo)->GetPhaseIncrement();
    for (uint8_t i = 1; i < num_lfos; ++i) {
      phase_increment += ((phase_increment >> 4) * (spread_expo_16 >> 4)) >> 8;
      (*(base_lfo + i))->SetPhaseIncrement(phase_increment);
    }
  } else { // Dephase
    uint32_t phase = (*base_lfo)->GetPhase();
    uint32_t phase_offset = (spread + 1) << (32 - 6);
    for (uint8_t i = 1; i < num_lfos; ++i) {
      phase += phase_offset;
      (*(base_lfo + i))->SetTargetPhase(phase);
    }
  }
}

void Multi::Refresh() {
  master_lfo_.Refresh();
  // Since the master LFO runs at 1/n of clock freq, we compensate by treating
  // each 1/n of its phase as a new tick, to make these output ticks 1:1 with
  // the original clock ticks
  bool new_tick =
    (master_lfo_.GetPhase() << kMasterLFOPeriodTicksBits) <
    (master_lfo_.GetPhaseIncrement() << kMasterLFOPeriodTicksBits);
  if (new_tick) master_lfo_tick_counter_++;

  for (uint8_t p = 0; p < num_active_parts_; ++p) {
    Part& part = part_[p];
    part.mutable_looper().Refresh();
    if (new_tick) {
      uint8_t lfo_rate = part.voicing_settings().lfo_rate;
      FastSyncedLFO* part_lfos[part.num_voices()];
      for (uint8_t v = 0; v < part.num_voices(); ++v) {
        part_lfos[v] = part.voice(v)->lfo(static_cast<LFORole>(0));
      }
      if (lfo_rate < 64) {
        part_lfos[0]->Tap(master_lfo_tick_counter_, lut_clock_ratio_ticks[(64 - lfo_rate - 1) >> 1]);
      } else {
        part_lfos[0]->SetPhaseIncrement(lut_lfo_increments[lfo_rate - 64]);
      }
      SpreadLFOs(part.voicing_settings().lfo_spread_voices, &part_lfos[0], part.num_voices());
      for (uint8_t v = 0; v < part.num_voices(); ++v) {
        FastSyncedLFO* voice_lfos[LFO_ROLE_LAST];
        for (uint8_t l = 0; l < LFO_ROLE_LAST; ++l) {
          voice_lfos[l] = part.voice(v)->lfo(static_cast<LFORole>(l));
        }
        SpreadLFOs(part.voicing_settings().lfo_spread_types, &voice_lfos[0], LFO_ROLE_LAST);
      }
    }
    for (uint8_t v = 0; v < part.num_voices(); ++v) {
      part.voice(v)->Refresh();
    }
  }

  for (uint8_t i = 0; i < kNumCVOutputs; ++i) {
    cv_outputs_[i].Refresh();
  }
}

bool Multi::Set(uint8_t address, uint8_t value) {
  uint8_t* bytes;
  bytes = static_cast<uint8_t*>(static_cast<void*>(&settings_));
  uint8_t previous_value = bytes[address];
  bytes[address] = value;
  if (value == previous_value) { return false; }
  if (address == MULTI_LAYOUT) {
    ChangeLayout(
        static_cast<Layout>(previous_value),
        static_cast<Layout>(value));
  } else if (address == MULTI_CLOCK_TEMPO) {
    UpdateTempo();
  } else if (address == MULTI_CLOCK_SWING) {
    internal_clock_.set_swing(settings_.clock_swing);
  }
  return true;
}

void Multi::AssignVoicesToCVOutputs() {
  for (uint8_t v = 0; v < kNumSystemVoices; ++v) {
    voice_[v].set_audio_output(NULL);
    for (int role = 0; role < DC_LAST; role++) {
      voice_[v].set_dc_output(static_cast<DCRole>(role), NULL);
    }
  }
  switch (settings_.layout) {
    case LAYOUT_MONO:
    case LAYOUT_DUAL_POLYCHAINED:
      AssignOutputVoice(0, 0, DC_PITCH, 0);
      AssignOutputVoice(1, 0, DC_VELOCITY, 0);
      AssignOutputVoice(2, 0, DC_AUX_1, 0);
      AssignOutputVoice(3, 0, DC_AUX_2, 1);
      break;

    case LAYOUT_DUAL_MONO:
      AssignOutputVoice(0, 0, DC_PITCH, 0);
      AssignOutputVoice(1, 1, DC_PITCH, 0);
      AssignOutputVoice(2, 0, DC_AUX_1, 1);
      AssignOutputVoice(3, 1, DC_AUX_1, 1);
      break;

    case LAYOUT_DUAL_POLY:
    case LAYOUT_QUAD_POLYCHAINED:
      AssignOutputVoice(0, 0, DC_PITCH, 0);
      AssignOutputVoice(1, 1, DC_PITCH, 0);
      AssignOutputVoice(2, 0, DC_AUX_1, 1);
      AssignOutputVoice(3, 1, DC_AUX_2, 1);
      break;

    case LAYOUT_QUAD_MONO:
    case LAYOUT_QUAD_POLY:
    case LAYOUT_OCTAL_POLYCHAINED:
    case LAYOUT_THREE_ONE:
    case LAYOUT_TWO_TWO:
      for (uint8_t i = 0; i < kNumCVOutputs; ++i) {
        AssignOutputVoice(i, i, DC_PITCH, 1);
      }
      break;
    case LAYOUT_QUAD_VOLTAGES:
      for (uint8_t i = 0; i < kNumCVOutputs; ++i) {
        AssignOutputVoice(i, i, DC_AUX_1, 1);
      }
      break;
    case LAYOUT_QUAD_TRIGGERS:
      for (uint8_t i = 0; i < kNumCVOutputs; ++i) {
        AssignOutputVoice(i, i, DC_TRIGGER, 1);
      }
      break;

    case LAYOUT_TWO_ONE:
      AssignOutputVoice(0, 0, DC_PITCH, 1);
      AssignOutputVoice(1, 1, DC_PITCH, 1);
      AssignOutputVoice(2, 2, DC_PITCH, 1);
      AssignOutputVoice(3, 2, DC_AUX_2, 0);
      break;

    case LAYOUT_PARAPHONIC_PLUS_TWO:
      AssignOutputVoice(0, 0, DC_PITCH, kNumParaphonicVoices);
      AssignOutputVoice(1, kNumParaphonicVoices, DC_PITCH, 1);
      AssignOutputVoice(2, kNumParaphonicVoices, DC_AUX_1, 0);
      AssignOutputVoice(3, kNumParaphonicVoices + 1, DC_PITCH, 1);
      // Do not assign the last voice to any CV output, since it only outputs gates
      break;

    case LAYOUT_TRI_MONO:
      for (uint8_t i = 0; i < 3; ++i) {
        AssignOutputVoice(i, i, DC_PITCH, 1);
      }
      AssignOutputVoice(3, 0, DC_VELOCITY, 0); // Dummy, will be overwritten
      break;
  }
}

void Multi::GetCvGate(uint16_t* cv, bool* gate) {
  for (uint8_t i = 0; i < kNumCVOutputs; ++i) {
    cv[i] = cv_outputs_[i].dc_dac_code();
  }

  switch (settings_.layout) {
    case LAYOUT_MONO:
    case LAYOUT_DUAL_POLYCHAINED:
      gate[0] = voice_[0].gate();
      gate[1] = voice_[0].trigger();
      gate[2] = clock();
      gate[3] = reset_or_playing_flag();
      break;
      
    case LAYOUT_DUAL_MONO:
      gate[0] = voice_[0].gate();
      gate[1] = voice_[1].gate();
      gate[2] = clock();
      gate[3] = reset_or_playing_flag();
      break;
    
    case LAYOUT_DUAL_POLY:
    case LAYOUT_QUAD_POLYCHAINED:
      gate[0] = voice_[0].gate();
      gate[1] = voice_[1].gate();
      gate[2] = clock();
      gate[3] = reset_or_playing_flag();
      break;
    
    case LAYOUT_QUAD_MONO:
    case LAYOUT_QUAD_POLY:
    case LAYOUT_OCTAL_POLYCHAINED:
      gate[0] = voice_[0].gate();
      gate[1] = voice_[1].gate();
      if (settings_.clock_override) {
        gate[2] = clock();
        gate[3] = reset_or_playing_flag();
      } else {
        gate[2] = voice_[2].gate();
        gate[3] = voice_[3].gate();
      }
      break;

    case LAYOUT_THREE_ONE:
    case LAYOUT_TWO_TWO:
      gate[0] = voice_[0].gate();
      gate[1] = voice_[1].gate();
      gate[2] = voice_[2].gate();
      if (settings_.clock_override) {
        gate[3] = clock();
      } else {
        gate[3] = voice_[3].gate();
      }
      break;
    
    case LAYOUT_TWO_ONE:
      gate[0] = voice_[0].gate();
      gate[1] = voice_[1].gate();
      gate[2] = voice_[2].gate();
      gate[3] = clock();
      break;

    case LAYOUT_PARAPHONIC_PLUS_TWO:
      gate[0] = voice_[kNumSystemVoices - 1].gate();
      gate[1] = cv_outputs_[1].gate();
      gate[2] = settings_.clock_override ? clock() : cv_outputs_[2].trigger();
      gate[3] = cv_outputs_[3].gate();
      break;

    case LAYOUT_TRI_MONO:
      for (uint8_t i = 0; i < 3; ++i) {
        gate[i] = voice_[i].gate();
      }
      gate[3] = clock();
      cv[3] = cv_outputs_[3].volts_dac_code(reset_or_playing_flag() ? 5 : 0);
      break;

    case LAYOUT_QUAD_TRIGGERS:
      gate[0] = voice_[0].trigger() && ~voice_[1].gate();
      gate[1] = voice_[0].trigger() && voice_[1].gate();
      gate[2] = clock();
      gate[3] = reset_or_playing_flag();
      break;

    case LAYOUT_QUAD_VOLTAGES:
      gate[0] = voice_[0].gate();
      gate[1] = voice_[1].gate();
      if (settings_.clock_override) {
        gate[2] = clock();
        gate[3] = reset_or_playing_flag();
      } else {
        gate[2] = voice_[2].gate();
        gate[3] = voice_[3].gate();
      }
      break;
  }
}

void Multi::GetLedsBrightness(uint8_t* brightness) {
  if (layout_configurator_.learning()) {
    fill(&brightness[0], &brightness[kNumCVOutputs], 0);
    for (uint8_t i = 0; i < layout_configurator_.num_notes(); ++i) {
      brightness[i] = 255;
    }
    return;
  }
  
  switch (settings_.layout) {
    case LAYOUT_MONO:
    case LAYOUT_DUAL_POLYCHAINED:
      brightness[0] = voice_[0].gate() ? 255 : 0;
      brightness[1] = voice_[0].velocity() << 1;
      brightness[2] = voice_[0].aux_cv();
      brightness[3] = voice_[0].aux_cv_2();
      break;
      
    case LAYOUT_DUAL_MONO:
      brightness[0] = voice_[0].gate() ? 255 : 0;
      brightness[1] = voice_[1].gate() ? 255 : 0;
      brightness[2] = voice_[0].aux_cv();
      brightness[3] = voice_[1].aux_cv();
      break;

    case LAYOUT_DUAL_POLY:
    case LAYOUT_QUAD_POLYCHAINED:
      brightness[0] = voice_[0].gate() ? 255 : 0;
      brightness[1] = voice_[1].gate() ? 255 : 0;
      brightness[2] = voice_[0].aux_cv();
      brightness[3] = voice_[1].aux_cv_2();
      break;
      
    case LAYOUT_QUAD_MONO:
    case LAYOUT_QUAD_POLY:
    case LAYOUT_OCTAL_POLYCHAINED:
    case LAYOUT_QUAD_TRIGGERS:
    case LAYOUT_THREE_ONE:
    case LAYOUT_TWO_TWO:
      brightness[0] = voice_[0].gate() ? (voice_[0].velocity() << 1) : 0;
      brightness[1] = voice_[1].gate() ? (voice_[1].velocity() << 1) : 0;
      brightness[2] = voice_[2].gate() ? (voice_[2].velocity() << 1) : 0;
      brightness[3] = voice_[3].gate() ? (voice_[3].velocity() << 1) : 0;
      break;
      
    case LAYOUT_TWO_ONE:
      brightness[0] = voice_[0].gate() ? (voice_[0].velocity() << 1) : 0;
      brightness[1] = voice_[1].gate() ? (voice_[1].velocity() << 1) : 0;
      brightness[2] = voice_[2].gate() ? 255 : 0;
      brightness[3] = clock() ? voice_[2].aux_cv_2() : 0;
      break;

    case LAYOUT_PARAPHONIC_PLUS_TWO:
      {
        // const NoteEntry& last_note = part_[0].priority_note(NOTE_STACK_PRIORITY_LAST);
        // const uint8_t last_voice = part_[0].FindVoiceForNote(last_note.note);
        // brightness[0] = (
        //   last_note.note == NOTE_STACK_FREE_SLOT ||
        //   last_voice == VOICE_ALLOCATION_NOT_FOUND
        // ) ? 0 : part_[0].voice(last_voice)->velocity() << 1;
        brightness[0] = voice_[kNumSystemVoices - 1].gate() ? 255 : 0;
        brightness[1] = voice_[kNumParaphonicVoices].gate() ? (voice_[kNumParaphonicVoices].velocity() << 1) : 0;
        brightness[2] = voice_[kNumParaphonicVoices].aux_cv();
        brightness[3] = voice_[kNumParaphonicVoices + 1].gate() ? (voice_[kNumParaphonicVoices + 1].velocity() << 1) : 0;
      }
      break;

    case LAYOUT_TRI_MONO:
      for (uint8_t i = 0; i < 3; ++i) {
        brightness[i] = voice_[i].gate() ? (voice_[i].velocity() << 1) : 0;
      }
      brightness[3] = clock() ? 0xff : 0;
      break;

    case LAYOUT_QUAD_VOLTAGES:
      brightness[0] = voice_[0].aux_cv();
      brightness[1] = voice_[1].aux_cv();
      brightness[2] = voice_[2].aux_cv();
      brightness[3] = voice_[3].aux_cv();
      break;
  }
}

void Multi::AllocateParts() {
  // Reset and close all parts and voices.
  for (uint8_t i = 0; i < kNumParts; ++i) {
    part_[i].Reset();
  }
  for (uint8_t i = 0; i < kNumSystemVoices; ++i) {
    voice_[i].NoteOff();
  }
  
  switch (settings_.layout) {
    case LAYOUT_MONO:
    case LAYOUT_DUAL_MONO:
    case LAYOUT_QUAD_MONO:
    case LAYOUT_QUAD_TRIGGERS:
    case LAYOUT_QUAD_VOLTAGES:
      {
        num_active_parts_ = settings_.layout == LAYOUT_MONO ? 1 : \
            (settings_.layout == LAYOUT_DUAL_MONO ? 2 : 4);
        for (uint8_t i = 0; i < num_active_parts_; ++i) {
          part_[i].AllocateVoices(&voice_[i], 1, false);
        }
      }
      break;
    
    case LAYOUT_DUAL_POLY:
    case LAYOUT_QUAD_POLY:
    case LAYOUT_DUAL_POLYCHAINED:
    case LAYOUT_QUAD_POLYCHAINED:
    case LAYOUT_OCTAL_POLYCHAINED:
      {
        uint8_t num_voices = settings_.layout == LAYOUT_DUAL_POLY || \
            settings_.layout == LAYOUT_QUAD_POLYCHAINED ? 2 : \
            (settings_.layout == LAYOUT_DUAL_POLYCHAINED ? 1 : 4);
        part_[0].AllocateVoices(
            &voice_[0],
            num_voices,
            settings_.layout >= LAYOUT_DUAL_POLYCHAINED);
        num_active_parts_ = 1;
      }
      break;
      
    case LAYOUT_THREE_ONE:
    case LAYOUT_TWO_ONE:
      {
        uint8_t num_poly_voices = (settings_.layout == LAYOUT_THREE_ONE) ? 3 : 2;
        part_[0].AllocateVoices(&voice_[0], num_poly_voices, false);
        part_[1].AllocateVoices(&voice_[num_poly_voices], 1, false);
        num_active_parts_ = 2;
      }
      break;
    
    case LAYOUT_TWO_TWO:
      {
        part_[0].AllocateVoices(&voice_[0], 2, false);
        part_[1].AllocateVoices(&voice_[2], 1, false);
        part_[2].AllocateVoices(&voice_[3], 1, false);
        num_active_parts_ = 3;
      }
      break;

    case LAYOUT_PARAPHONIC_PLUS_TWO:
      {
        CONSTRAIN(part_[0].mutable_voicing_settings()->oscillator_mode, OSCILLATOR_MODE_OFF + 1, OSCILLATOR_MODE_LAST - 1);
        part_[0].AllocateVoices(&voice_[0], kNumParaphonicVoices, false);
        part_[1].AllocateVoices(&voice_[kNumParaphonicVoices], 1, false);
        part_[2].AllocateVoices(&voice_[kNumParaphonicVoices + 1], 1, false);
        part_[3].AllocateVoices(&voice_[kNumParaphonicVoices + 2], 1, false);
        num_active_parts_ = 4;
      }
      break;

    case LAYOUT_TRI_MONO:
      num_active_parts_ = 3;
      for (uint8_t i = 0; i < num_active_parts_; ++i) {
        part_[i].AllocateVoices(&voice_[i], 1, false);
      }
      break;

    default:
      break;
  }
  AssignVoicesToCVOutputs();
}

void Multi::ChangeLayout(Layout old_layout, Layout new_layout) {
  AllocateParts();
  for (uint8_t i = 0; i < num_active_parts_; ++i) {
    part_[i].NewLayout();
    part_[i].set_siblings(num_active_parts_ > 1);
  }
  switch (new_layout) {
    case LAYOUT_MONO:
    case LAYOUT_DUAL_MONO:
    case LAYOUT_QUAD_MONO:
    case LAYOUT_QUAD_VOLTAGES:
    case LAYOUT_TRI_MONO:
      {
        // Duplicate uninitialized voices.
        for (uint8_t i = 1; i < num_active_parts_; ++i) {
          uint8_t destination = i;
          uint8_t source = i % num_active_parts_;
          if (destination != source) {
            memcpy(
                part_[destination].mutable_midi_settings(),
                part_[source].mutable_midi_settings(),
                sizeof(MidiSettings));
            memcpy(
                part_[destination].mutable_voicing_settings(),
                part_[source].mutable_voicing_settings(),
                sizeof(VoicingSettings));
            memcpy(
                part_[destination].mutable_sequencer_settings(),
                part_[source].mutable_sequencer_settings(),
                sizeof(SequencerSettings));
          }
        }
      }
      break;

    case LAYOUT_QUAD_TRIGGERS:
      {
        for (uint8_t i = 0; i < num_active_parts_; ++i) {
          MidiSettings* midi = part_[i].mutable_midi_settings();
          if (old_layout != LAYOUT_QUAD_TRIGGERS) {
            midi->min_note = 36 + i * 2;
            midi->max_note = 36 + i * 2;
          }
          midi->channel = part_[0].mutable_midi_settings()->channel;
          midi->out_mode = part_[0].mutable_midi_settings()->out_mode;
        }
        
        // Duplicate sequencer settings.
        for (uint8_t i = 1; i < num_active_parts_; ++i) {
          uint8_t destination = i;
          uint8_t source = i % num_active_parts_;
          if (destination != source) {
            memcpy(
                part_[destination].mutable_sequencer_settings(),
                part_[source].mutable_sequencer_settings(),
                sizeof(SequencerSettings));
          }
        }
      }
      break;

    default:
      break;
  }
  for (uint8_t i = 1; i < num_active_parts_; ++i) {
    part_[i].AfterDeserialize();
  }
}


void Multi::UpdateTempo() {
  internal_clock_.set_tempo(settings_.clock_tempo);
  if (running_) return; // If running, master LFO will get Tap
  // If not running, there's no Tap to update the increment, so do that here
  master_lfo_.SetPhaseIncrement(tick_phase_increment() >> kMasterLFOPeriodTicksBits);
}

void Multi::AfterDeserialize() {
  CONSTRAIN(settings_.control_change_mode, 0, CONTROL_CHANGE_MODE_LAST - 1);

  Stop();
  UpdateTempo();
  AllocateParts();
  
  for (uint8_t i = 0; i < kNumParts; ++i) {
    part_[i].AfterDeserialize();
    macro_record_last_value_[i] = 127;
  }
}

void Multi::StartRecording(uint8_t part) {
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
  if (part_[part].looper_in_use()) {
    // Looper needs a running clock
    Start(false);
  }
  part_[part].StartRecording();
  recording_ = true;
  recording_part_ = part;
}

void Multi::StopRecording(uint8_t part) {
  if (recording_ && recording_part_ == part) {
    part_[part].StopRecording();
    recording_ = false;
    part_[part].set_seq_overwrite(false);
  }
}

bool Multi::ControlChange(uint8_t channel, uint8_t controller, uint8_t value_7bits) {
  bool thru = true;

  int8_t relative_increment;
  switch (settings_.control_change_mode) {
    case CONTROL_CHANGE_MODE_OFF:
      return thru;
    case CONTROL_CHANGE_MODE_RELATIVE_TWOS_COMPLEMENT:
      relative_increment = IncrementFromTwosComplementRelativeCC(value_7bits);
      break;
    case CONTROL_CHANGE_MODE_ABSOLUTE:
    default:
      relative_increment = 0;
      break;
  }

  if (
    is_remote_control_channel(channel) &&
    setting_defs.remote_control_cc_map[controller] != 0xff
  ) {
    // Always thru
    SetFromCC(0xff, controller, value_7bits);
  } else {
    for (uint8_t part_index = 0; part_index < num_active_parts_; ++part_index) {
      if (!part_accepts_channel(part_index, channel)) continue;

      int16_t macro_zone;

      switch (controller) { // Intercept special CCs
      case kCCRecordOffOn: {
        bool start = value_7bits >= 64;
        start ? StartRecording(part_index) : StopRecording(part_index);
        ui.SplashPartString(start ? "R+" : "R-", part_index);
        break;
      }

      case kCCDeleteRecording:
        part_[part_index].DeleteRecording();
        ui.SplashPartString("RX", part_index);
        break;

      case kCCMacroRecord:
        if (relative_increment) {
          if (recording_ && recording_part_ == part_index) {
            macro_zone = part_[part_index].seq_overwrite() ? MACRO_RECORD_OVERWRITE : MACRO_RECORD_ON;
          } else {
            macro_zone = MACRO_RECORD_OFF;
          }
          macro_zone += relative_increment;
          CONSTRAIN(macro_zone, MACRO_RECORD_OFF, MACRO_RECORD_DELETE);
        } else {
          macro_zone = ScaleAbsoluteCC(value_7bits, MACRO_RECORD_OFF, MACRO_RECORD_DELETE);
        }

        macro_zone >= MACRO_RECORD_ON ? StartRecording(part_index) : StopRecording(part_index);
        if (
          macro_zone == MACRO_RECORD_DELETE &&
          // Only on increasing value, so that leaving an absolute controller in
          // the delete zone doesn't doom any subsequent recordings
          (relative_increment || value_7bits > macro_record_last_value_[part_index]))
        {
          part_[part_index].DeleteRecording();
          ui.SplashPartString("RX", part_index);
        } else {
          part_[part_index].set_seq_overwrite(macro_zone == MACRO_RECORD_OVERWRITE);
          ui.SplashPartString(macro_zone == MACRO_RECORD_OVERWRITE ? "R*" : (macro_zone ? "R+" : "R-"), part_index);
        }
        macro_record_last_value_[part_index] = value_7bits;
        break;

      case kCCMacroPlayMode:
        if (relative_increment) {
          macro_zone = part_[part_index].midi_settings().play_mode;
          if (part_[part_index].sequencer_settings().clock_quantization) {
            macro_zone = -macro_zone;
          }
          macro_zone += relative_increment;
          CONSTRAIN(macro_zone, MACRO_PLAY_MODE_STEP_SEQ, MACRO_PLAY_MODE_LOOP_SEQ);
        } else {
          macro_zone = ScaleAbsoluteCC(value_7bits, MACRO_PLAY_MODE_STEP_SEQ, MACRO_PLAY_MODE_LOOP_SEQ);
        }

        ApplySetting(SETTING_SEQUENCER_CLOCK_QUANTIZATION, part_index, macro_zone < MACRO_PLAY_MODE_MANUAL);
        ApplySetting(SETTING_SEQUENCER_PLAY_MODE, part_index, abs(macro_zone));
        char label[2];
        if (macro_zone == MACRO_PLAY_MODE_MANUAL) strcpy(label, "--"); else {
          label[0] = macro_zone < MACRO_PLAY_MODE_MANUAL ? 'S' : 'L';
          label[1] = abs(macro_zone) == 1 ? 'A' : 'S';
        }
        ui.SplashPartString(label, part_index);
        break;

      case kCCLooperPhaseOffset:
        if (part_[part_index].looped()) {
          if (relative_increment) {
            part_->mutable_looper().pos_offset += (relative_increment << 9); // Wraps
          } else {
            part_->mutable_looper().pos_offset = value_7bits << 9;
          }
          ui.SplashOn(SPLASH_LOOPER_PHASE_OFFSET);
        }
        break;

      default:
        thru = thru && part_[part_index].cc_thru();
        part_[part_index].ControlChange(channel, controller, value_7bits);
        SetFromCC(part_index, controller, value_7bits);
        break;

      }
    }
  }
  return thru;
}

int16_t Multi::ScaleAbsoluteCC(uint8_t value_7bits, int16_t min, int16_t max) const {
  int16_t scaled_value;
  uint8_t range = max - min + 1;
  scaled_value = range * value_7bits >> 7;
  scaled_value += min;
  return scaled_value;
}

void Multi::SetFromCC(uint8_t part_index, uint8_t controller, uint8_t value_7bits) {
  uint8_t* map = part_index == 0xff ?
    setting_defs.remote_control_cc_map : setting_defs.part_cc_map;
  uint8_t setting_index = map[controller];
  if (setting_index == 0xff) { return; }
  const Setting& setting = setting_defs.get(setting_index);

  uint8_t part = part_index == 0xff ? controller >> 5 : part_index;
  int16_t raw_value;
  if (settings_.control_change_mode > CONTROL_CHANGE_MODE_ABSOLUTE) {
    raw_value = IncrementSetting(setting, part, IncrementFromTwosComplementRelativeCC(value_7bits));
  } else {
    SettingRange setting_range = GetSettingRange(setting, part);
    raw_value = ScaleAbsoluteCC(value_7bits, setting_range.min, setting_range.max);
  }
  if (setting.unit == SETTING_UNIT_TEMPO) {
    raw_value &= 0xfe;
    if (raw_value < TEMPO_EXTERNAL) {
      raw_value = TEMPO_EXTERNAL;
    }
  }
  ApplySettingAndSplash(setting, part, raw_value);
}

void Multi::ApplySettingAndSplash(const Setting& setting, uint8_t part, int16_t raw_value) {
  ApplySetting(setting, part, raw_value);
  ui.SplashSetting(setting, part);
}

// Determine dynamic min/max for a setting, based on other settings
SettingRange Multi::GetSettingRange(const Setting& setting, uint8_t part) const {
  int16_t min_value = setting.min_value;
  int16_t max_value = setting.max_value;
  if (multi.part(part).num_voices() == 1) { // Part is monophonic
    // if (&setting == &setting_defs.get(SETTING_VOICING_ALLOCATION_MODE))
    //   min_value = max_value = POLY_MODE_OFF;
    if (&setting == &setting_defs.get(SETTING_VOICING_LFO_SPREAD_VOICES))
      min_value = max_value = 0;
  }
  if (
    multi.layout() == LAYOUT_PARAPHONIC_PLUS_TWO &&
    part == 0 &&
    &setting == &setting_defs.get(SETTING_VOICING_OSCILLATOR_MODE)
  ) {
    min_value = OSCILLATOR_MODE_DRONE;
  }
  if (
    part_[part].midi_settings().play_mode == PLAY_MODE_ARPEGGIATOR &&
    !part_[part].seq_has_notes() &&
    &setting == &setting_defs.get(SETTING_SEQUENCER_ARP_PATTERN)
  ) {
    // If no notes are present, sequencer-driven setting values are not allowed
    max_value = LUT_ARPEGGIATOR_PATTERNS_SIZE - 1;
  }
  return SettingRange(min_value, max_value);
}

void Multi::ApplySetting(const Setting& setting, uint8_t part, int16_t raw_value) {
  // Apply dynamic min/max as needed
  SettingRange setting_range = GetSettingRange(setting, part);
  CONSTRAIN(raw_value, setting_range.min, setting_range.max);
  uint8_t value = static_cast<uint8_t>(raw_value);

  uint8_t prev_value = GetSetting(setting, part);
  if (prev_value == value) { return; }

  bool layout = &setting == &setting_defs.get(SETTING_LAYOUT);
  bool sequencer_semantics = \
    &setting == &setting_defs.get(SETTING_SEQUENCER_PLAY_MODE) ||
    &setting == &setting_defs.get(SETTING_SEQUENCER_CLOCK_QUANTIZATION);

  if (running_ && layout) { Stop(); }
  if (recording_ && (
    layout || (recording_part_ == part && sequencer_semantics)
  )) { StopRecording(recording_part_); }
  if (sequencer_semantics) part_[part].AllNotesOff();

  switch (setting.domain) {
    case SETTING_DOMAIN_MULTI:
      multi.Set(setting.address[0], value);
      break;
    case SETTING_DOMAIN_PART:
      // When the module is configured in *triggers* mode, each part is mapped
      // to a single note. To edit this setting, both the "note min" and
      // "note max" parameters are simultaneously changed to the same value.
      // This is a bit more user friendly than letting the user set note min
      // and note max to the same value.
      if (setting.address[1]) {
        multi.mutable_part(part)->Set(setting.address[1], value);
      }
      multi.mutable_part(part)->Set(setting.address[0], value);
      break;

    default:
      break;
  }
}

uint8_t Multi::GetSetting(const Setting& setting, uint8_t part) const {
  uint8_t value = 0;
  switch (setting.domain) {
    case SETTING_DOMAIN_MULTI:
      value = multi.Get(setting.address[0]);
      break;
    case SETTING_DOMAIN_PART:
      value = multi.part(part).Get(setting.address[0]);
      break;
  }
  return value;
}

/* extern */
Multi multi;

}  // namespace yarns
