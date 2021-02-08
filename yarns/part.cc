// Copyright 2013 Emilie Gillet.
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
// Part.

#include "yarns/part.h"

#include <cmath>

#include <algorithm>

#include "stmlib/midi/midi.h"
#include "stmlib/utils/random.h"

#include "yarns/just_intonation_processor.h"
#include "yarns/midi_handler.h"
#include "yarns/resources.h"
#include "yarns/voice.h"
#include "yarns/clock_division.h"
#include "yarns/multi.h"

namespace yarns {
  
using namespace stmlib;
using namespace stmlib_midi;
using namespace std;

void Part::Init() {
  pressed_keys_.Init();
  mono_allocator_.Init();
  poly_allocator_.Init();
  generated_notes_.Init();
  std::fill(
      &active_note_[0],
      &active_note_[kNumMaxVoicesPerPart],
      VOICE_ALLOCATION_NOT_FOUND);
  num_voices_ = 0;
  polychained_ = false;
  ignore_note_off_messages_ = false;
  seq_recording_ = false;
  release_latched_keys_on_next_note_on_ = false;
  transposable_ = true;
  seq_.looper_tape.RemoveAll();
  bar_lfo_.Init();
  LooperRewind();

  midi_.channel = 0;
  midi_.min_note = 0;
  midi_.max_note = 127;
  midi_.min_velocity = 0;
  midi_.max_velocity = 127;
  midi_.out_mode = MIDI_OUT_MODE_GENERATED_EVENTS;
  midi_.sustain_mode = SUSTAIN_MODE_NORMAL;
  midi_.transpose_octaves = 0;

  voicing_.allocation_priority = NOTE_STACK_PRIORITY_LAST;
  voicing_.allocation_mode = VOICE_ALLOCATION_MODE_MONO;
  voicing_.legato_mode = LEGATO_MODE_OFF;
  voicing_.portamento = 0;
  voicing_.pitch_bend_range = 2;
  voicing_.vibrato_range = 1;
  voicing_.vibrato_initial = 0;
  voicing_.vibrato_control_source = VIBRATO_CONTROL_SOURCE_MODWHEEL;
  voicing_.modulation_rate = 50;
  voicing_.trigger_duration = 2;
  voicing_.aux_cv = MOD_AUX_MODULATION;
  voicing_.aux_cv_2 = MOD_AUX_VIBRATO_LFO;
  voicing_.oscillator_pw_initial = 80;
  voicing_.oscillator_pw_mod = 10;
  voicing_.envelope_attack = 40;
  voicing_.envelope_decay = 30;
  voicing_.envelope_sustain = 80;
  voicing_.envelope_release = 105;
  voicing_.tuning_transpose = 0;
  voicing_.tuning_fine = 0;
  voicing_.tuning_root = 0;
  voicing_.tuning_system = TUNING_SYSTEM_EQUAL;
  voicing_.tuning_factor = 0;
  voicing_.audio_mode = AUDIO_MODE_OFF;

  seq_.clock_division = 6; // /4
  seq_.gate_length = 3;
  seq_.arp_range = 0;
  seq_.arp_direction = 0;
  seq_.arp_pattern = 0;
  seq_.input_response = SEQUENCER_INPUT_RESPONSE_TRANSPOSE;
  seq_.play_mode = PLAY_MODE_MANUAL;

  DeleteSequence();
}
  
void Part::AllocateVoices(Voice* voice, uint8_t num_voices, bool polychain) {
  AllNotesOff();
    
  num_voices_ = std::min(num_voices, kNumMaxVoicesPerPart);
  polychained_ = polychain;
  for (uint8_t i = 0; i < num_voices_; ++i) {
    voice_[i] = voice + i;
  }
  poly_allocator_.Clear();
  poly_allocator_.set_size(num_voices_ * (polychain ? 2 : 1));
  TouchVoices();
}

bool Part::NoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
  bool sent_from_step_editor = channel & 0x80;
  
  // scale velocity to compensate for its min/max range, so that voices using
  // velocity filtering can still have a full velocity range
  velocity = (128 * (velocity - midi_.min_velocity)) / (midi_.max_velocity - midi_.min_velocity + 1);

