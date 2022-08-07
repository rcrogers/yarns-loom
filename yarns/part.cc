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
  voicing_.vibrato_mod = 0;
  voicing_.lfo_rate = 70;
  voicing_.lfo_spread_types = 0;
  voicing_.lfo_spread_voices = 0;
  voicing_.trigger_duration = 2;
  voicing_.aux_cv = MOD_AUX_ENVELOPE;
  voicing_.aux_cv_2 = MOD_AUX_ENVELOPE;
  voicing_.tuning_transpose = 0;
  voicing_.tuning_fine = 0;
  voicing_.tuning_root = 0;
  voicing_.tuning_system = TUNING_SYSTEM_EQUAL;
  voicing_.tuning_factor = 0;
  voicing_.oscillator_mode = OSCILLATOR_MODE_OFF;
  voicing_.oscillator_shape = OSC_SHAPE_FM;

  voicing_.timbre_initial = 64;
  voicing_.timbre_mod_velocity = 32;
  voicing_.timbre_mod_envelope = -16;
  voicing_.timbre_mod_lfo = 16;

  voicing_.amplitude_mod_velocity = 48;
  voicing_.env_init_attack = 64;
  voicing_.env_init_decay = 64;
  voicing_.env_init_sustain = 64;
  voicing_.env_init_release = 32;
  voicing_.env_mod_attack = -32;
  voicing_.env_mod_decay = -32;
  voicing_.env_mod_sustain = 0;
  voicing_.env_mod_release = 32;

  seq_.clock_division = 20;
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

uint8_t Part::HeldKeysNoteOn(HeldKeys &keys, uint8_t pitch, uint8_t velocity) {
  if (keys.stop_sustained_notes_on_next_note_on) StopSustainedNotes(keys);
  return keys.stack.NoteOn(pitch, velocity);
}

bool Part::NoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
  bool sent_from_step_editor = channel & 0x80;
  
  // scale velocity to compensate for its min/max range, so that voices using
  // velocity filtering can still have a full velocity range
  velocity = ((velocity - midi_.min_velocity) << 7) / (midi_.max_velocity - midi_.min_velocity + 1);

  if (seq_recording_) {
    if (!looped() && !sent_from_step_editor) {
      RecordStep(SequencerStep(note, velocity));
    } else if (looped()) {
      uint8_t pressed_key_index = HeldKeysNoteOn(manual_keys_, note, velocity);
      LooperRecordNoteOn(pressed_key_index);
    }
  } else if (midi_.play_mode == PLAY_MODE_ARPEGGIATOR) {
    HeldKeysNoteOn(arp_keys_, note, velocity);
  } else {
    HeldKeysNoteOn(manual_keys_, note, velocity);
    if (sent_from_step_editor || manual_control()) {
      InternalNoteOn(note, velocity);
    }
  }

  return midi_.out_mode == MIDI_OUT_MODE_THRU && !polychained_;
}

bool Part::NoteOff(uint8_t channel, uint8_t note, bool respect_sustain) {
  bool sent_from_step_editor = channel & 0x80;

  uint8_t pressed_key_index = manual_keys_.stack.Find(note);
  if (seq_recording_ && looped() && looper_is_recording(pressed_key_index)) {
    // Directly mapping pitch to looper notes would be cleaner, but requires a
    // data structure more sophisticated than an array
    LooperRecordNoteOff(pressed_key_index);
    // Sustain is respected only if it was applied before recording
    if (!manual_keys_.IsSustained(note)) {
      manual_keys_.stack.NoteOff(note);
    }
  } else if (midi_.play_mode == PLAY_MODE_ARPEGGIATOR) {
    arp_keys_.NoteOff(note, respect_sustain);
  } else {
    bool off = manual_keys_.NoteOff(note, respect_sustain);
    if (off && (sent_from_step_editor || manual_control())) {
      InternalNoteOff(note);
    }
  }
  return midi_.out_mode == MIDI_OUT_MODE_THRU && !polychained_;
}

