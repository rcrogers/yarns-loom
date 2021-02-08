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
// Part.

#include "yarns/part.h"

#include <algorithm>

#include "stmlib/midi/midi.h"
#include "stmlib/utils/random.h"

#include "yarns/just_intonation_processor.h"
#include "yarns/midi_handler.h"
#include "yarns/resources.h"
#include "yarns/voice.h"
#include "yarns/clock_division.h"
#include "yarns/multi.h"
#include "yarns/ui.h"

namespace yarns {
  
using namespace stmlib;
using namespace stmlib_midi;
using namespace std;

void Part::Init() {
  manual_keys_.Init();
  arp_keys_.Init();
  mono_allocator_.Init();
  poly_allocator_.Init();
  generated_notes_.Init();
  std::fill(
      &active_note_[0],
      &active_note_[kNumMaxVoicesPerPart],
      VOICE_ALLOCATION_NOT_FOUND);
  num_voices_ = 0;
  polychained_ = false;
  seq_recording_ = false;
  transposable_ = true;

  looper_.Init(this);

  midi_.channel = 0;
  midi_.min_note = 0;
  midi_.max_note = 127;
  midi_.min_velocity = 0;
  midi_.max_velocity = 127;
  midi_.out_mode = MIDI_OUT_MODE_GENERATED_EVENTS;
  midi_.sustain_mode = SUSTAIN_MODE_LATCH;
  midi_.sustain_polarity = 0;
  midi_.transpose_octaves = 0;

  voicing_.allocation_priority = NOTE_STACK_PRIORITY_LAST;
  voicing_.allocation_mode = VOICE_ALLOCATION_MODE_MONO;
  voicing_.legato_mode = LEGATO_MODE_OFF;
  voicing_.portamento = 0;
  voicing_.pitch_bend_range = 2;
  voicing_.vibrato_range = 1;
  voicing_.vibrato_initial = 5;
  voicing_.modulation_rate = 50;
  voicing_.trigger_duration = 2;
  voicing_.aux_cv = MOD_AUX_MODULATION;
  voicing_.aux_cv_2 = MOD_AUX_VIBRATO_LFO;
  voicing_.oscillator_pw_initial = 40;
  voicing_.oscillator_pw_mod = 5;
  voicing_.tuning_transpose = 0;
  voicing_.tuning_fine = 0;
  voicing_.tuning_root = 0;
  voicing_.tuning_system = TUNING_SYSTEM_EQUAL;
  voicing_.tuning_factor = 0;
  voicing_.oscillator_mode = OSCILLATOR_MODE_OFF;
  voicing_.oscillator_shape = OSCILLATOR_SHAPE_PULSE_VARIABLE;

  voicing_.envelope_amplitude_init = 16;
  voicing_.envelope_amplitude_mod = 24;
  voicing_.env_init_attack = 30;
  voicing_.env_init_decay = 20;
  voicing_.env_init_sustain = 40;
  voicing_.env_init_release = 40;
  voicing_.env_mod_attack = -16;
  voicing_.env_mod_decay = -8;
  voicing_.env_mod_sustain = 8;
  voicing_.env_mod_release = 16;

  seq_.clock_division = clock_division::unity;
  seq_.gate_length = 3;
  seq_.arp_range = 0;
  seq_.arp_direction = 0;
  seq_.arp_pattern = 1;
  midi_.input_response = SEQUENCER_INPUT_RESPONSE_TRANSPOSE;
  midi_.play_mode = PLAY_MODE_MANUAL;
  seq_.clock_quantization = 0;
  seq_.loop_length = 2; // 1 bar

  StopRecording();
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

uint8_t Part::PressedKeysNoteOn(PressedKeys &keys, uint8_t pitch, uint8_t velocity) {
  if (keys.release_latched_keys_on_next_note_on) {
    bool still_latched = keys.ignore_note_off_messages;

    // Releasing all latched key will generate "fake" NoteOff messages. We
    // should not ignore them.
    keys.ignore_note_off_messages = false;
    ReleaseLatchedNotes(keys);

    keys.release_latched_keys_on_next_note_on = still_latched;
    keys.ignore_note_off_messages = still_latched;
  }
  bool sustained = keys.IsSustained(pitch);
  uint8_t index = keys.stack.NoteOn(pitch, velocity);
  if (sustained) { keys.SetSustain(pitch); }
  return index;
}

bool Part::NoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
  bool sent_from_step_editor = channel & 0x80;
  
  // scale velocity to compensate for its min/max range, so that voices using
  // velocity filtering can still have a full velocity range
  velocity = ((velocity - midi_.min_velocity) << 7) / (midi_.max_velocity - midi_.min_velocity + 1);

  if (seq_recording_) {
    note = ArpUndoTransposeInputPitch(note);
    if (!looped() && !sent_from_step_editor) {
      RecordStep(SequencerStep(note, velocity));
    } else if (looped()) {
      uint8_t pressed_key_index = PressedKeysNoteOn(manual_keys_, note, velocity);
      LooperRecordNoteOn(pressed_key_index);
    }
  } else if (midi_.play_mode == PLAY_MODE_ARPEGGIATOR) {
    PressedKeysNoteOn(arp_keys_, note, velocity);
  } else {
    PressedKeysNoteOn(manual_keys_, note, velocity);
    if (sent_from_step_editor || manual_control()) {
      InternalNoteOn(note, velocity);
    }
  }

  return midi_.out_mode == MIDI_OUT_MODE_THRU && !polychained_;
}

bool Part::NoteOff(uint8_t channel, uint8_t note) {
  bool sent_from_step_editor = channel & 0x80;

  uint8_t recording_pitch = ArpUndoTransposeInputPitch(note);
  uint8_t pressed_key_index = manual_keys_.stack.Find(recording_pitch);
  if (seq_recording_ && looped() && looper_is_recording(pressed_key_index)) {
    // Looper interlaces some operations here, because the manual key has to
    // exist until LooperRecordNoteOff returns.  Directly mapping pitch to
    // looper notes would be cleaner, but requires a data structure more
    // sophisticated than an array
    manual_keys_.SetSustain(recording_pitch);
    if (!manual_keys_.IsSustained(recording_pitch)) {
      LooperRecordNoteOff(pressed_key_index);
      manual_keys_.stack.NoteOff(recording_pitch);
    }
  } else if (midi_.play_mode == PLAY_MODE_ARPEGGIATOR) {
    arp_keys_.SustainableNoteOff(note);
  } else {
    bool off = manual_keys_.SustainableNoteOff(note);
    if (off && (sent_from_step_editor || manual_control())) {
      InternalNoteOff(note);
    }
  }
  return midi_.out_mode == MIDI_OUT_MODE_THRU && !polychained_;
}

void Part::PressedKeysSustainOn(PressedKeys &keys) {
  switch (midi_.sustain_mode) {
    case SUSTAIN_MODE_NORMAL:
      keys.ignore_note_off_messages = true;
      break;
    case SUSTAIN_MODE_SOSTENUTO:
      keys.SetSustainable(true);
      break;
    case SUSTAIN_MODE_LATCH:
    case SUSTAIN_MODE_MOMENTARY_LATCH:
      keys.ignore_note_off_messages = true;
      keys.release_latched_keys_on_next_note_on = true;
      break;
    case SUSTAIN_MODE_CLUTCH_UP:
      keys.Clutch(false);
      break;
    case SUSTAIN_MODE_CLUTCH_DOWN:
      keys.Clutch(true);
      break;
    case SUSTAIN_MODE_OFF:
    default:
      break;
  }
}

void Part::PressedKeysSustainOff(PressedKeys &keys) {
  switch (midi_.sustain_mode) {
    case SUSTAIN_MODE_NORMAL:
      keys.ignore_note_off_messages = false;
      ReleaseLatchedNotes(keys);
      break;
    case SUSTAIN_MODE_SOSTENUTO:
      keys.SetSustainable(false);
      ReleaseLatchedNotes(keys);
      break;
    case SUSTAIN_MODE_LATCH:
      keys.ignore_note_off_messages = false;
      keys.release_latched_keys_on_next_note_on = true;
      break;
    case SUSTAIN_MODE_MOMENTARY_LATCH:
      PressedKeysResetLatch(keys);
    case SUSTAIN_MODE_CLUTCH_UP:
      keys.Clutch(true);
      break;
    case SUSTAIN_MODE_CLUTCH_DOWN:
      keys.Clutch(false);
      break;
    case SUSTAIN_MODE_OFF:
    default:
      break;
  }
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
      (value >= 64) == (midi_.sustain_polarity == 0) ?
        SustainOn() : SustainOff();
      break;

    case 0x70:
      if (looped()) {
        looper_.RemoveOldestNote();
      } else if (seq_recording_) {
        RecordStep(SEQUENCER_STEP_TIE);
      }
      break;
    
    case 0x71:
      if (looped()) {
        looper_.RemoveNewestNote();
      } else if (seq_recording_) {
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
        (voicing_.allocation_mode == VOICE_ALLOCATION_MODE_POLY || \
         voicing_.allocation_mode == VOICE_ALLOCATION_MODE_POLY_STEAL_MOST_RECENT) ? \
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
  SequencerStep step;

  bool clock = !arp_seq_prescaler_;
  bool play = midi_.play_mode != PLAY_MODE_MANUAL && (
    !looped() || (
      midi_.play_mode == PLAY_MODE_ARPEGGIATOR && !seq_driven_arp()
    )
  );

  if (clock && play) {
    step = BuildSeqStep();
    if (midi_.play_mode == PLAY_MODE_ARPEGGIATOR) {
      arp_ = BuildArpState(step);
      step = arp_.step;
    }
    if (step.has_note()) {
      if (step.is_slid()) {
        InternalNoteOn(step.note(), step.velocity());
        StopSequencerArpeggiatorNotes();
      } else {
        StopSequencerArpeggiatorNotes();
        InternalNoteOn(step.note(), step.velocity());
      }
      generated_notes_.NoteOn(step.note(), step.velocity());
      gate_length_counter_ = seq_.gate_length;
    }
  }

  if (clock) {
    ++seq_step_;
    if (seq_step_ >= seq_.num_steps) {
      seq_step_ = 0;
    }
  }

  if (play) {
    if (num_voices_ > 1) {
      while (generated_notes_.size() > num_voices_) {
        const NoteEntry note = generated_notes_.played_note(0);
        generated_notes_.NoteOff(note.note);
      }
    } else if (gate_length_counter_) {
      --gate_length_counter_;
    } else if (generated_notes_.most_recent_note_index()) {
      // Peek at next step to see if it's a continuation
      step = BuildSeqStep();
      if (midi_.play_mode == PLAY_MODE_ARPEGGIATOR) {
        step = BuildArpState(step).step;
      }
      if (step.is_continuation()) {
        // The next step contains a "sustain" message; or a slid note. Extends
        // the duration of the current note.
        gate_length_counter_ += clock_division::list[seq_.clock_division].num_ticks;
      } else {
        StopSequencerArpeggiatorNotes();
      }
    }
  }
  
  ++arp_seq_prescaler_;
  if (arp_seq_prescaler_ >= clock_division::list[seq_.clock_division].num_ticks) {
    arp_seq_prescaler_ = 0;
  }
  
  for (uint8_t i = 0; i < num_voices_; ++i) {
    voice_[i]->Clock();
  }

  looper_.Clock();
}

void Part::Start() {
  arp_seq_prescaler_ = 0;

  seq_step_ = 0;
  
  arp_.ResetKey();
  arp_.step_index = 0;
  
  looper_.Rewind();
  std::fill(
    &looper_note_recording_pressed_key_[0],
    &looper_note_recording_pressed_key_[kNoteStackSize],
    looper::kNullIndex
  );
  std::fill(
    &looper_note_index_for_generated_note_index_[0],
    &looper_note_index_for_generated_note_index_[kNoteStackSize],
    looper::kNullIndex
  );
  std::fill(
    &output_pitch_for_looper_note_[0],
    &output_pitch_for_looper_note_[kNoteStackSize],
    looper::kNullIndex
  );

  generated_notes_.Clear();
}

void Part::Stop() {
  StopSequencerArpeggiatorNotes();
  AllNotesOff();
}

void Part::StopRecording() {
  if (!seq_recording_) { return; }
  seq_recording_ = false;
  if (looped()) {
    // Stop recording any held notes
    for (uint8_t i = 1; i <= manual_keys_.stack.max_size(); ++i) {
      const NoteEntry& e = manual_keys_.stack.note(i);
      if (e.note == NOTE_STACK_FREE_SLOT) { continue; }
      // This could be a transpose key that was held before StartRecording
      if (!looper_is_recording(i)) { continue; }
      LooperRecordNoteOff(i);
    }
  }
}

void Part::StartRecording() {
  if (seq_recording_) {
    return;
  }
  seq_recording_ = true;
  if (looped() && manual_control()) {
    // Start recording any held notes
    for (uint8_t i = 1; i <= manual_keys_.stack.max_size(); ++i) {
      const NoteEntry& e = manual_keys_.stack.note(i);
      if (e.note == NOTE_STACK_FREE_SLOT) { continue; }
      LooperRecordNoteOn(i);
    }
  } else {
    seq_rec_step_ = 0;
    seq_overdubbing_ = seq_.num_steps > 0;
  }
}

void Part::DeleteRecording() {
  if (midi_.play_mode == PLAY_MODE_MANUAL) { return; }
  StopSequencerArpeggiatorNotes();
  looped() ? looper_.RemoveAll() : DeleteSequence();
}

void Part::DeleteSequence() {
  std::fill(
    &seq_.step[0],
    &seq_.step[kNumSteps],
    SequencerStep(SEQUENCER_STEP_REST, 0)
  );
  seq_rec_step_ = 0;
  seq_step_ = 0;
  seq_.num_steps = 0;
  seq_overdubbing_ = false;
}

void Part::StopSequencerArpeggiatorNotes() {
  while (generated_notes_.most_recent_note_index()) {
    uint8_t generated_note_index = generated_notes_.most_recent_note_index();
    uint8_t pitch = generated_notes_.note(generated_note_index).note;
    uint8_t looper_note_index = looper_note_index_for_generated_note_index_[generated_note_index];

    looper_note_index_for_generated_note_index_[generated_note_index] = looper::kNullIndex;
    generated_notes_.NoteOff(pitch);
    if (looper_in_use()) {
      if (midi_.play_mode == PLAY_MODE_ARPEGGIATOR) {
        pitch = output_pitch_for_looper_note_[looper_note_index];
      }
      if (!looper_can_control(pitch)) { continue; }
    }
    InternalNoteOff(pitch);
  }
}

uint8_t Part::ApplySequencerInputResponse(int16_t pitch, int8_t root_pitch) const {
  if (midi_.play_mode == PLAY_MODE_ARPEGGIATOR || !transposable_) {
    return pitch;
  }

  // Find the most recent manual key that isn't being used to record
  uint8_t transpose_key = manual_keys_.stack.most_recent_note_index();
  while (transpose_key && looper_is_recording(transpose_key)) {
    transpose_key = manual_keys_.stack.note(transpose_key).next_ptr;
  }
  if (!transpose_key) { return pitch; }

  uint8_t transpose_pitch = manual_keys_.stack.note(transpose_key).note;
  switch (midi_.input_response) {
    case SEQUENCER_INPUT_RESPONSE_TRANSPOSE:
      pitch += transpose_pitch - root_pitch;
      while (pitch > 127) { pitch -= 12; }
      while (pitch < 0) { pitch += 12; }
      break;

    case SEQUENCER_INPUT_RESPONSE_REPLACE:
      pitch = transpose_pitch;
      break;

    case SEQUENCER_INPUT_RESPONSE_DIRECT:
    case SEQUENCER_INPUT_RESPONSE_OFF:
      break;
  }
  return pitch;
}

const SequencerStep Part::BuildSeqStep() const {
  const SequencerStep& step = seq_.step[seq_step_];
  int16_t note = step.note();
  if (step.has_note()) {
    // When we play a monophonic sequence, we can make the guess that root
    // note = first note.
    // But this is not the case when we are playing several sequences at the
    // same time. In this case, we use root note = 60.
    int8_t root_note = !has_siblings_ ? seq_.first_note() : 60;
    note = ApplySequencerInputResponse(note, root_note);
  }
  return SequencerStep((0x80 & step.data[0]) | (0x7f & note), step.data[1]);
}

const ArpeggiatorState Part::BuildArpState(SequencerStep seq_step) const {
  ArpeggiatorState next = arp_;
  // In case the pattern doesn't hit a note, the default output step is a REST
  next.step.data[0] = SEQUENCER_STEP_REST;

  // Advance pattern
  uint32_t pattern_mask;
  uint32_t pattern;
  uint8_t pattern_length;
  bool hit = false;
  if (seq_driven_arp()) {
    pattern_length = seq_.num_steps;
    if (seq_step.has_note()) {
      hit = true;
    } else { // Here, the output step can also be a TIE
      next.step.data[0] = seq_step.data[0];
    }
  } else {
    // Build a dummy input step for ROTATE/SUBROTATE
    seq_step.data[0] = kC4 + 1 + next.step_index;
    seq_step.data[1] = 0x7f; // Full velocity

    if (seq_.euclidean_length != 0) {
      pattern_length = seq_.euclidean_length;
      pattern_mask = 1 << ((next.step_index + seq_.euclidean_rotate) % seq_.euclidean_length);
      // Read euclidean pattern from ROM.
      uint16_t offset = static_cast<uint16_t>(seq_.euclidean_length - 1) * 32;
      pattern = lut_euclidean[offset + seq_.euclidean_fill];
      hit = pattern_mask & pattern;
    } else {
      pattern_length = 16;
      pattern_mask = 1 << next.step_index;
      pattern = lut_arpeggiator_patterns[seq_.arp_pattern - 1];
      hit = pattern_mask & pattern;
    }
  }
  ++next.step_index;
  if (next.step_index >= pattern_length) {
    next.step_index = 0;
  }

  // If the pattern didn't hit a note, return a REST/TIE output step, and don't
  // advance the arp key
  if (!hit) { return next; }
  uint8_t num_keys = arp_keys_.stack.size();
  if (!num_keys) {
    next.ResetKey();
    return next;
  }

  uint8_t key_with_octave = next.octave * num_keys + next.key_index;
  // Update arepggiator note/octave counter.
  switch (seq_.arp_direction) {
    case ARPEGGIATOR_DIRECTION_RANDOM:
      {
        uint16_t random = Random::GetSample();
        next.octave = random & 0xff;
        next.key_index = random >> 8;
      }
      break;
    case ARPEGGIATOR_DIRECTION_STEP_ROTATE:
      {
        next.key_increment = 0; // These arp directions move before playing the note
        if (seq_step.is_white()) {
          next.key_index = key_with_octave + seq_step.white_key_distance_from_middle_c();
          next.octave = next.key_index / num_keys;
        } else { // If black key
          next.key_index = seq_step.black_key_distance_from_middle_c();
          next.octave = abs(next.key_index / num_keys);
          if (next.octave > seq_.arp_range) {
            return next; // Rest
          }
        }
      }
      break;
    case ARPEGGIATOR_DIRECTION_STEP_SUBROTATE:
      {
        next.key_increment = 0; // These arp directions move before playing the note
        // Movement instructions derived from sequence step
        uint8_t limit = seq_step.octave();
        uint8_t clock;
        uint8_t spacer;
        if (seq_step.is_white()) {
          clock = seq_step.white_key_value();
          spacer = 1;
        } else {
          clock = 1;
          spacer = seq_step.black_key_value() + 1;
        }
        uint8_t old_pos = modulo(key_with_octave / spacer, limit);
        uint8_t new_pos = modulo(old_pos + clock, limit);
        int8_t key_without_wrap = key_with_octave + spacer * (new_pos - old_pos);
        next.octave = key_without_wrap / num_keys;
        if (next.octave < 0 || next.octave > seq_.arp_range) {
          // If outside octave range
          next.key_index = key_with_octave - spacer * old_pos;
          next.octave = next.key_index / num_keys;
        } else {
          next.key_index = key_without_wrap;
        }
      }
      break;
    default:
      {
        if (num_keys == 1 && seq_.arp_range == 0) {
          // This is a corner case for the Up/down pattern code.
          // Get it out of the way.
          next.key_index = 0;
          next.octave = 0;
        } else {
          bool wrapped = true;
          while (wrapped) {
            if (next.key_index >= num_keys || next.key_index < 0) {
              next.octave += next.key_increment;
              next.key_index = next.key_increment > 0 ? 0 : num_keys - 1;
            }
            wrapped = false;
            if (next.octave > seq_.arp_range || next.octave < 0) {
              next.octave = next.key_increment > 0 ? 0 : seq_.arp_range;
              if (seq_.arp_direction == ARPEGGIATOR_DIRECTION_UP_DOWN) {
                next.key_increment = -next.key_increment;
                next.key_index = next.key_increment > 0 ? 1 : num_keys - 2;
                next.octave = next.key_increment > 0 ? 0 : seq_.arp_range;
                wrapped = true;
              }
            }
          }
        }
      }
      break;
  }
  // Invariants
  next.octave = stmlib::modulo(next.octave, seq_.arp_range + 1);
  next.key_index = stmlib::modulo(next.key_index, num_keys);

  // Build arpeggiator step
  const NoteEntry* arpeggio_note = &arp_keys_.stack.played_note(next.key_index);
  next.key_index += next.key_increment;

  // TODO step type algorithm

  uint8_t note = arpeggio_note->note;
  uint8_t velocity = arpeggio_note->velocity & 0x7f;
  if (
    seq_.arp_direction == ARPEGGIATOR_DIRECTION_STEP_ROTATE ||
    seq_.arp_direction == ARPEGGIATOR_DIRECTION_STEP_SUBROTATE
  ) {
    velocity = (velocity * seq_step.velocity()) >> 7;
  }
  note += 12 * next.octave;
  while (note > 127) {
    note -= 12;
  }
  next.step.data[0] = note;
  next.step.data[1] = velocity;

  return next;
}

void Part::ResetAllControllers() {
  ResetLatch();
  for (uint8_t i = 0; i < num_voices_; ++i) {
    voice_[i]->ResetAllControllers();
  }
}

void Part::AllNotesOff() {
  poly_allocator_.ClearNotes();
  mono_allocator_.Clear();

  manual_keys_.Init();
  arp_keys_.Init();
  ResetLatch();

  generated_notes_.Clear();
  looper_note_index_for_generated_note_index_[generated_notes_.most_recent_note_index()] = looper::kNullIndex;
  for (uint8_t i = 0; i < num_voices_; ++i) {
    voice_[i]->NoteOff();
  }
  std::fill(
      &active_note_[0],
      &active_note_[kNumMaxVoicesPerPart],
      VOICE_ALLOCATION_NOT_FOUND);
}

void Part::ReleaseLatchedNotes(PressedKeys &keys) {
  for (uint8_t i = 1; i <= keys.stack.max_size(); ++i) {
    NoteEntry* e = keys.stack.mutable_note(i);
    if (!keys.IsSustained(*e)) { continue; }
    e->velocity &= ~PressedKeys::VELOCITY_SUSTAIN_MASK; // Un-flag the note
    NoteOff(tx_channel(), e->note);
  }
}

void Part::DispatchSortedNotes(bool unison, bool force_legato) {
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
      bool legato = force_legato || active_note_[i] == note_entry.note;
      VoiceNoteOn(voice_[i], note_entry.note, note_entry.velocity, legato);
      active_note_[i] = note_entry.note;
    } else {
      voice_[i]->NoteOff();
      active_note_[i] = VOICE_ALLOCATION_NOT_FOUND;
    }
  }
}