  if (seq_recording_ && !sent_from_step_editor && seq_.play_mode != PLAY_MODE_LOOPER) {
    RecordStep(SequencerStep(note, velocity));
  } else {
    if (release_latched_keys_on_next_note_on_) {
      bool still_latched = ignore_note_off_messages_;

      // Releasing all latched key will generate "fake" NoteOff messages. We
      // should note ignore them.
      ignore_note_off_messages_ = false;
      ReleaseLatchedNotes();
    
      release_latched_keys_on_next_note_on_ = still_latched;
      ignore_note_off_messages_ = still_latched;
    }
    uint8_t pressed_key_index = pressed_keys_.NoteOn(note, velocity);
  
    if (
      seq_.play_mode == PLAY_MODE_MANUAL ||
      sent_from_step_editor ||
      SequencerDirectResponse()
    ) {
      InternalNoteOn(note, velocity);
    } else if (seq_recording_ && seq_.play_mode == PLAY_MODE_LOOPER) {
      uint8_t looper_note_index = seq_.looper_tape.RecordNoteOn(
        this, looper_pos_, note, velocity
      );
      looper_note_index_for_pressed_key_index_[pressed_key_index] = looper_note_index;
      LooperNoteOn(looper_note_index, note, velocity);
    }
  }
  return midi_.out_mode == MIDI_OUT_MODE_THRU && !polychained_;
}

bool Part::NoteOff(uint8_t channel, uint8_t note) {
  bool sent_from_step_editor = channel & 0x80;

  if (ignore_note_off_messages_) {
    for (uint8_t i = 1; i <= pressed_keys_.max_size(); ++i) {
      // Flag the note so that it is removed once the sustain pedal is released.
      NoteEntry* e = pressed_keys_.mutable_note(i);
      if (e->note == note && e->velocity) {
        e->velocity |= 0x80;
      }
    }
  } else {
    uint8_t pressed_key_index = pressed_keys_.Find(note);
    if (pressed_keys_.note(pressed_key_index).velocity & 0x80) {
      // If the note was flagged by a prior NoteOff, this subsequent NoteOff was
      // meant for another part (there is no velocity filter for NoteOff)
      return false;
    }
    pressed_keys_.NoteOff(note);

    if (
      seq_.play_mode == PLAY_MODE_MANUAL ||
      sent_from_step_editor ||
      SequencerDirectResponse()
    ) {
      InternalNoteOff(note);
    } else if (seq_recording_ && seq_.play_mode == PLAY_MODE_LOOPER) {
      uint8_t looper_note_index = looper_note_index_for_pressed_key_index_[pressed_key_index];
      looper_note_index_for_pressed_key_index_[pressed_key_index] = looper::kNullIndex;
      if (
        looper_note_index != looper::kNullIndex &&
        seq_.looper_tape.RecordNoteOff(looper_pos_, looper_note_index)
      ) {
        LooperNoteOff(looper_note_index, note);
      }
    }
  }
  return midi_.out_mode == MIDI_OUT_MODE_THRU && !polychained_;
}

bool Part::ControlChange(uint8_t channel, uint8_t controller, uint8_t value) {
  switch (controller) {
    case kCCModulationWheelMsb:
    case kCCBreathController:
    case kCCFootPedalMsb:
      for (uint8_t i = 0; i < num_voices_; ++i) {
        voice_[i]->ControlChange(controller, value);
      }
      break;
      
    case kCCOmniModeOff:
      midi_.channel = channel;
      break;
      
    case kCCOmniModeOn:
      midi_.channel = 0x10;
      break;
      
    case kCCMonoModeOn:
      voicing_.allocation_mode = VOICE_ALLOCATION_MODE_MONO;
      TouchVoiceAllocation();
      break;
      
    case kCCPolyModeOn:
      voicing_.allocation_mode = VOICE_ALLOCATION_MODE_POLY;
      TouchVoiceAllocation();
      break;
      
    case kCCHoldPedal:
      {
        if (value >= 64) {
          switch (midi_.sustain_mode) {
            case SUSTAIN_MODE_NORMAL:
              ignore_note_off_messages_ = true;
              break;
            /*
            case SUSTAIN_MODE_SOSTENUTO:
              for (uint8_t i = 1; i <= pressed_keys_.max_size(); ++i) {
                // Flag the note so that it is removed once the sustain pedal is released.
                NoteEntry* e = pressed_keys_.mutable_note(i);
                e->velocity |= 0x80;
              }
              break;
            */
            case SUSTAIN_MODE_LATCH:
            case SUSTAIN_MODE_MOMENTARY_LATCH:
              Latch();
              break;
            case SUSTAIN_MODE_OFF:
            default:
              break;
          }
        } else {
          switch (midi_.sustain_mode) {
            case SUSTAIN_MODE_NORMAL:
              ignore_note_off_messages_ = false;
              ReleaseLatchedNotes();
              break;
            /*
            case SUSTAIN_MODE_SOSTENUTO:
              // TODO this is busted -- these notes could still be held
              ReleaseLatchedNotes();
              break;
            */
            case SUSTAIN_MODE_LATCH:
              UnlatchOnNextNoteOn();
              break;
            case SUSTAIN_MODE_MOMENTARY_LATCH:
              UnlatchImmediate();
            case SUSTAIN_MODE_OFF:
            default:
              break;
          }
        }
      }
      break;
    
    case 0x70:
      if (seq_recording_) {
        RecordStep(SEQUENCER_STEP_TIE);
      }
      break;
    
    case 0x71:
      if (seq_recording_) {
        RecordStep(SEQUENCER_STEP_REST);
      }
      break;
    
    case 0x78:
      AllNotesOff();
      break;
      
    case 0x79:
      ResetAllControllers();
      break;
      
    case 0x7b:
      AllNotesOff();
      break;
  }
  return midi_.out_mode != MIDI_OUT_MODE_OFF;
}