void Part::HeldKeysSustainOn(HeldKeys &keys) {
  switch (midi_.sustain_mode) {
    case SUSTAIN_MODE_NORMAL:
      keys.universally_sustainable = true;
      break;
    case SUSTAIN_MODE_SOSTENUTO:
      keys.SetIndividuallySustainable(true);
      break;
    case SUSTAIN_MODE_LATCH:
    case SUSTAIN_MODE_MOMENTARY_LATCH:
      keys.Latch(true);
      break;
    case SUSTAIN_MODE_FILTER:
      keys.universally_sustainable = true;
      keys.stop_sustained_notes_on_next_note_on = false;
      break;
    case SUSTAIN_MODE_CLUTCH:
      keys.Clutch(true);
      break;
    case SUSTAIN_MODE_OFF:
    default:
      break;
  }
}

void Part::HeldKeysSustainOff(HeldKeys &keys) {
  switch (midi_.sustain_mode) {
    case SUSTAIN_MODE_NORMAL:
      keys.universally_sustainable = false;
      StopSustainedNotes(keys);
      break;
    case SUSTAIN_MODE_SOSTENUTO:
      keys.SetIndividuallySustainable(false);
      StopSustainedNotes(keys);
      break;
    case SUSTAIN_MODE_LATCH:
    case SUSTAIN_MODE_MOMENTARY_LATCH:
    case SUSTAIN_MODE_FILTER:
      if (midi_.sustain_mode == SUSTAIN_MODE_MOMENTARY_LATCH) {
        StopSustainedNotes(keys);
      }
      keys.Latch(false);
      break;
    case SUSTAIN_MODE_CLUTCH:
      keys.Clutch(false);
      break;
    case SUSTAIN_MODE_OFF:
    default:
      break;
  }
}

void Part::ResetAllKeys() {
  ResetKeys(manual_keys_);
  ResetKeys(arp_keys_);
  ControlChange(0, kCCHoldPedal, hold_pedal_engaged_ ? 127 : 0);
}