void Part::VoiceNoteOn(Voice* voice, uint8_t pitch, uint8_t velocity, bool legato) {
  VoiceNoteOnWithADSR(
    voice,
    Tune(pitch),
    velocity,
    (voicing_.legato_mode == LEGATO_MODE_AUTO_PORTAMENTO) && !legato ? 0 : voicing_.portamento,
    (voicing_.legato_mode == LEGATO_MODE_OFF) || !legato
  );
}

void Part::VoiceNoteOnWithADSR(
  Voice* voice,
  int16_t pitch,
  uint8_t vel,
  uint8_t portamento,
  bool trigger
) {
  int32_t amplitude_12bit = (voicing_.envelope_amplitude_init << 6) + vel * voicing_.envelope_amplitude_mod;
  CONSTRAIN(amplitude_12bit, 0, (1 << 12) - 1)
  voice->set_envelope_amplitude(amplitude_12bit << 4);

  voice->envelope()->SetADSR(
    modulate_7bit(voicing_.env_init_attack << 1, voicing_.env_mod_attack << 1, vel),
    modulate_7bit(voicing_.env_init_decay << 1, voicing_.env_mod_decay << 1, vel),
    modulate_7bit(voicing_.env_init_sustain << 1, voicing_.env_mod_sustain << 1, vel),
    modulate_7bit(voicing_.env_init_release << 1, voicing_.env_mod_release << 1, vel)
  );

  voice->NoteOn(pitch, vel, portamento, trigger);
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
        VoiceNoteOn(voice_[i], after.note, after.velocity, legato);
      }
    }
  } else if (voicing_.allocation_mode == VOICE_ALLOCATION_MODE_POLY_SORTED ||
             voicing_.allocation_mode == VOICE_ALLOCATION_MODE_POLY_UNISON_1 ||
             voicing_.allocation_mode == VOICE_ALLOCATION_MODE_POLY_UNISON_2) {
    DispatchSortedNotes(
        voicing_.allocation_mode != VOICE_ALLOCATION_MODE_POLY_SORTED, false);
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
      VoiceNoteOnWithADSR(
        voice_[voice_index],
        Tune(note),
        velocity,
        voicing_.portamento,
        true
      );
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
        VoiceNoteOnWithADSR(
          voice_[i],
          Tune(after.note),
          after.velocity,
          voicing_.portamento,
          voicing_.legato_mode == LEGATO_MODE_OFF
        );
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
      DispatchSortedNotes(true, true);
    }
  } else {
    uint8_t voice_index = \
        (voicing_.allocation_mode == VOICE_ALLOCATION_MODE_POLY ||
         voicing_.allocation_mode == VOICE_ALLOCATION_MODE_POLY_STEAL_MOST_RECENT) ? \
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
    voice_[i]->set_modulation_rate(voicing_.modulation_rate, i);
    voice_[i]->set_vibrato_range(voicing_.vibrato_range);
    voice_[i]->set_vibrato_initial(voicing_.vibrato_initial);
    voice_[i]->set_trigger_duration(voicing_.trigger_duration);
    voice_[i]->set_trigger_scale(voicing_.trigger_scale);
    voice_[i]->set_trigger_shape(voicing_.trigger_shape);
    voice_[i]->set_aux_cv(voicing_.aux_cv);
    voice_[i]->set_aux_cv_2(voicing_.aux_cv_2);
    voice_[i]->set_oscillator_mode(voicing_.oscillator_mode);
    voice_[i]->set_oscillator_shape(voicing_.oscillator_shape);
    voice_[i]->set_tuning(voicing_.tuning_transpose, voicing_.tuning_fine);
    voice_[i]->set_oscillator_pw_initial(voicing_.oscillator_pw_initial);
    voice_[i]->set_oscillator_pw_mod(voicing_.oscillator_pw_mod);
  }
}