bool Part::PitchBend(uint8_t channel, uint16_t pitch_bend) {
  for (uint8_t i = 0; i < num_voices_; ++i) {
    voice_[i]->PitchBend(pitch_bend);
  }
  
  if (seq_recording_ &&
      (pitch_bend > 8192 + 2048 || pitch_bend < 8192 - 2048)) {
    seq_.step[seq_rec_step_].data[1] |= 0x80;
  }
  
  return midi_.out_mode != MIDI_OUT_MODE_OFF;
}

bool Part::Aftertouch(uint8_t channel, uint8_t note, uint8_t velocity) {
  if (voicing_.allocation_mode != VOICE_ALLOCATION_MODE_MONO) {
    uint8_t voice_index = \
        voicing_.allocation_mode == VOICE_ALLOCATION_MODE_POLY ? \
        poly_allocator_.Find(note) : \
        FindVoiceForNote(note);
    if (voice_index < poly_allocator_.size()) {
      voice_[voice_index]->Aftertouch(velocity);
    }
  } else {
    Aftertouch(channel, velocity);
  }
  return midi_.out_mode != MIDI_OUT_MODE_OFF;
}

bool Part::Aftertouch(uint8_t channel, uint8_t velocity) {
  for (uint8_t i = 0; i < num_voices_; ++i) {
    voice_[i]->Aftertouch(velocity);
  }
  return midi_.out_mode != MIDI_OUT_MODE_OFF;
}

void Part::Reset() {
  Stop();
  for (uint8_t i = 0; i < num_voices_; ++i) {
    voice_[i]->NoteOff();
    voice_[i]->ResetAllControllers();
  }
}

void Part::Clock() {
  if (!arp_seq_prescaler_) {
    uint32_t step_mask = 1 << stmlib::modulo(euclidean_step_index_ + seq_.euclidean_rotate, seq_.euclidean_length);
    // Read euclidean pattern from ROM.
    uint16_t offset = static_cast<uint16_t>(seq_.euclidean_length - 1) * 32;
    if (step_mask & lut_euclidean[offset + seq_.euclidean_fill]) {
      if (seq_.play_mode == PLAY_MODE_ARPEGGIATOR) {
        ClockArpeggiator();
      } else if (seq_.play_mode == PLAY_MODE_SEQUENCER) {
        ClockSequencer();
      }
    }
    ++euclidean_step_index_;
    if (euclidean_step_index_ >= seq_.euclidean_length) {
      euclidean_step_index_ = 0;
    }
  }
  
  if (!gate_length_counter_ && generated_notes_.size()) {
    StopSequencerArpeggiatorNotes();
  } else {
    --gate_length_counter_;
  }
  
  ++arp_seq_prescaler_;
  if (arp_seq_prescaler_ >= clock_division::num_ticks[seq_.clock_division]) {
    arp_seq_prescaler_ = 0;
  }
  
  uint16_t num_ticks;

  if (voicing_.modulation_rate >= 100) {
    num_ticks = clock_division::num_ticks[voicing_.modulation_rate - 100];
    // TODO decipher this math -- how much to add so that the voices are in quadrature?
    for (uint8_t i = 0; i < num_voices_; ++i) {
      voice_[i]->synced_lfo_.Tap(num_ticks);
    }
  }

  // looper
  num_ticks = clock_division::num_ticks[seq_.clock_division];
  uint8_t bar_duration = multi.settings().clock_bar_duration;
  bar_lfo_.Tap(num_ticks * (bar_duration ? bar_duration : 1));
}

void Part::Start() {
  arp_seq_prescaler_ = 0;

  seq_step_ = 0;
  
  release_latched_keys_on_next_note_on_ = false;
  ignore_note_off_messages_ = false;
  
  euclidean_step_index_ = 0;
  arp_.key_index = 0;
  arp_.octave = 0;
  arp_.key_increment = 1;
  arp_.step_index = 0;
  
  bar_lfo_.Init();
  LooperRewind();

  generated_notes_.Clear();
}