bool Part::ControlChange(uint8_t channel, uint8_t controller, uint8_t value) {
  switch (controller) {
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
      hold_pedal_engaged_ = value >= 64;
      hold_pedal_engaged_ == (midi_.sustain_polarity == 0) ?
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

    case 0x73:
      if (looped()) {
        looper_.pos_offset = value << 9;
        ui.SplashOn(SPLASH_LOOPER_PHASE_OFFSET);
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
    // Set slide flag
    seq_.step[seq_rec_step_].data[1] |= 0x80;
  }
  
  return midi_.out_mode != MIDI_OUT_MODE_OFF;
}

bool Part::Aftertouch(uint8_t channel, uint8_t note, uint8_t velocity) {
  if (voicing_.allocation_mode != VOICE_ALLOCATION_MODE_MONO) {
    uint8_t voice_index = \
        uses_poly_allocator() ? \
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
    VoiceNoteOff(i);
    voice_[i]->ResetAllControllers();
  }
}

void Part::Clock() { // From Multi::ClockFast
  if (looper_in_use() || midi_.play_mode == PLAY_MODE_MANUAL) return;

  if (multi.tick_counter() % PPQN() == 0) { // New step
    uint32_t step_counter = multi.tick_counter() / PPQN();
    StepSequencerArpeggiator(step_counter);
  }
  ClockStepGateEndings();
}

void Part::StepSequencerArpeggiator(uint32_t step_counter) {
  // Advance euclidean state.  If skipping this beat, don't advance other state
  if (seq_.euclidean_length != 0) {
    euclidean_step_index_ = (euclidean_step_index_ + 1) % seq_.euclidean_length;
    uint32_t pattern_mask = 1 << ((euclidean_step_index_ + seq_.euclidean_rotate) % seq_.euclidean_length);
    // Read euclidean pattern from ROM.
    uint16_t offset = static_cast<uint16_t>(seq_.euclidean_length - 1) << 5;
    uint32_t pattern = lut_euclidean[offset + seq_.euclidean_fill];
    if (!(pattern_mask & pattern)) return;
  }

  SequencerStep* step_ptr = NULL;
  SequencerStep step;
  if (seq_.num_steps) {
    seq_step_ = step_counter % seq_.num_steps;
    step = BuildSeqStep(seq_step_);
    step_ptr = &step;
  }
  if (midi_.play_mode == PLAY_MODE_ARPEGGIATOR) {
    arp_ = BuildArpState(step_ptr);
    step_ptr = &arp_.step;
  }
  if (step_ptr && step_ptr->has_note()) {
    uint8_t pitch = step_ptr->note();
    uint8_t velocity = step_ptr->velocity();
    GeneratedNoteOff(pitch); // Simulate a human retriggering a key
    if (GeneratedNoteOn(pitch, velocity) && !manual_keys_.stack.Find(pitch)) {
      InternalNoteOn(pitch, velocity, step_ptr->is_slid());
    }
  }
}

void Part::ClockStepGateEndings() {
  for (uint8_t v = 0; v < num_voices_; ++v) {
    if (gate_length_counter_[v]) { // Gate hasn't ended yet
      --gate_length_counter_[v];
      continue;
    }
    // Peek at next step to see if it's a continuation
    // If more than one voice has a step ending, the peek is redundant
    SequencerStep* next_step_ptr = NULL;
    SequencerStep next_step;
    if (seq_.num_steps) {
      next_step = BuildSeqStep((seq_step_ + 1) % seq_.num_steps);
      next_step_ptr = &next_step;
    }
    if (midi_.play_mode == PLAY_MODE_ARPEGGIATOR) {
      next_step = BuildArpState(next_step_ptr).step;
      next_step_ptr = &next_step;
    }
    if (next_step_ptr && next_step_ptr->is_continuation()) {
      // The next step contains a "sustain" message; or a slid note. Extends
      // the duration of the current note.
      gate_length_counter_[v] += PPQN();
    } else if (active_note_[v] != VOICE_ALLOCATION_NOT_FOUND) {
      GeneratedNoteOff(active_note_[v]);
    }
  }
}

void Part::Start() {
  arp_.ResetKey();
  arp_.step_index = euclidean_step_index_ = -1;
  
  looper_.Rewind();
  std::fill(
    &looper_note_recording_pressed_key_[0],
    &looper_note_recording_pressed_key_[kNoteStackMapping],
    looper::kNullIndex
  );
  std::fill(
    &looper_note_index_for_generated_note_index_[0],
    &looper_note_index_for_generated_note_index_[kNoteStackMapping],
    looper::kNullIndex
  );
  std::fill(
    &output_pitch_for_looper_note_[0],
    &output_pitch_for_looper_note_[kNoteStackMapping],
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
      if (
        e.note == NOTE_STACK_FREE_SLOT ||
        manual_keys_.IsSustained(e)
      ) { continue; }
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
  seq_overwrite_ = false;
}

void Part::DeleteSequence() {
  std::fill(
    &seq_.step[0],
    &seq_.step[kNumSteps],
    SequencerStep(SEQUENCER_STEP_REST, 0)
  );
  seq_rec_step_ = 0;
  seq_.num_steps = 0;
  seq_overdubbing_ = false;
}

uint8_t Part::GeneratedNoteOn(uint8_t pitch, uint8_t velocity) {
  if (
    mono_allocator_.size() == mono_allocator_.max_size() ||
    generated_notes_.size() == generated_notes_.max_size()
  ) {
    return 0;
  }
  return generated_notes_.NoteOn(pitch, velocity);
}

void Part::StopSequencerArpeggiatorNotes() {
  while (generated_notes_.most_recent_note_index()) {
    GeneratedNoteOff(generated_notes_.most_recent_note().note);
  }
}

void Part::GeneratedNoteOff(uint8_t pitch) {
  uint8_t generated_note_index = generated_notes_.Find(pitch);
  uint8_t looper_note_index = looper_note_index_for_generated_note_index_[generated_note_index];
  looper_note_index_for_generated_note_index_[generated_note_index] = looper::kNullIndex;
  generated_notes_.NoteOff(pitch);
  if (looper_in_use()) {
    if (midi_.play_mode == PLAY_MODE_ARPEGGIATOR) {
      pitch = output_pitch_for_looper_note_[looper_note_index];
    }
    if (!looper_can_control(pitch)) return;
  } else if (manual_keys_.stack.Find(pitch)) return;
  InternalNoteOff(pitch);
}

uint8_t Part::ApplySequencerInputResponse(int16_t pitch, int8_t root_pitch) const {
  if (midi_.play_mode == PLAY_MODE_ARPEGGIATOR) {
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

const SequencerStep Part::BuildSeqStep(uint8_t step_index) const {
  const SequencerStep& step = seq_.step[step_index];
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

const ArpeggiatorState Part::BuildArpState(SequencerStep* seq_step_ptr) const {
  SequencerStep seq_step;
  ArpeggiatorState next = arp_;
  // In case the pattern doesn't hit a note, the default output step is a REST
  next.step.data[0] = SEQUENCER_STEP_REST;

  // Always advance the pattern counter, even if we rest
  next.step_index = (next.step_index + 1) % 16;

  // If sequencer/pattern doesn't hit a note, return a REST/TIE output step, and
  // don't advance the arp key
  uint32_t pattern_mask, pattern;
  if (seq_driven_arp()) {
    if (!seq_step_ptr) return next;
    seq_step = *seq_step_ptr;
    if (!seq_step.has_note()) { // Here, the output step can also be a TIE
      next.step.data[0] = seq_step.data[0];
      return next;
    }
  } else {
    // Build a dummy input step for JUMP/GRID
    seq_step.data[0] = kC4 + 1 + next.step_index;
    seq_step.data[1] = 0x7f; // Full velocity
    pattern_mask = 1 << next.step_index;
    pattern = lut_arpeggiator_patterns[seq_.arp_pattern - 1];
    if (!(pattern_mask & pattern)) return next;
  }

  uint8_t num_keys = arp_keys_.stack.size();
  if (!num_keys) {
    next.ResetKey();
    return next;
  }

  uint8_t num_octaves = seq_.arp_range + 1;
  uint8_t num_keys_all_octaves = num_keys * num_octaves;
  uint8_t display_octave = seq_step.octave();
  if (display_octave > 0) display_octave--; // Match octave display in UI, with floor 0
  // Update arepggiator note/octave counter.
  switch (seq_.arp_direction) {
    case ARPEGGIATOR_DIRECTION_RANDOM:
      {
        uint16_t random = Random::GetSample();
        next.octave = random & 0xff;
        next.key_index = random >> 8;
      }
      break;
    case ARPEGGIATOR_DIRECTION_STEP_JUMP:
      {
        // If step value by color within octave is greater than total chord size, rest without moving
        if (seq_step.color_key_value() >= num_keys_all_octaves) return next;

        // Advance active position by octave # -- C4 -> pos + 4
        next.key_index = modulo(next.key_index + display_octave, num_keys_all_octaves);
        if (seq_step.is_white()) {
          next.key_increment = 0; // Move is already complete
        } else { // If black key
          // Play the black key value as an absolute position in the arp chord,
          // then return to active position
          next.key_increment = next.key_index - seq_step.color_key_value();
          next.key_index = seq_step.color_key_value();
        }

        next.octave = modulo(next.key_index / num_keys, num_octaves);
      }
      break;
    case ARPEGGIATOR_DIRECTION_STEP_GRID:
      {
        // If step value by color within octave is greater than total chord size, rest without moving
        if (seq_step.color_key_value() >= num_keys_all_octaves) return next;

        // Map linear position to X-Y grid coordinates
        uint8_t size = std::max(static_cast<uint8_t>(1), display_octave); // C4 -> 4x4 grid; minimum 1x1
        uint8_t x_pos = modulo(next.key_index, size);
        uint8_t y_pos = modulo(next.key_index / size, size);
        // Move within grid
        if (seq_step.is_white()) {
          x_pos = modulo(x_pos + 1, size);
        } else {
          y_pos = modulo(y_pos + 1, size);
        }
        // Map grid position back to linear position, which can be > chord size
        next.key_index = x_pos + y_pos * size;
        next.key_increment = 0; // Move is already complete

        next.octave = modulo(next.key_index / num_keys, num_octaves);
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

  // Build arpeggiator step
  const NoteEntry* arpeggio_note = &arp_keys_.stack.played_note(modulo(next.key_index, num_keys));
  next.key_index += next.key_increment;

  // TODO step type algorithm

  uint8_t note = arpeggio_note->note;
  uint8_t velocity = arpeggio_note->velocity & 0x7f;
  if (seq_driven_arp()) {
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
  ResetAllKeys();
  for (uint8_t i = 0; i < num_voices_; ++i) {
    voice_[i]->ResetAllControllers();
  }
}

void Part::AllNotesOff() {
  poly_allocator_.ClearNotes();
  mono_allocator_.Clear();

  ResetAllKeys();

  generated_notes_.Clear();
  looper_note_index_for_generated_note_index_[generated_notes_.most_recent_note_index()] = looper::kNullIndex;
  for (uint8_t i = 0; i < num_voices_; ++i) {
    VoiceNoteOff(i);
  }
  std::fill(
      &active_note_[0],
      &active_note_[kNumMaxVoicesPerPart],
      VOICE_ALLOCATION_NOT_FOUND);
}

void Part::StopNotesBySustainStatus(HeldKeys &keys, bool sustain_status) {
  for (uint8_t i = 1; i <= keys.stack.max_size(); ++i) {
    const NoteEntry& e = keys.stack.note(i);
    if (e.note == NOTE_STACK_FREE_SLOT) continue;
    if (keys.IsSustained(e) != sustain_status) continue;
    NoteOff(tx_channel(), e.note, false);
  }
}

struct DispatchNote {
  NoteEntry const* note;
  bool done;
};

void Part::DispatchSortedNotes(bool via_note_off) {
  uint8_t num_notes = mono_allocator_.size();
  uint8_t num_dispatch = num_voices_;
  bool unison = voicing_.allocation_mode != VOICE_ALLOCATION_MODE_POLY_SORTED;
  if (!unison) { num_dispatch = std::min(num_dispatch, num_notes); }

  // Set up structures to track assignments
  DispatchNote dispatch[num_dispatch];
  for (uint8_t d = 0; d < num_dispatch; ++d) {
    dispatch[d].note = &priority_note(d % num_notes);
    dispatch[d].done = false;
  }
  bool voice_intact[num_voices_];
  std::fill(&voice_intact[0], &voice_intact[num_voices_], false);

  // First pass: find voices that don't need to change
  for (uint8_t v = 0; v < num_voices_; ++v) {
    for (uint8_t d = 0; d < num_dispatch; ++d) {
      if (dispatch[d].done) { continue; }
      if (active_note_[v] != dispatch[d].note->note) { continue; }
      dispatch[d].done = true;
      voice_intact[v] = true;
      break; // Voice keeps its current note
    }
  }
  // Second pass: change remaining voices
  for (uint8_t v = 0; v < num_voices_; ++v) {
    if (voice_intact[v]) { continue; }
    const NoteEntry* note = NULL;
    for (uint8_t d = 0; d < num_dispatch; ++d) {
      if (dispatch[d].done) { continue; }
      dispatch[d].done = true;
      note = dispatch[d].note;
      break; // Voice gets this note
    }
    if (note) {
      VoiceNoteOn(v, note->note, note->velocity, via_note_off, !via_note_off);
    } else if (active_note_[v] != VOICE_ALLOCATION_NOT_FOUND) {
      VoiceNoteOff(v);
    }
  }
}

void Part::VoiceNoteOn(
  uint8_t voice_index, uint8_t pitch, uint8_t vel,
  bool legato, bool reset_gate_counter
) {
  uint8_t portamento = voicing_.portamento;
  bool trigger = !legato;
  switch (voicing_.legato_mode) {
    case LEGATO_MODE_OFF:
      trigger = true;
      break;
    case LEGATO_MODE_AUTO_PORTAMENTO:
      if (trigger) { portamento = 0; }
      break;
    default:
      break;
  }
  // If this pitch is under manual control, don't extend the gate
  if (reset_gate_counter && !manual_keys_.stack.Find(pitch)) {
    gate_length_counter_[voice_index] = seq_.gate_length + 1;
  }
  active_note_[voice_index] = pitch;
  Voice* voice = voice_[voice_index];

  int32_t timbre_14 = (voicing_.timbre_mod_envelope << 7) + vel * voicing_.timbre_mod_velocity;
  CONSTRAIN(timbre_14, -1 << 13, (1 << 13) - 1)
  voice->set_timbre_mod_envelope(timbre_14 << 2);

  uint16_t vel_concave_up = UINT16_MAX - lut_env_expo[((127 - vel) << 1)];
  int32_t damping_22 = -voicing_.amplitude_mod_velocity * vel_concave_up;
  if (voicing_.amplitude_mod_velocity >= 0) {
    damping_22 += voicing_.amplitude_mod_velocity << 16;
  }

  voice->envelope()->SetADSR(
    UINT16_MAX - (damping_22 >> (22 - 16)),
    modulate_7bit(voicing_.env_init_attack, voicing_.env_mod_attack, vel),
    modulate_7bit(voicing_.env_init_decay, voicing_.env_mod_decay, vel),
    modulate_7bit(voicing_.env_init_sustain, voicing_.env_mod_sustain, vel),
    modulate_7bit(voicing_.env_init_release, voicing_.env_mod_release, vel)
  );

  voice->NoteOn(Tune(pitch), vel, portamento, trigger);
}

void Part::VoiceNoteOff(uint8_t voice) {
  voice_[voice]->NoteOff();
  active_note_[voice] = VOICE_ALLOCATION_NOT_FOUND;
}

void Part::InternalNoteOn(uint8_t note, uint8_t velocity, bool force_legato) {
  if (midi_.out_mode == MIDI_OUT_MODE_GENERATED_EVENTS && !polychained_) {
    midi_handler.OnInternalNoteOn(tx_channel(), note, velocity);
  }
  
  const NoteEntry& before = priority_note();
  mono_allocator_.NoteOn(note, velocity);
  const NoteEntry& after = priority_note();
  bool legato = force_legato || mono_allocator_.size() > 1;
  if (voicing_.allocation_mode == VOICE_ALLOCATION_MODE_MONO) {
    // Check if the note that has been played should be triggered according
    // to selected voice priority rules.
    if (before.note != after.note) {
      for (uint8_t i = 0; i < num_voices_; ++i) {
        VoiceNoteOn(i, after.note, after.velocity, legato, true);
      }
    }
  } else if (uses_sorted_dispatch()) {
    DispatchSortedNotes(false);
  } else {
    uint8_t voice_index = 0;
    switch (voicing_.allocation_mode) {
      case VOICE_ALLOCATION_MODE_POLY:
        voice_index = poly_allocator_.NoteOn(note, VOICE_STEALING_MODE_LRU);
        break;
      
      case VOICE_ALLOCATION_MODE_POLY_STEAL_MOST_RECENT:
        voice_index = poly_allocator_.NoteOn(note, VOICE_STEALING_MODE_MRU);
        break;

      case VOICE_ALLOCATION_MODE_POLY_NICE:
        voice_index = poly_allocator_.NoteOn(note, VOICE_STEALING_MODE_NONE);
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
      if (legato) {
        if (active_note_[voice_index] != VOICE_ALLOCATION_NOT_FOUND) {
          // Disable legato when stealing
          legato = false;
        } else {
          // Begin portamento from the preceding priority note
          voice_[voice_index]->SetPortamento(Tune(before.note), velocity, 0);
        }
      }
      // Prevent the same note from being simultaneously played on two channels.
      KillAllInstancesOfNote(note);
      VoiceNoteOn(voice_index, note, velocity, legato, true);
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
      VoiceNoteOff(index);
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
  
  bool had_extra_notes = mono_allocator_.size() > num_voices_;
  const NoteEntry& before = priority_note();
  mono_allocator_.NoteOff(note);
  const NoteEntry& after = priority_note();
  if (voicing_.allocation_mode == VOICE_ALLOCATION_MODE_MONO) {
    if (mono_allocator_.size() == 0) {
      // No key is pressed, we just close the gate.
      for (uint8_t i = 0; i < num_voices_; ++i) {
        VoiceNoteOff(i);
      }
    } else if (before.note != after.note) {
      // Removing the note gives priority to another note that is still held
      for (uint8_t i = 0; i < num_voices_; ++i) {
        VoiceNoteOn(i, after.note, after.velocity, true, false);
      }
    }
  } else if (uses_sorted_dispatch()) {
    KillAllInstancesOfNote(note);
    if (
        voicing_.allocation_mode == VOICE_ALLOCATION_MODE_POLY_UNISON_1 ||
        had_extra_notes
    ) {
      DispatchSortedNotes(true);
    }
  } else {
    uint8_t voice_index = \
        uses_poly_allocator() ? \
        poly_allocator_.NoteOff(note) : \
        FindVoiceForNote(note);
    if (voice_index < num_voices_) {
      VoiceNoteOff(voice_index);
      if (
        had_extra_notes &&
        voicing_.allocation_mode == VOICE_ALLOCATION_MODE_POLY_NICE
      ) {
        const NoteEntry& nice = priority_note(NOTE_STACK_PRIORITY_FIRST, num_voices_ - 1);
        poly_allocator_.NoteOn(nice.note, VOICE_STEALING_MODE_NONE);
        VoiceNoteOn(voice_index, nice.note, nice.velocity, true, false);
      }
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
  voice_[0]->garbage(0);
  for (uint8_t i = 0; i < num_voices_; ++i) {
    voice_[i]->set_pitch_bend_range(voicing_.pitch_bend_range);
    voice_[i]->set_vibrato_range(voicing_.vibrato_range);
    voice_[i]->set_vibrato_mod(voicing_.vibrato_mod);
    voice_[i]->set_tremolo_mod(voicing_.tremolo_mod);
    voice_[i]->set_lfo_shape(LFO_ROLE_PITCH, voicing_.vibrato_shape);
    voice_[i]->set_lfo_shape(LFO_ROLE_TIMBRE, voicing_.timbre_lfo_shape);
    voice_[i]->set_lfo_shape(LFO_ROLE_AMPLITUDE, voicing_.tremolo_shape);
    voice_[i]->set_trigger_duration(voicing_.trigger_duration);
    voice_[i]->set_trigger_scale(voicing_.trigger_scale);
    voice_[i]->set_trigger_shape(voicing_.trigger_shape);
    voice_[i]->set_aux_cv(voicing_.aux_cv);
    voice_[i]->set_aux_cv_2(voicing_.aux_cv_2);
    voice_[i]->set_oscillator_mode(voicing_.oscillator_mode);
    voice_[i]->set_oscillator_shape(voicing_.oscillator_shape);
    voice_[i]->set_tuning(voicing_.tuning_transpose, voicing_.tuning_fine);
    voice_[i]->set_timbre_init(voicing_.timbre_initial);
    voice_[i]->set_timbre_mod_lfo(voicing_.timbre_mod_lfo);
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
      // Shut all channels off when a MIDI parameter is changed to prevent
      // stuck notes.
      AllNotesOff();
      break;

    case PART_MIDI_TRANSPOSE_OCTAVES:
      // Release notes that are currently under direct manual control, sparing
      // notes that are controlled by sustain or the sequencer
      StopNotesBySustainStatus(manual_keys_, false);
      StopNotesBySustainStatus(arp_keys_, false);
      break;

    case PART_VOICING_ALLOCATION_MODE:
      TouchVoiceAllocation();
      break;
      
    case PART_VOICING_PITCH_BEND_RANGE:
    case PART_VOICING_LFO_RATE:
    case PART_VOICING_VIBRATO_RANGE:
    case PART_VOICING_VIBRATO_MOD:
    case PART_VOICING_TREMOLO_MOD:
    case PART_VOICING_VIBRATO_SHAPE:
    case PART_VOICING_TIMBRE_LFO_SHAPE:
    case PART_VOICING_TREMOLO_SHAPE:
    case PART_VOICING_TRIGGER_DURATION:
    case PART_VOICING_TRIGGER_SHAPE:
    case PART_VOICING_TRIGGER_SCALE:
    case PART_VOICING_AUX_CV:
    case PART_VOICING_AUX_CV_2:
    case PART_VOICING_OSCILLATOR_SHAPE:
    case PART_VOICING_TIMBRE_INIT:
    case PART_VOICING_TIMBRE_MOD_LFO:
    case PART_VOICING_TUNING_TRANSPOSE:
    case PART_VOICING_TUNING_FINE:
      TouchVoices();
      break;
      
    case PART_SEQUENCER_ARP_DIRECTION:
      arp_.key_increment = 1;
      break;

    case PART_MIDI_SUSTAIN_MODE:
    case PART_MIDI_SUSTAIN_POLARITY:
      AllNotesOff();
      break;

    case PART_VOICING_OSCILLATOR_MODE:
      AllNotesOff();
      TouchVoices();
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