bool Part::Set(uint8_t address, uint8_t value) {
  uint8_t* bytes;
  bytes = static_cast<uint8_t*>(static_cast<void*>(&midi_));
  uint8_t previous_value = bytes[address];
  bytes[address] = value;
  if (value == previous_value) { return false; }
  switch (address) {
    case PART_MIDI_CHANNEL:
    case PART_MIDI_MIN_NOTE:
    case PART_MIDI_MAX_NOTE:
    case PART_MIDI_MIN_VELOCITY:
    case PART_MIDI_MAX_VELOCITY:
    case PART_MIDI_INPUT_RESPONSE:
    case PART_MIDI_PLAY_MODE:
    case PART_MIDI_TRANSPOSE_OCTAVES:
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
    case PART_VOICING_TRIGGER_DURATION:
    case PART_VOICING_TRIGGER_SHAPE:
    case PART_VOICING_TRIGGER_SCALE:
    case PART_VOICING_AUX_CV:
    case PART_VOICING_AUX_CV_2:
    case PART_VOICING_OSCILLATOR_MODE:
    case PART_VOICING_OSCILLATOR_SHAPE:
    case PART_VOICING_OSCILLATOR_PW_INITIAL:
    case PART_VOICING_OSCILLATOR_PW_MOD:
    case PART_VOICING_TUNING_TRANSPOSE:
    case PART_VOICING_TUNING_FINE:
      TouchVoices();
      break;
      
    case PART_SEQUENCER_ARP_DIRECTION:
      arp_.key_increment = 1;
      break;

    case PART_MIDI_SUSTAIN_MODE:
    case PART_MIDI_SUSTAIN_POLARITY:
      ResetLatch();
      break;

    default:
      break;
  }
  return true;
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