void Part::LooperRewind() {
  looper_pos_ = 0;
  looper_needs_advance_ = false;
  seq_.looper_tape.ResetHead();
  std::fill(
    &looper_note_index_for_pressed_key_index_[0],
    &looper_note_index_for_pressed_key_index_[kNoteStackSize],
    looper::kNullIndex
  );
}

void Part::LooperAdvance() {
  if (!looper_needs_advance_) {
    return;
  }
  uint16_t new_pos = bar_lfo_.GetPhase() >> 16;
  seq_.looper_tape.Advance(
    this, seq_.play_mode == PLAY_MODE_LOOPER, looper_pos_, new_pos
  );
  looper_pos_ = new_pos;
  looper_needs_advance_ = false;
}

void Part::Stop() {
  StopSequencerArpeggiatorNotes();
  AllNotesOff();
}

void Part::StartRecording() {
  if (seq_recording_) {
    return;
  }
  seq_recording_ = true;
  if (seq_.play_mode != PLAY_MODE_LOOPER) {
    seq_rec_step_ = 0;
    seq_overdubbing_ = seq_.num_steps > 0;
  }
}

void Part::DeleteSequence() {
  StopRecording();
  std::fill(
    &seq_.step[0],
    &seq_.step[kNumSteps],
    SequencerStep(SEQUENCER_STEP_REST, 0)
  );
  seq_rec_step_ = 0;
  seq_.num_steps = 0;
}

void Part::StopSequencerArpeggiatorNotes() {
  while (generated_notes_.size()) {
    uint8_t note = generated_notes_.sorted_note(0).note;
    generated_notes_.NoteOff(note);
    InternalNoteOff(note);
  }
}

void Part::ClockSequencer() {
  const SequencerStep& step = seq_.step[seq_step_];

  if (step.has_note()) {
    int16_t note = step.note();
    if (pressed_keys_.size() && transposable_) {
      switch (seq_.input_response) {
        case SEQUENCER_INPUT_RESPONSE_TRANSPOSE:
          {
          // When we play a monophonic sequence, we can make the guess that root
          // note = first note.
          // But this is not the case when we are playing several sequences at the
          // same time. In this case, we use root note = 60.
          int8_t root_note = !has_siblings_ ? seq_.first_note() : 60;
          note += pressed_keys_.most_recent_note().note - root_note;
          }
          while (note > 127) {
            note -= 12;
          }
          while (note < 0) {
            note += 12;
          }
          break;

        case SEQUENCER_INPUT_RESPONSE_OVERRIDE:
          note = pressed_keys_.most_recent_note().note;
          break;

        case SEQUENCER_INPUT_RESPONSE_DIRECT:
        case SEQUENCER_INPUT_RESPONSE_OFF:
          break;
      }
    }
    if (!step.is_slid()) {
      StopSequencerArpeggiatorNotes();
      InternalNoteOn(note, step.velocity());
    } else {
      InternalNoteOn(note, step.velocity());
      StopSequencerArpeggiatorNotes();
    }
    generated_notes_.NoteOn(note, step.velocity());
    gate_length_counter_ = seq_.gate_length;
  }
  ++seq_step_;
  if (seq_step_ >= seq_.num_steps) {
    seq_step_ = 0;
  }
  if (seq_.step[seq_step_].is_tie() || seq_.step[seq_step_].is_slid()) {
    // The next step contains a "sustain" message; or a slid note. Extends
    // the duration of the current note.
    gate_length_counter_ += clock_division::num_ticks[seq_.clock_division];
  }
}

void Part::ClockArpeggiator() {
  uint32_t pattern_mask;
  uint32_t pattern;
  uint32_t pattern_length;
  if (
    seq_.arp_direction == ARPEGGIATOR_DIRECTION_SEQUENCER_ALL ||
    seq_.arp_direction == ARPEGGIATOR_DIRECTION_SEQUENCER_REST ||
    seq_.arp_direction == ARPEGGIATOR_DIRECTION_SEQUENCER_WRAP
  ) {
    pattern_length = seq_.num_steps;
    seq_step_ = arp_.step_index;
    if (seq_.step[arp_.step_index].has_note()) {
      ArpeggiatorNoteOn();
    }
  } else {
    pattern_mask = 1 << arp_.step_index;
    pattern = lut_arpeggiator_patterns[seq_.arp_pattern];
    pattern_length = 16;
    if (pattern_mask & pattern) {
      ArpeggiatorNoteOn();
    }
  }
  
  ++arp_.step_index;
  if (arp_.step_index >= pattern_length) {
    arp_.step_index = 0;
  }
}

void Part::ArpeggiatorNoteOn() {
  uint8_t num_keys = pressed_keys_.size();
  if (!num_keys) {
    return;
  }
  // Update arepggiator note/octave counter.
  switch (seq_.arp_direction) {
    case ARPEGGIATOR_DIRECTION_RANDOM:
      {
        uint16_t random = Random::GetSample();
        arp_.octave = (random & 0xff) % seq_.arp_range;
        arp_.key_index = (random >> 8) % num_keys;
      }
      break;
    case ARPEGGIATOR_DIRECTION_SEQUENCER_ALL:
    case ARPEGGIATOR_DIRECTION_SEQUENCER_REST:
    case ARPEGGIATOR_DIRECTION_SEQUENCER_WRAP:
      {
        seq_step_ = arp_.step_index;
        if (!seq_.step[arp_.step_index].has_note()) {
          return;
        }
        // TODO respect arp range?
        SequencerStep step = seq_.step[arp_.step_index];
        if (seq_.arp_direction == ARPEGGIATOR_DIRECTION_SEQUENCER_WRAP) {
          // Movement instructions derived from sequence step
          uint8_t limit = step.octave();
          uint8_t clock;
          uint8_t spacer;
          if (step.is_white()) {
            clock = step.white_key_value(); // TODO is 0 clock useful?
            spacer = 1;
          } else {
            clock = 1;
            spacer = step.black_key_value() + 1; // TODO is 0 spacer useful?
          }
          uint8_t old_pos = modulo(arp_.key_index / spacer, limit);
          uint8_t new_pos = modulo(old_pos + clock, limit);
          uint8_t increment_without_wrap = spacer * (new_pos - old_pos);
          if ((arp_.key_index + increment_without_wrap) < num_keys) {
            arp_.key_index += increment_without_wrap;
          } else {
            arp_.key_index -= spacer * old_pos;
          }
        } else {
          uint8_t new_arp_note;
          if (step.is_white()) {
            new_arp_note = arp_.key_index + step.white_key_distance_from_middle_c();
          } else {
            new_arp_note = step.black_key_distance_from_middle_c();
          }
          arp_.key_index = stmlib::modulo(new_arp_note, num_keys);
          if (arp_.key_index != new_arp_note && seq_.arp_direction == ARPEGGIATOR_DIRECTION_SEQUENCER_REST) {
            // if we moved out of bounds, rest this step
            return;
          }
        }
        arp_.key_increment = 0; // these arp directions move before playing the note
      }
      break;
    default:
      {
        if (num_keys == 1 && seq_.arp_range == 1) {
          // This is a corner case for the Up/down pattern code.
          // Get it out of the way.
          arp_.key_index = 0;
          arp_.octave = 0;
        } else {
          bool wrapped = true;
          while (wrapped) {
            if (arp_.key_index >= num_keys || arp_.key_index < 0) {
              arp_.octave += arp_.key_increment;
              arp_.key_index = arp_.key_increment > 0 ? 0 : num_keys - 1;
            }
            wrapped = false;
            if (arp_.octave >= seq_.arp_range || arp_.octave < 0) {
              arp_.octave = arp_.key_increment > 0 ? 0 : seq_.arp_range - 1;
              if (seq_.arp_direction == ARPEGGIATOR_DIRECTION_UP_DOWN) {
                arp_.key_increment = -arp_.key_increment;
                arp_.key_index = arp_.key_increment > 0 ? 1 : num_keys - 2;
                arp_.octave = arp_.key_increment > 0 ? 0 : seq_.arp_range - 1;
                wrapped = true;
              }
            }
          }
        }
      }
      break;
  }
    
  // Kill pending notes (if any).
  StopSequencerArpeggiatorNotes();
    
  // Trigger arpeggiated note or chord.
  if (seq_.arp_direction != ARPEGGIATOR_DIRECTION_CHORD) {
    const NoteEntry* arpeggio_note = &pressed_keys_.note_by_priority(
      static_cast<NoteStackFlags>(voicing_.allocation_priority),
      arp_.key_index
    );
    uint8_t note = arpeggio_note->note;
    uint8_t velocity = arpeggio_note->velocity & 0x7f;
    if (
      seq_.arp_direction == ARPEGGIATOR_DIRECTION_SEQUENCER_ALL ||
      seq_.arp_direction == ARPEGGIATOR_DIRECTION_SEQUENCER_REST
    ) {
      velocity = (velocity * seq_.step[arp_.step_index].velocity()) >> 7;
    }
    note += 12 * arp_.octave;
    while (note > 127) {
      note -= 12;
    }
    generated_notes_.NoteOn(note, velocity);
    InternalNoteOn(note, velocity);
  } else {
    for (uint8_t i = 0; i < num_keys; ++i) {
      const NoteEntry& chord_note = pressed_keys_.played_note(i);
      generated_notes_.NoteOn(chord_note.note, chord_note.velocity & 0x7f);
      InternalNoteOn(chord_note.note, chord_note.velocity & 0x7f);
    }
  }
  arp_.key_index += arp_.key_increment;
  gate_length_counter_ = seq_.gate_length;
}

void Part::ResetAllControllers() {
  ignore_note_off_messages_ = false;
  for (uint8_t i = 0; i < num_voices_; ++i) {
    voice_[i]->ResetAllControllers();
  }
}

void Part::AllNotesOff() {
  poly_allocator_.ClearNotes();
  mono_allocator_.Clear();
  pressed_keys_.Clear();
  generated_notes_.Clear();
  looper_note_index_for_generated_note_index_[generated_notes_.most_recent_note_index()] = looper::kNullIndex;
  for (uint8_t i = 0; i < num_voices_; ++i) {
    voice_[i]->NoteOff();
  }
  std::fill(
      &active_note_[0],
      &active_note_[kNumMaxVoicesPerPart],
      VOICE_ALLOCATION_NOT_FOUND);
  release_latched_keys_on_next_note_on_ = false;
  ignore_note_off_messages_ = false;
}

void Part::ReleaseLatchedNotes() {
  for (uint8_t i = 1; i <= pressed_keys_.max_size(); ++i) {
    NoteEntry* e = pressed_keys_.mutable_note(i);
    if (e->velocity & 0x80) {
      e->velocity &= ~0x80; // Un-flag the note
      NoteOff(tx_channel(), e->note);
    }
  }
}

void Part::DispatchSortedNotes(bool unison) {
  uint8_t n = mono_allocator_.size();
  for (uint8_t i = 0; i < num_voices_; ++i) {
    uint8_t index = 0xff;
    if (unison && n < num_voices_) {
      // distribute extra voices evenly among notes
      index = n ? (i * n / num_voices_) : 0xff;
    } else {
      index = i < mono_allocator_.size() ? i : 0xff;
    }
    if (index != 0xff) {
      const NoteEntry& note_entry = priority_note(index);
      voice_[i]->NoteOn(
          Tune(note_entry.note),
          note_entry.velocity,
          voicing_.portamento,
          active_note_[i] != note_entry.note && voicing_.legato_mode == LEGATO_MODE_OFF
      );
      active_note_[i] = note_entry.note;
    } else {
      voice_[i]->NoteOff();
      active_note_[i] = VOICE_ALLOCATION_NOT_FOUND;
    }
  }
}

void Part::InternalNoteOn(uint8_t note, uint8_t velocity) {
  if (midi_.out_mode == MIDI_OUT_MODE_GENERATED_EVENTS && !polychained_) {
    midi_handler.OnInternalNoteOn(tx_channel(), note, velocity);
  }
  
  const NoteEntry& before = priority_note();
  mono_allocator_.NoteOn(note, velocity);
  const NoteEntry& after = priority_note();
  if (voicing_.allocation_mode == VOICE_ALLOCATION_MODE_MONO) {
    // Check if the note that has been played should be triggered according
    // to selected voice priority rules.
    if (before.note != after.note) {
      bool legato = mono_allocator_.size() > 1;
      for (uint8_t i = 0; i < num_voices_; ++i) {
        voice_[i]->NoteOn(
            Tune(after.note),
            after.velocity,
            (voicing_.legato_mode == LEGATO_MODE_AUTO_PORTAMENTO) && !legato ? 0 : voicing_.portamento,
            (voicing_.legato_mode == LEGATO_MODE_OFF) || !legato);
      }
    }
  } else if (voicing_.allocation_mode == VOICE_ALLOCATION_MODE_POLY_SORTED ||
             voicing_.allocation_mode == VOICE_ALLOCATION_MODE_POLY_UNISON_1 ||
             voicing_.allocation_mode == VOICE_ALLOCATION_MODE_POLY_UNISON_2) {
    DispatchSortedNotes(
        voicing_.allocation_mode != VOICE_ALLOCATION_MODE_POLY_SORTED);
  } else {
    uint8_t voice_index = 0;
    switch (voicing_.allocation_mode) {
      case VOICE_ALLOCATION_MODE_POLY:
        voice_index = poly_allocator_.NoteOn(note, VOICE_STEALING_MODE_LRU);
        break;
      
      case VOICE_ALLOCATION_MODE_POLY_STEAL_MOST_RECENT:
        voice_index = poly_allocator_.NoteOn(note, VOICE_STEALING_MODE_MRU);
        break;
        
      case VOICE_ALLOCATION_MODE_POLY_CYCLIC:
        if (cyclic_allocation_note_counter_ >= num_voices_) {
          cyclic_allocation_note_counter_ = 0;
        }
        voice_index = cyclic_allocation_note_counter_;
        ++cyclic_allocation_note_counter_;
        break;
      
      case VOICE_ALLOCATION_MODE_POLY_RANDOM:
        voice_index = (Random::GetWord() >> 24) % num_voices_;
        break;
        
      case VOICE_ALLOCATION_MODE_POLY_VELOCITY:
        voice_index = (static_cast<uint16_t>(velocity) * num_voices_) >> 7;
        break;
        
      default:
        break;
    }
    
    if (voice_index < num_voices_) {
      // Prevent the same note from being simultaneously played on two channels.
      KillAllInstancesOfNote(note);
      voice_[voice_index]->NoteOn(
          Tune(note),
          velocity,
          voicing_.portamento,
          true);
      active_note_[voice_index] = note;
    } else {
      // Polychaining forwarding.
      midi_handler.OnInternalNoteOn(tx_channel(), note, velocity);
    }
  }
}

void Part::KillAllInstancesOfNote(uint8_t note) {
  while (true) {
    uint8_t index = FindVoiceForNote(note);
    if (index != VOICE_ALLOCATION_NOT_FOUND) {
      voice_[index]->NoteOff();
      active_note_[index] = VOICE_ALLOCATION_NOT_FOUND;
    } else {
      break;
    }
  }
}

void Part::InternalNoteOff(uint8_t note) {
  if (midi_.out_mode == MIDI_OUT_MODE_GENERATED_EVENTS && !polychained_) {
    midi_handler.OnInternalNoteOff(tx_channel(), note);
  }
  
  if (voicing_.tuning_system == TUNING_SYSTEM_JUST_INTONATION) {
    just_intonation_processor.NoteOff(note);
  }
  
  const NoteEntry& before = priority_note();
  mono_allocator_.NoteOff(note);
  const NoteEntry& after = priority_note();
  if (voicing_.allocation_mode == VOICE_ALLOCATION_MODE_MONO) {
    if (mono_allocator_.size() == 0) {
      // No key is pressed, we just close the gate.
      for (uint8_t i = 0; i < num_voices_; ++i) {
        voice_[i]->NoteOff();
      }
    } else if (before.note != after.note) {
      // Removing the note gives priority to another note that is still being
      // pressed. Slide to this note (or retrigger is legato mode is off).
      for (uint8_t i = 0; i < num_voices_; ++i) {
        voice_[i]->NoteOn(
            Tune(after.note),
            after.velocity,
            voicing_.portamento,
            voicing_.legato_mode == LEGATO_MODE_OFF);
      }
    }
  } else if (voicing_.allocation_mode == VOICE_ALLOCATION_MODE_POLY_SORTED ||
             voicing_.allocation_mode == VOICE_ALLOCATION_MODE_POLY_UNISON_1 ||
             voicing_.allocation_mode == VOICE_ALLOCATION_MODE_POLY_UNISON_2) {
    KillAllInstancesOfNote(note);
    if (
        voicing_.allocation_mode == VOICE_ALLOCATION_MODE_POLY_UNISON_1 ||
        mono_allocator_.size() >= num_voices_
    ) {
      DispatchSortedNotes(true);
    }
  } else {
    uint8_t voice_index = \
        voicing_.allocation_mode == VOICE_ALLOCATION_MODE_POLY ? \
        poly_allocator_.NoteOff(note) : \
        FindVoiceForNote(note);
    if (voice_index < num_voices_) {
      voice_[voice_index]->NoteOff();
      active_note_[voice_index] = VOICE_ALLOCATION_NOT_FOUND;
    } else {
       midi_handler.OnInternalNoteOff(tx_channel(), note);
    }
  }
}

void Part::TouchVoiceAllocation() {
  AllNotesOff();
  ResetAllControllers();
}

void Part::TouchVoices() {
  CONSTRAIN(voicing_.aux_cv, 0, MOD_AUX_LAST - 1);
  CONSTRAIN(voicing_.aux_cv_2, 0, MOD_AUX_LAST - 1);
  for (uint8_t i = 0; i < num_voices_; ++i) {
    voice_[i]->set_pitch_bend_range(voicing_.pitch_bend_range);
    voice_[i]->set_modulation_rate(voicing_.modulation_rate * pow(1.123, (int) i));
    voice_[i]->set_vibrato_range(voicing_.vibrato_range);
    voice_[i]->set_vibrato_initial(voicing_.vibrato_initial);
    voice_[i]->set_vibrato_control_source(voicing_.vibrato_control_source);
    voice_[i]->set_trigger_duration(voicing_.trigger_duration);
    voice_[i]->set_trigger_scale(voicing_.trigger_scale);
    voice_[i]->set_trigger_shape(voicing_.trigger_shape);
    voice_[i]->set_aux_cv(voicing_.aux_cv);
    voice_[i]->set_aux_cv_2(voicing_.aux_cv_2);
    voice_[i]->set_audio_mode(voicing_.audio_mode);
    voice_[i]->set_tuning(voicing_.tuning_transpose, voicing_.tuning_fine);
    voice_[i]->set_oscillator_pw_initial(voicing_.oscillator_pw_initial);
    voice_[i]->set_oscillator_pw_mod(voicing_.oscillator_pw_mod);
    voice_[i]->envelope()->SetADSR(
        voicing_.envelope_attack,
        voicing_.envelope_decay,
        voicing_.envelope_sustain,
        voicing_.envelope_release
    );
  }
}

void Part::Set(uint8_t address, uint8_t value) {
  uint8_t* bytes;
  bytes = static_cast<uint8_t*>(static_cast<void*>(&midi_));
  uint8_t previous_value = bytes[address];
  bytes[address] = value;
  if (value != previous_value) {
    switch (address) {
      case PART_MIDI_CHANNEL:
      case PART_MIDI_MIN_NOTE:
      case PART_MIDI_MAX_NOTE:
      case PART_MIDI_MIN_VELOCITY:
      case PART_MIDI_MAX_VELOCITY:
      case PART_SEQUENCER_INPUT_RESPONSE:
      case PART_SEQUENCER_PLAY_MODE:
        // Shut all channels off when a MIDI parameter is changed to prevent
        // stuck notes.
        AllNotesOff();
        break;
        
      case PART_VOICING_ALLOCATION_MODE:
        TouchVoiceAllocation();
        break;
        
      case PART_VOICING_PITCH_BEND_RANGE:
      case PART_VOICING_MODULATION_RATE:
      case PART_VOICING_VIBRATO_RANGE:
      case PART_VOICING_VIBRATO_INITIAL:
      case PART_VOICING_VIBRATO_CONTROL_SOURCE:
      case PART_VOICING_TRIGGER_DURATION:
      case PART_VOICING_TRIGGER_SHAPE:
      case PART_VOICING_TRIGGER_SCALE:
      case PART_VOICING_AUX_CV:
      case PART_VOICING_AUX_CV_2:
      case PART_VOICING_AUDIO_MODE:
      case PART_VOICING_OSCILLATOR_PW_INITIAL:
      case PART_VOICING_OSCILLATOR_PW_MOD:
      case PART_VOICING_ENVELOPE_ATTACK:
      case PART_VOICING_ENVELOPE_DECAY:
      case PART_VOICING_ENVELOPE_SUSTAIN:
      case PART_VOICING_ENVELOPE_RELEASE:
      case PART_VOICING_TUNING_TRANSPOSE:
      case PART_VOICING_TUNING_FINE:
        TouchVoices();
        break;
        
      case PART_SEQUENCER_ARP_DIRECTION:
        arp_.key_increment = 1;
        break;

      case PART_MIDI_SUSTAIN_MODE:
      case PART_MIDI_TRANSPOSE_OCTAVES:
        break;
    }
  }
}

struct Ratio { int p; int q; };

const Ratio ratio_table[] = {
  { 1, 1 },
  { 0, 1 },
  { 1, 8 },
  { 1, 4 },
  { 3, 8 },
  { 1, 2 },
  { 5, 8 },
  { 3, 4 },
  { 7, 8 },
  { 1, 1 },
  { 5, 4 },
  { 3, 2 },
  { 2, 1 },
  { 51095, 65536 }
};

int16_t Part::Tune(int16_t midi_note) {
  int16_t note = midi_note;
  int16_t pitch = note << 7;
  uint8_t pitch_class = (note + 240) % 12;

  // Just intonation.
  if (voicing_.tuning_system == TUNING_SYSTEM_JUST_INTONATION) {
    pitch = just_intonation_processor.NoteOn(note);
  } else if (voicing_.tuning_system == TUNING_SYSTEM_CUSTOM) {
    pitch += custom_pitch_table_[pitch_class];
  } else if (voicing_.tuning_system > TUNING_SYSTEM_JUST_INTONATION) {
    note -= voicing_.tuning_root;
    pitch_class = (note + 240) % 12;
    pitch += lookup_table_signed_table[LUT_SCALE_PYTHAGOREAN + \
        voicing_.tuning_system - TUNING_SYSTEM_PYTHAGOREAN][pitch_class];
  }
  
  int32_t root = (static_cast<int32_t>(voicing_.tuning_root) + 60) << 7;
  int32_t scaled_pitch = static_cast<int32_t>(pitch);
  scaled_pitch -= root;
  Ratio r = ratio_table[voicing_.tuning_factor];
  scaled_pitch = scaled_pitch * r.p / r.q;
  scaled_pitch += root;
  CONSTRAIN(scaled_pitch, 0, 16383);
  return static_cast<int16_t>(scaled_pitch);
}

}  // namespace yarns
